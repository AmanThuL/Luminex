//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveUpscale.cpp
/// @brief Declares spatial commits and temporal upscaling at output extent.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/Dispatch.h"
#include "Render/Common/GraphResources.h"
#include "Render/Passes/Temporal/TemporalResolve.h"
#include "Render/Passes/Temporal/TemporalResolveInternal.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Scalar.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <span>
#include <utility>

namespace lmx::render {
using temporal_detail::SpatialUpscaleParams;
using temporal_detail::TemporalUpscaleParams;

namespace {

// SpatialUpscale.slang's slot map.
constexpr uint32_t kUpscaleSceneColorSlot = 0; // texture
constexpr uint32_t kUpscaleOutputSlot = 1;     // storage texture
constexpr uint32_t kUpscaleSamplerSlot = 0;    // sampler
constexpr uint32_t kUpscaleParamsSlot = 0;     // buffer

} // namespace

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
            auto& sceneTexture = lmx::render::texture(resources, sceneColor);
            auto& historyTexture = lmx::render::texture(resources, history);

            commands.bindComputePipeline(*m_spatialUpscalePipeline);
            commands.bindTexture(kUpscaleSceneColorSlot, sceneTexture);
            commands.bindStorageTexture(kUpscaleOutputSlot, historyTexture, {},
                                        rojoRHI::StorageAccess::Write);
            // The clamped sampler: a tap of the Catmull-Rom fetch that reaches the edge of the
            // active rectangle must answer with that edge rather than with the opposite one.
            commands.bindSampler(kUpscaleSamplerSlot, *m_sampler);
            commands.bindFrameData(kUpscaleParamsSlot, params);
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
                           .pipeline = *m_temporalUpscalePipeline,
                           .passName = "lmx.pass.temporal.upscale",
                           .width = params.outputWidth,
                           .height = params.outputHeight,
                           .rejectionWanted = rejectionWanted,
                           .reprojectedWanted = reprojectedWanted},
                          outputs);
}

} // namespace lmx::render
