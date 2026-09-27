//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementSample.cpp
/// @brief Copies shared measurement declarations into owned CPU samples.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Performance/MeasurementSample.h"

namespace lmx::app {

//======================================================================================================================
uint64_t measurementTableBytes(const engine::SceneTableStats& stats) {
    return 3 * (uint64_t{stats.instanceCapacity} * sizeof(engine::InstanceRow) +
                uint64_t{stats.meshCapacity} * sizeof(engine::MeshRow) +
                uint64_t{stats.materialCapacity} * sizeof(engine::MaterialRow) +
                uint64_t{stats.lightCapacity} * sizeof(engine::LightRow));
}

//======================================================================================================================
MeasurementCpuSample measurementCpuSample(uint32_t sequenceFrame, double waitMs, double encodeMs,
                                          const render::VisibilityStatus& visibility,
                                          const engine::SceneTableStats& tables,
                                          const render::CompiledFrameRecord& record, bool hasSky,
                                          const render::TemporalStatus& temporal,
                                          const render::LightingStatus& lighting) {
    MeasurementCpuSample sample{
        .frameId = record.frameId,
        .sequenceFrame = sequenceFrame,
        .classifyMs = visibility.classifyMs,
        .prepareMs = visibility.prepareMs,
        .encodeMs = encodeMs,
        .slotWaitMs = waitMs,
        .renderWidth = temporal.extents.renderWidth,
        .renderHeight = temporal.extents.renderHeight,
        .outputWidth = temporal.extents.outputWidth,
        .outputHeight = temporal.extents.outputHeight,
        .effectiveScale = temporal.renderScale,
        .effectiveReconstruction = static_cast<uint32_t>(temporal.reconstruction),
        .vendorFallback = static_cast<uint32_t>(temporal.vendorFallback),
        .candidates = visibility.sceneCounters.candidates,
        .visible = static_cast<uint32_t>(visibility.scene.visibleItems.size()),
        .rejected = visibility.scene.rejected,
        .sceneCommands = visibility.submission.sceneCommands + (hasSky ? 1u : 0u),
        .shadowCommands = visibility.submission.shadowCommands,
        .tableBytes = measurementTableBytes(tables),
        .reservedListBytes = visibility.submission.listBytes,
        .listBytes = visibility.submission.listBytes,
        .argumentBytes = visibility.submission.argumentBytes,
        .allocatedListBytes = visibility.submission.allocatedListBytes,
        .allocatedArgumentBytes = visibility.submission.allocatedArgumentBytes,
        .candidateBytes = visibility.submission.candidateBytes,
        .runBytes = visibility.submission.runBytes,
        .chunkBytes = visibility.submission.chunkBytes,
        .stateBytes = visibility.submission.stateBytes,
        .counterBytes = visibility.submission.counterBytes,
        .classifyMode = visibility.classifyMode,
        .sceneCounters = visibility.sceneCounters,
        .shadowCounters = visibility.shadowCounters,
        .transientBytes = record.debug.memory.highWater,
        .lighting = lighting};
    for (uint32_t index : record.debug.schedule.passes)
        sample.expectedPasses.push_back(record.debug.passes[index].label);
    return sample;
}

} // namespace lmx::app
