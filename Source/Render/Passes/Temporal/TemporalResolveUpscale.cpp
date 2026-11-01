//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveUpscale.cpp
/// @brief Declares spatial commits and temporal upscaling at output extent.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Common/Dispatch.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/GraphResources.h"
#include "Render/Passes/Temporal/TemporalResolve.h"
#include "Render/Passes/Temporal/TemporalResolveInternal.h"
#include "Render/Passes/Temporal/TemporalRoots.h"

#include <array>
#include <span>
#include <utility>

namespace lmx::render {
using temporal_detail::SpatialUpscaleParams;
using temporal_detail::TemporalUpscaleParams;

//======================================================================================================================
GraphTexture TemporalResolve::declareSpatialCommit(RenderGraph& graph,
                                                   rojoRHI::CommandList& commands,
                                                   const TemporalInputs& inputs) {
    const SpatialUpscaleParams params =
        buildSpatialUpscaleParams(inputs.extents, inputs.camera.jitterPixels);

    ComputePassDesc commitDesc;
    commitDesc.shaderTextureReads.push_back(inputs.sceneColor);
    commitDesc.textureWrites.push_back(inputs.colorSlot);
    const GraphTexture sceneColor = inputs.sceneColor;
    const GraphTexture history = inputs.colorSlot;
    graph.addComputePass(
        "lmx.pass.temporal.commitUpscaled", std::move(commitDesc),
        [this, &commands, sceneColor, history, params](const PassResources& resources) {
            const auto paramsAddress = frameData(commands, params);
            engine::registerFrameRoot("SpatialUpscaleParams", paramsAddress);
            commands.setPipeline(m_spatialUpscalePipeline.get());
            engine::registerFrameRoot(
                "SpatialUpscaleRoot",
                commands.pushRoot(SpatialUpscaleRoot{
                    .paramsAddr = paramsAddress,
                    .sceneColor = static_cast<uint32_t>(resources.view(sceneColor)),
                    .output = static_cast<uint32_t>(resources.storageView(history, 0)),
                    // The clamped sampler: a tap of the Catmull-Rom fetch that reaches the edge
                    // of the active rectangle must answer with that edge rather than the opposite.
                    .sampler = static_cast<uint32_t>(m_sampler.get())}));
#ifndef NDEBUG
            const std::array<rojoRHI::ResourceAccess, 2> accesses{
                {{.texture = resources.texture(sceneColor).texture,
                  .range = {0, 1, 0, 1},
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderRead},
                 {.texture = resources.texture(history).texture,
                  .range = {0, 1, 0, 1},
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderWrite}}};
            commands.declareAccess(accesses);
#endif
            const auto groups = dispatchGroups2D(params.outputWidth, params.outputHeight);
            commands.dispatch(groups[0], groups[1], 1);
        });
    // On declareHistoryCommit()'s terms: the consumer is the next frame, so the export is what
    // keeps the pass alive through culling.
    const GraphTexture committed = nextVersion(history);
    graph.exportTexture(committed);
    return committed;
}

//======================================================================================================================
void TemporalResolve::declareUpscale(RenderGraph& graph, rojoRHI::CommandList& commands,
                                     const TemporalInputs& inputs, bool rejectionWanted,
                                     bool reprojectedWanted, TemporalResolveOutputs& outputs) {
    // The output extent, at every scale: the history the kernel accumulates over and the picture
    // it produces are both that size, so the diagnostics beside them are too -- which also keeps
    // their descriptors independent of the render scale.
    const bool historyValid = inputs.resetReason == HistoryResetReason::None;
    const TemporalUpscaleParams params =
        buildTemporalUpscaleParams({.extents = inputs.extents,
                                    .previousExtents = inputs.previousExtents,
                                    .camera = inputs.camera,
                                    .previousCamera = inputs.previousCamera,
                                    .historyValid = historyValid,
                                    .rejectionWanted = rejectionWanted,
                                    .reprojectedWanted = reprojectedWanted});

    declareReconstruction(graph, commands, inputs,
                          {.parameters = std::as_bytes(std::span{&params, 1}),
                           .parameterAlignment = alignof(TemporalUpscaleParams),
                           .pipeline = m_temporalUpscalePipeline.get(),
                           .passName = "lmx.pass.temporal.upscale",
                           .width = params.outputWidth,
                           .height = params.outputHeight,
                           .rejectionWanted = rejectionWanted,
                           .reprojectedWanted = reprojectedWanted},
                          outputs);
}

} // namespace lmx::render
