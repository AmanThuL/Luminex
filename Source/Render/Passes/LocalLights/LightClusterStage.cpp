//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterStage.cpp
/// @brief Allocates the paced froxel grid and declares deterministic count, scan and fill passes.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/LocalLights/LightClusterStage.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/GraphResources.h"
#include "Render/Common/PacedSlots.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/LocalLights/LightClusterFrameData.h"
#include "Render/Passes/LocalLights/LightClusterReadback.h"
#include "Render/Passes/LocalLights/LightRoots.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Scalar.h"
#include "Engine/Gpu/CaptureSchema.h"

#include <string>

namespace lmx::render {
namespace {

using light_detail::kCountBytes;
using light_detail::kCounterBytes;
using light_detail::kGridBytes;
using light_detail::kIndexBytes;

constexpr uint32_t kThreadsPerGroup = 256;
// Where each kernel leaves the buffers it writes, which is what a readback after it preserves.
constexpr SyncPoint kComputeWrite{rojoRHI::Stage::ComputeShader, rojoRHI::Access::ShaderWrite,
                                  rojoRHI::Layout::Undefined};

#ifndef NDEBUG
// The six counter words the scan writes; the two padding words keep the reset's zeros.
constexpr uint64_t kCounterValueBytes = 6 * sizeof(uint32_t);

//======================================================================================================================
rojoRHI::ResourceAccess computeAccess(const PassResources& resources, GraphBuffer handle,
                                      uint64_t bytes, rojoRHI::Access access) {
    return {.buffer = resources.buffer(handle),
            .size = bytes,
            .stage = rojoRHI::Stage::ComputeShader,
            .access = access};
}
#endif

} // namespace

//======================================================================================================================
LightClusterStage::LightClusterStage(engine::GpuContext& gpu)
    : m_gpu(gpu), m_readback(std::make_unique<light_detail::LightClusterReadback>()) {}

//======================================================================================================================
LightClusterStage::~LightClusterStage() = default;

//======================================================================================================================
void LightClusterStage::registerLayoutsForCapture() {
    engine::registerUniformStruct(
        {.name = "LightClusterParams",
         .sizeBytes = sizeof(LightClusterKernelParams),
         .fields = {{"view", offsetof(LightClusterKernelParams, view), "float4[4]"},
                    {"inverseProjection", offsetof(LightClusterKernelParams, inverseProjection),
                     "float4[4]"},
                    {"rowCount", offsetof(LightClusterKernelParams, rowCount), "uint"},
                    {"activeWidth", offsetof(LightClusterKernelParams, activeWidth), "uint"},
                    {"activeHeight", offsetof(LightClusterKernelParams, activeHeight), "uint"},
                    {"perClusterCap", offsetof(LightClusterKernelParams, perClusterCap), "uint"},
                    {"globalCapacity", offsetof(LightClusterKernelParams, globalCapacity), "uint"},
                    {"froxelCount", offsetof(LightClusterKernelParams, froxelCount), "uint"},
                    {"padding0", offsetof(LightClusterKernelParams, padding0), "uint"},
                    {"padding1", offsetof(LightClusterKernelParams, padding1), "uint"},
                    {"sliceDepth", offsetof(LightClusterKernelParams, sliceDepth), "float4[7]"}}});
    engine::registerUniformStruct(
        {.name = "LightClusterRoot",
         .sizeBytes = sizeof(LightClusterRoot),
         .fields = {{"params", offsetof(LightClusterRoot, params), "Addr"},
                    {"lights", offsetof(LightClusterRoot, lights), "Addr"},
                    {"counts", offsetof(LightClusterRoot, counts), "Addr"},
                    {"grid", offsetof(LightClusterRoot, grid), "Addr"},
                    {"indices", offsetof(LightClusterRoot, indices), "Addr"},
                    {"counters", offsetof(LightClusterRoot, counters), "Addr"}}});
}

//======================================================================================================================
lmx::Result<std::unique_ptr<LightClusterStage>> LightClusterStage::create(engine::GpuContext& gpu) {
    std::unique_ptr<LightClusterStage> self(new LightClusterStage(gpu));
    // The scan is a single thread on purpose: 3,456 counts summed in froxel order is what
    // makes every range and every counter a function of the declarations alone.
    constexpr std::array names{"LightClusterCount", "LightClusterScan", "LightClusterFill"};
    auto pipelines = createComputePipelines(gpu, names, "lmx.light.");
    if (!pipelines) {
        return std::unexpected(pipelines.error());
    }
    for (uint32_t index = 0; index < names.size(); ++index) {
        self->m_libraries[index] = std::move(pipelines->libraries[index]);
        self->m_pipelines[index] = std::move(pipelines->pipelines[index]);
    }
    for (uint32_t slot = 0; slot < self->m_slots.size(); ++slot) {
        const std::string suffix = "." + std::to_string(slot);
        auto allocate = [&](engine::GpuBuffer& buffer, uint64_t bytes,
                            const std::string& name) -> lmx::Result<void> {
            const std::string label = name + suffix;
            auto result =
                engine::GpuBuffer::create(gpu.device(), {.size = bytes,
                                                         .memory = rojoRHI::Memory::Gpu,
                                                         .usage = rojoRHI::BufferUsage::ShaderWrite,
                                                         .label = label});
            if (!result) {
                return std::unexpected(result.error());
            }
            buffer = std::move(*result);
            return {};
        };
        auto& owned = self->m_slots[slot];
        auto result = allocate(owned.grid, kGridBytes, "lmx.light.grid");
        if (!result) {
            return std::unexpected(result.error());
        }
        result = allocate(owned.indices, kIndexBytes, "lmx.light.indices");
        if (!result) {
            return std::unexpected(result.error());
        }
        result = allocate(owned.counts, kCountBytes, "lmx.light.counts");
        if (!result) {
            return std::unexpected(result.error());
        }
        result = allocate(owned.counters, kCounterBytes, "lmx.light.counters");
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    return self;
}

//======================================================================================================================
void LightClusterStage::setCapacityOverride(uint32_t perCluster, uint32_t global) {
    LMX_ASSERT(perCluster > 0 && perCluster <= kMaxLightsPerCluster,
               "setCapacityOverride: the per-cluster cap must fit the frozen capacity");
    LMX_ASSERT(global > 0 && global <= kLightClusterIndexCapacity,
               "setCapacityOverride: the global capacity must fit the frozen index list");
    m_perClusterCap = perCluster;
    m_globalCapacity = global;
}

//======================================================================================================================
LightClusterOutputs LightClusterStage::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                               const LightClusterInputs& inputs) {
    // Nothing is imported and no pass is declared unless the frame actually shades from the grid:
    // a frame without local lights must compile to exactly the record it compiled to before.
    if (!inputs.clustered || inputs.liveLightCount == 0) {
        return {};
    }
    LMX_ASSERT(inputs.activeWidth > 0 && inputs.activeHeight > 0,
               "LightClusterStage::declare: the active render rectangle must be nonempty");
    LMX_ASSERT(inputs.liveLightCount <= inputs.rowCount,
               "LightClusterStage::declare: live lights must fit the addressable rows");
    LMX_ASSERT(!m_readback->hasPendingSlot(inputs.frameNumber),
               "retire a light-cluster slot before reusing its GPU buffers");
    auto& slot = m_slots[inputs.frameNumber % 3];
    const auto params = buildLightClusterKernelParams(
        {.view = inputs.view,
         .inverseJitteredProjection = inputs.inverseJitteredProjection,
         .rowCount = inputs.rowCount,
         .activeWidth = inputs.activeWidth,
         .activeHeight = inputs.activeHeight,
         .perClusterCap = m_perClusterCap,
         .globalCapacity = m_globalCapacity,
         .sliceDepth = inputs.sliceDepth});
    // One upload serves all three kernels; its address lives until this frame slot recycles.
    const rojoRHI::Addr paramsAddr = frameData(commands, params);
    engine::registerFrameRoot("LightClusterParams", paramsAddr);
    const auto tickets = m_readback->enqueue(inputs.frameNumber, inputs.captureLists);
    const bool captureLists = inputs.captureLists;
    const engine::GpuBuffer* const gridOwner = &slot.grid;
    const engine::GpuBuffer* const indexOwner = &slot.indices;
    const engine::GpuBuffer* const counterOwner = &slot.counters;

    const auto grid =
        importPaced(graph, slot.grid, "lmx.light.grid",
                    slot.shaderRead ? BufferUse::ShaderRead : BufferUse::StorageRead, slot.used);
    const auto indices =
        importPaced(graph, slot.indices, "lmx.light.indices",
                    slot.shaderRead ? BufferUse::ShaderRead : BufferUse::StorageWrite, slot.used);
    const auto counts =
        importPaced(graph, slot.counts, "lmx.light.counts", BufferUse::StorageRead, slot.used);
    const auto counters =
        importPaced(graph, slot.counters, "lmx.light.counters", BufferUse::StorageWrite, slot.used);

    // The counters the scan writes are complete on their own; the fill is what makes them a
    // reconciling total of bytes the GPU actually holds, so the zero fill is the stated start.
    declareZeroFill(graph, commands, "lmx.pass.light.reset", counters, kCounterBytes);
    const auto countersReset = nextVersion(counters);

    const uint32_t groups = divRoundUp(kClusterCount, kThreadsPerGroup);
    const auto lights = inputs.lights;
    // The kernels read rows [0, rowCount) and the fill writes inside the global capacity.
    [[maybe_unused]] const uint64_t lightBytes =
        uint64_t{inputs.rowCount} * sizeof(engine::LightRow);
    [[maybe_unused]] const uint64_t listBytes = uint64_t{m_globalCapacity} * sizeof(uint32_t);
    ComputePassDesc count;
    count.shaderBufferReads = {lights};
    count.bufferWrites = {counts};
    graph.addComputePass(
        "lmx.pass.light.count", std::move(count),
        [=, this, &commands](const PassResources& resources) {
            commands.setPipeline(m_pipelines[0].get());
            engine::registerFrameRoot(
                "LightClusterRoot",
                commands.pushRoot(LightClusterRoot{.params = paramsAddr,
                                                   .lights = resources.address(lights),
                                                   .counts = resources.address(counts)}));
#ifndef NDEBUG
            const std::array accesses{
                computeAccess(resources, lights, lightBytes, rojoRHI::Access::ShaderRead),
                computeAccess(resources, counts, kCountBytes, rojoRHI::Access::ShaderWrite)};
            commands.declareAccess(accesses);
#endif
            commands.dispatch(groups, 1, 1);
        });
    const auto countsFilled = nextVersion(counts);

    ComputePassDesc scan;
    scan.bufferReads = {countsFilled};
    scan.bufferWrites = {grid, countersReset};
    graph.addComputePass(
        "lmx.pass.light.scan", std::move(scan),
        [=, this, &commands](const PassResources& resources) {
            commands.setPipeline(m_pipelines[1].get());
            engine::registerFrameRoot(
                "LightClusterRoot",
                commands.pushRoot(LightClusterRoot{.params = paramsAddr,
                                                   .counts = resources.address(countsFilled),
                                                   .grid = resources.address(grid),
                                                   .counters = resources.address(countersReset)}));
#ifndef NDEBUG
            const std::array accesses{
                computeAccess(resources, countsFilled, kCountBytes, rojoRHI::Access::ShaderRead),
                computeAccess(resources, grid, kGridBytes, rojoRHI::Access::ShaderWrite),
                computeAccess(resources, countersReset, kCounterValueBytes,
                              rojoRHI::Access::ShaderWrite)};
            commands.declareAccess(accesses);
#endif
            commands.dispatch(1, 1, 1);
            tickets->counters = recordReadback(m_gpu, commands, *counterOwner, kComputeWrite,
                                               wholeBuffer(*counterOwner));
            if (captureLists) {
                tickets->grid = recordReadback(m_gpu, commands, *gridOwner, kComputeWrite,
                                               wholeBuffer(*gridOwner));
            }
        });
    const auto gridBuilt = nextVersion(grid);
    const auto countersWritten = nextVersion(countersReset);

    ComputePassDesc fill;
    fill.shaderBufferReads = {lights};
    fill.bufferReads = {gridBuilt};
    fill.bufferWrites = {indices};
    graph.addComputePass(
        "lmx.pass.light.fill", std::move(fill),
        [=, this, &commands](const PassResources& resources) {
            commands.setPipeline(m_pipelines[2].get());
            engine::registerFrameRoot(
                "LightClusterRoot",
                commands.pushRoot(LightClusterRoot{.params = paramsAddr,
                                                   .lights = resources.address(lights),
                                                   .grid = resources.address(gridBuilt),
                                                   .indices = resources.address(indices)}));
#ifndef NDEBUG
            const std::array accesses{
                computeAccess(resources, lights, lightBytes, rojoRHI::Access::ShaderRead),
                computeAccess(resources, gridBuilt, kGridBytes, rojoRHI::Access::ShaderRead),
                computeAccess(resources, indices, listBytes, rojoRHI::Access::ShaderWrite)};
            commands.declareAccess(accesses);
#endif
            commands.dispatch(groups, 1, 1);
            // The whole list: only the retired counters say how much of it is defined.
            if (captureLists) {
                tickets->indices = recordReadback(m_gpu, commands, *indexOwner, kComputeWrite,
                                                  wholeBuffer(*indexOwner));
            }
        });
    const auto listFilled = nextVersion(indices);

    graph.readbackBuffer(countersWritten);
    graph.exportBuffer(gridBuilt);
    graph.exportBuffer(listFilled);
    if (inputs.captureLists) {
        graph.readbackBuffer(gridBuilt);
        graph.readbackBuffer(listFilled);
    }
    slot.used = true;
    slot.shaderRead = inputs.shaderReadsOutputs;
    return {.grid = gridBuilt, .indices = listFilled, .declared = true};
}

//======================================================================================================================
lmx::Result<void> LightClusterStage::retireThrough(uint64_t completedFrame) {
    return m_readback->retireThrough(m_gpu, completedFrame);
}

//======================================================================================================================
std::vector<RetiredLightClusters> LightClusterStage::takeRetired() {
    return m_readback->takeRetired();
}

} // namespace lmx::render
