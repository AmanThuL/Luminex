//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterStage.cpp
/// @brief Allocates the paced froxel grid and declares deterministic count, scan and fill passes.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/LocalLights/LightClusterStage.h"
#include "Render/Common/GraphResources.h"
#include "Render/Common/PacedSlots.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/LocalLights/LightClusterFrameData.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Scalar.h"
#include "Engine/Gpu/CaptureSchema.h"

#include <algorithm>
#include <string>

namespace lmx::render {
namespace {

// The shared buffer binding space; see CommandList::bindBuffer on why one index space serves the
// constant, read-only and storage bindings alike.
constexpr uint32_t kParamsSlot = 0;
constexpr uint32_t kLightsSlot = 1;
constexpr uint32_t kCountsSlot = 2;
constexpr uint32_t kGridSlot = 3;
constexpr uint32_t kIndicesSlot = 4;
constexpr uint32_t kCountersSlot = 5;

constexpr uint32_t kThreadsPerGroup = 256;
constexpr uint64_t kGridBytes = uint64_t{kClusterCount} * sizeof(ClusterRecord);
constexpr uint64_t kIndexBytes = uint64_t{kLightClusterIndexCapacity} * sizeof(uint32_t);
constexpr uint64_t kCountBytes = uint64_t{kClusterCount} * sizeof(uint32_t);

} // namespace

//======================================================================================================================
void LightClusterStage::registerLayoutsForCapture() {
    engine::registerUniformStruct(
        {.name = "LightClusterParams",
         .slot = kParamsSlot,
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
}

//======================================================================================================================
lmx::Result<std::unique_ptr<LightClusterStage>> LightClusterStage::create(engine::GpuContext& gpu) {
    rojoRHI::Device& device = gpu.device();
    std::unique_ptr<LightClusterStage> self(new LightClusterStage(gpu));
    constexpr std::array names{"LightClusterCount", "LightClusterScan", "LightClusterFill"};
    // The scan is a single thread on purpose: 3,456 counts summed in froxel order is what
    // makes every range and every counter a function of the declarations alone.
    auto pipelines = createComputePipelines(gpu, names, "lmx.light.", [](uint32_t index) {
        return index == 1 ? 1u : kThreadsPerGroup;
    });
    if (!pipelines) {
        return std::unexpected(pipelines.error());
    }
    for (uint32_t index = 0; index < names.size(); ++index) {
        self->m_libraries[index] = std::move(pipelines->libraries[index]);
        self->m_pipelines[index] = std::move(pipelines->pipelines[index]);
    }
    for (uint32_t slot = 0; slot < self->m_slots.size(); ++slot) {
        const std::string suffix = "." + std::to_string(slot);
        auto allocate = [&](engine::GpuBuffer& buffer, uint64_t bytes, const std::string& name,
                            bool readback) -> lmx::Result<void> {
            const std::string label = name + suffix;
            auto result = engine::GpuBuffer::create(device, {.size = bytes,
                                                             .storageRead = true,
                                                             .storageWrite = true,
                                                             .cpuReadback = readback,
                                                             .label = label});
            if (!result) {
                return std::unexpected(result.error());
            }
            buffer = std::move(*result);
            return {};
        };
        auto& owned = self->m_slots[slot];
        auto result = allocate(owned.grid, kGridBytes, "lmx.light.grid", true);
        if (!result) {
            return std::unexpected(result.error());
        }
        result = allocate(owned.indices, kIndexBytes, "lmx.light.indices", true);
        if (!result) {
            return std::unexpected(result.error());
        }
        result = allocate(owned.counts, kCountBytes, "lmx.light.counts", false);
        if (!result) {
            return std::unexpected(result.error());
        }
        result = allocate(owned.counters, kLightClusterCounterWords * sizeof(uint32_t),
                          "lmx.light.counters", true);
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

    LMX_ASSERT(std::ranges::none_of(m_pending,
                                    [&](const Pending& pending) {
                                        return pending.frameNumber % 3 == inputs.frameNumber % 3;
                                    }),
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
    m_pending.push_back({.frameNumber = inputs.frameNumber, .captureLists = inputs.captureLists});
    const auto tickets = m_pending.back().tickets;
    const bool captureLists = inputs.captureLists;
    const engine::GpuBuffer* const gridOwner = &slot.grid;
    const engine::GpuBuffer* const indexOwner = &slot.indices;
    const engine::GpuBuffer* const counterOwner = &slot.counters;

    const auto grid =
        importPaced(graph, slot.grid.rhi(), "lmx.light.grid",
                    slot.shaderRead ? BufferUse::ShaderRead : BufferUse::StorageRead, slot.used);
    const auto indices =
        importPaced(graph, slot.indices.rhi(), "lmx.light.indices",
                    slot.shaderRead ? BufferUse::ShaderRead : BufferUse::StorageWrite, slot.used);
    const auto counts = importPaced(graph, slot.counts.rhi(), "lmx.light.counts",
                                    BufferUse::StorageRead, slot.used);
    const auto counters = importPaced(graph, slot.counters.rhi(), "lmx.light.counters",
                                      BufferUse::StorageWrite, slot.used);

    // The counters the scan writes are complete on their own; the fill is what makes them a
    // reconciling total of bytes the GPU actually holds, so the zero fill is the stated start.
    declareZeroFill(graph, commands, "lmx.pass.light.reset", counters,
                    kLightClusterCounterWords * sizeof(uint32_t));
    const auto countersReset = nextVersion(counters);

    const uint32_t groups = divRoundUp(kClusterCount, kThreadsPerGroup);
    const auto lights = inputs.lights;
    ComputePassDesc count;
    count.shaderBufferReads = {lights};
    count.bufferWrites = {counts};
    graph.addComputePass("lmx.pass.light.count", std::move(count),
                         [=, this, &commands](const PassResources& resources) {
                             auto& lightRows = lmx::render::buffer(resources, lights);
                             auto& target = lmx::render::buffer(resources, counts);
                             commands.bindComputePipeline(*m_pipelines[0]);
                             commands.bindFrameData(kParamsSlot, params);
                             commands.bindBuffer(kLightsSlot, lightRows);
                             commands.bindStorageBuffer(kCountsSlot, target,
                                                        rojoRHI::StorageAccess::Write);
                             commands.dispatch(groups, 1, 1);
                         });
    const auto countsFilled = nextVersion(counts);

    ComputePassDesc scan;
    scan.bufferReads = {countsFilled};
    scan.bufferWrites = {grid, countersReset};
    graph.addComputePass(
        "lmx.pass.light.scan", std::move(scan),
        [=, this, &commands](const PassResources& resources) {
            auto& source = lmx::render::buffer(resources, countsFilled);
            auto& records = lmx::render::buffer(resources, grid);
            auto& totals = lmx::render::buffer(resources, countersReset);
            commands.bindComputePipeline(*m_pipelines[1]);
            commands.bindFrameData(kParamsSlot, params);
            commands.bindStorageBuffer(kCountsSlot, source, rojoRHI::StorageAccess::Read);
            commands.bindStorageBuffer(kGridSlot, records, rojoRHI::StorageAccess::Write);
            commands.bindStorageBuffer(kCountersSlot, totals, rojoRHI::StorageAccess::Write);
            commands.dispatch(1, 1, 1);
            tickets->counters = recordReadback(m_gpu, commands, *counterOwner,
                                               BufferUse::StorageWrite, wholeBuffer(*counterOwner));
            if (captureLists) {
                tickets->grid = recordReadback(m_gpu, commands, *gridOwner, BufferUse::StorageWrite,
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
            auto& lightRows = lmx::render::buffer(resources, lights);
            auto& records = lmx::render::buffer(resources, gridBuilt);
            auto& list = lmx::render::buffer(resources, indices);
            commands.bindComputePipeline(*m_pipelines[2]);
            commands.bindFrameData(kParamsSlot, params);
            commands.bindBuffer(kLightsSlot, lightRows);
            commands.bindStorageBuffer(kGridSlot, records, rojoRHI::StorageAccess::Read);
            commands.bindStorageBuffer(kIndicesSlot, list, rojoRHI::StorageAccess::Write);
            commands.dispatch(groups, 1, 1);
            // The whole list: only the retired counters say how much of it is defined.
            if (captureLists) {
                tickets->indices =
                    recordReadback(m_gpu, commands, *indexOwner, BufferUse::StorageWrite,
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

} // namespace lmx::render
