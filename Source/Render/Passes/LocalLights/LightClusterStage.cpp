//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterStage.cpp
/// @brief Allocates the paced froxel grid and declares deterministic count, scan and fill passes.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/LocalLights/LightClusterStage.h"
#include "Render/Common/GraphResources.h"
#include "Render/Common/StageSetup.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Scalar.h"
#include <rojoRHI/CaptureSchema.h>

#include <algorithm>
#include <string>

namespace lmx::render {
namespace {

// Mirrors Shaders/Passes/LocalLights/LightCluster.slang's LightClusterParams. Matrices travel as
// their four glm columns because a Slang float4x4 in a cbuffer leaves the column/row convention to
// codegen, while `column[c][r]` names exactly what `m[c][r]` names here; the slice table rides in
// float4s because cbuffer packing pads a scalar array element to sixteen bytes.
struct LightClusterKernelParams {
    std::array<glm::vec4, 4> view{};
    std::array<glm::vec4, 4> inverseProjection{};
    uint32_t rowCount = 0;
    uint32_t activeWidth = 0;
    uint32_t activeHeight = 0;
    uint32_t perClusterCap = 0;
    uint32_t globalCapacity = 0;
    uint32_t froxelCount = 0;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    std::array<glm::vec4, 7> sliceDepth{};
};
static_assert(sizeof(LightClusterKernelParams) == 272,
              "must match LightCluster.slang's LightClusterParams");

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

//======================================================================================================================
LightClusterKernelParams makeKernelParams(const LightClusterInputs& inputs, uint32_t perClusterCap,
                                          uint32_t globalCapacity) {
    LightClusterKernelParams params{.rowCount = inputs.rowCount,
                                    .activeWidth = inputs.activeWidth,
                                    .activeHeight = inputs.activeHeight,
                                    .perClusterCap = perClusterCap,
                                    .globalCapacity = globalCapacity,
                                    .froxelCount = kClusterCount};
    for (uint32_t column = 0; column < 4; ++column) {
        params.view[column] = inputs.view[int(column)];
        params.inverseProjection[column] = inputs.inverseJitteredProjection[int(column)];
    }
    for (uint32_t boundary = 0; boundary < kClusterSliceBoundaryCount; ++boundary) {
        params.sliceDepth[boundary / 4][int(boundary % 4)] = inputs.sliceDepth[boundary];
    }
    return params;
}

} // namespace

//======================================================================================================================
void LightClusterStage::registerLayoutsForCapture() {
    auto& schema = rojoRHI::debug::CaptureSchema::instance();
    schema.registerUniformStruct(
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
rojoRHI::Result<std::unique_ptr<LightClusterStage>>
LightClusterStage::create(rojoRHI::Device& device) {
    std::unique_ptr<LightClusterStage> self(new LightClusterStage(device));
    constexpr std::array names{"LightClusterCount", "LightClusterScan", "LightClusterFill"};
    // The scan is a single thread on purpose: 3,456 counts summed in froxel order is what
    // makes every range and every counter a function of the declarations alone.
    auto pipelines = createComputePipelines(device, names, "lmx.light.", [](uint32_t index) {
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
        auto allocate = [&](std::unique_ptr<rojoRHI::Buffer>& buffer, uint64_t bytes,
                            const std::string& name, bool readback) -> rojoRHI::Result<void> {
            auto result = device.createBuffer({.size = bytes,
                                               .storageRead = true,
                                               .storageWrite = true,
                                               .cpuReadback = readback,
                                               .label = name + suffix},
                                              nullptr);
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
    const auto params = makeKernelParams(inputs, m_perClusterCap, m_globalCapacity);
    m_pending.push_back({.frameNumber = inputs.frameNumber,
                         .captureLists = inputs.captureLists,
                         .grid = slot.grid.get(),
                         .indices = slot.indices.get(),
                         .counters = slot.counters.get()});

    auto import = [&](rojoRHI::Buffer& buffer, const char* name, rojoRHI::BufferUse use) {
        return slot.used ? graph.importBuffer(buffer, name, use) : graph.importBuffer(buffer, name);
    };
    const auto grid =
        import(*slot.grid, "lmx.light.grid",
               slot.shaderRead ? rojoRHI::BufferUse::ShaderRead : rojoRHI::BufferUse::StorageRead);
    const auto indices =
        import(*slot.indices, "lmx.light.indices",
               slot.shaderRead ? rojoRHI::BufferUse::ShaderRead : rojoRHI::BufferUse::StorageWrite);
    const auto counts = import(*slot.counts, "lmx.light.counts", rojoRHI::BufferUse::StorageRead);
    const auto counters =
        import(*slot.counters, "lmx.light.counters", rojoRHI::BufferUse::StorageWrite);

    // The counters the scan writes are complete on their own; the fill is what makes them a
    // reconciling total of bytes the GPU actually holds, so the zero fill is the stated start.
    CopyPassDesc reset;
    reset.bufferDestinations.push_back(counters);
    graph.addCopyPass("lmx.pass.light.reset", std::move(reset),
                      [&commands, counters](const PassResources& resources) {
                          auto& buffer = lmx::render::buffer(resources, counters);
                          commands.fillBuffer(buffer, 0,
                                              kLightClusterCounterWords * sizeof(uint32_t), 0);
                      });
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
