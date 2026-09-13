//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveUpscale.cpp
/// @brief Declares spatial commits and temporal upscaling at output extent.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/TemporalResolve.h"
#include "Render/TemporalResolveInternal.h"

#include "Core/Assert.h"
#include "Core/Math.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <utility>

namespace lmx::render {
using temporal_detail::kComputeThreadsPerGroup2D;
using temporal_detail::kResolveDepthSlot;
using temporal_detail::kResolveExposureSlot;
using temporal_detail::kResolveHistorySlot;
using temporal_detail::kResolveMotionSlot;
using temporal_detail::kResolveOutputSlot;
using temporal_detail::kResolveParamsSlot;
using temporal_detail::kResolvePreviousDepthSlot;
using temporal_detail::kResolveReactiveSlot;
using temporal_detail::kResolveRejectionSlot;
using temporal_detail::kResolveReprojectedSlot;
using temporal_detail::kResolveSamplerSlot;
using temporal_detail::kResolveSceneColorSlot;
using temporal_detail::SpatialUpscaleParams;
using temporal_detail::spatialUpscaleParams;
using temporal_detail::TemporalUpscaleParams;
using temporal_detail::temporalUpscaleParams;

namespace {

// SpatialUpscale.slang's slot map.
constexpr uint32_t kUpscaleSceneColorSlot = 0; // texture
constexpr uint32_t kUpscaleOutputSlot = 1;     // storage texture
constexpr uint32_t kUpscaleSamplerSlot = 0;    // sampler
constexpr uint32_t kUpscaleParamsSlot = 0;     // buffer

} // namespace

namespace temporal_detail {

//======================================================================================================================
// Shaders/SpatialUpscale.slang's block. The allocated extent is the output one: every render-extent
// target is allocated at capacity and used through an origin-anchored active rectangle, so a UV
// over one of them is taken over the output extent whatever the frame rasterised at.
SpatialUpscaleParams spatialUpscaleParams(const TemporalInputs& inputs) {
    return SpatialUpscaleParams{.renderWidth = inputs.extents.renderWidth,
                                .renderHeight = inputs.extents.renderHeight,
                                .outputWidth = inputs.extents.outputWidth,
                                .outputHeight = inputs.extents.outputHeight,
                                .allocatedWidth = inputs.extents.outputWidth,
                                .allocatedHeight = inputs.extents.outputHeight,
                                .jitterOffset = jitterTexelOffset(inputs.camera.jitterPixels)};
}

//======================================================================================================================
// Shaders/TemporalUpscale.slang's block. The render extent is what the kernel reads its inputs
// within, the output extent what it dispatches over, and the allocated extent the output one on
// spatialUpscaleParams()' terms. The previous render extent is carried separately because the
// previous depth slot is addressed at the extent it was rendered at, which a scale change moves.
TemporalUpscaleParams temporalUpscaleParams(const TemporalInputs& inputs, bool historyValid,
                                            uint32_t writeDiagnostics) {
    return TemporalUpscaleParams{.width = inputs.extents.renderWidth,
                                 .height = inputs.extents.renderHeight,
                                 .historyValid = historyValid ? 1u : 0u,
                                 .writeDiagnostics = writeDiagnostics,
                                 .inverseViewProjection = inputs.camera.inverseViewProjection,
                                 .previousViewProjection = inputs.previousCamera.viewProjection,
                                 .previousNearZ = inputs.previousCamera.nearZ,
                                 .outputWidth = inputs.extents.outputWidth,
                                 .outputHeight = inputs.extents.outputHeight,
                                 .allocatedWidth = inputs.extents.outputWidth,
                                 .allocatedHeight = inputs.extents.outputHeight,
                                 .previousRenderWidth = inputs.previousExtents.renderWidth,
                                 .previousRenderHeight = inputs.previousExtents.renderHeight,
                                 .jitterOffset = jitterTexelOffset(inputs.camera.jitterPixels)};
}

} // namespace temporal_detail

//======================================================================================================================
GraphTexture TemporalResolve::declareSpatialCommit(RenderGraph& graph, rhi::CommandList& commands,
                                                   const TemporalInputs& inputs) {
    const SpatialUpscaleParams params = spatialUpscaleParams(inputs);

    ComputePassDesc commitDesc;
    commitDesc.shaderTextureReads.push_back(inputs.sceneColor);
    commitDesc.textureWrites.push_back(inputs.colorSlot);
    const GraphTexture sceneColor = inputs.sceneColor;
    const GraphTexture history = inputs.colorSlot;
    graph.addComputePass(
        "lmx.pass.temporal.commitUpscaled", std::move(commitDesc),
        [this, &commands, sceneColor, history, params](const PassResources& resources) {
            const GraphResult<rhi::Texture*> sceneTexture = resources.texture(sceneColor);
            LMX_ASSERT(sceneTexture.has_value(), sceneTexture.error().message);
            const GraphResult<rhi::Texture*> historyTexture = resources.texture(history);
            LMX_ASSERT(historyTexture.has_value(), historyTexture.error().message);

            commands.bindComputePipeline(*m_spatialUpscalePipeline);
            commands.bindTexture(kUpscaleSceneColorSlot, **sceneTexture);
            commands.bindStorageTexture(kUpscaleOutputSlot, **historyTexture, {},
                                        rhi::StorageAccess::Write);
            // The clamped sampler: a tap of the Catmull-Rom fetch that reaches the edge of the
            // active rectangle must answer with that edge rather than with the opposite one.
            commands.bindSampler(kUpscaleSamplerSlot, *m_sampler);
            commands.bindFrameData(kUpscaleParamsSlot, params);
            commands.dispatch(divRoundUp(params.outputWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(params.outputHeight, kComputeThreadsPerGroup2D), 1);
        });
    // On declareHistoryCommit()'s terms: the consumer is the next frame, so the export is what
    // keeps the pass alive through culling.
    const GraphTexture committed = nextVersion(history);
    graph.exportTexture(committed);
    return committed;
}

//======================================================================================================================
void TemporalResolve::declareUpscale(RenderGraph& graph, rhi::CommandList& commands,
                                     const TemporalInputs& inputs, bool rejectionWanted,
                                     bool reprojectedWanted, TemporalResolveOutputs& outputs) {
    // The output extent, at every scale: the history the kernel accumulates over and the picture
    // it produces are both that size, so the diagnostics beside them are too -- which also keeps
    // their descriptors independent of the render scale.
    const uint32_t width = inputs.extents.outputWidth;
    const uint32_t height = inputs.extents.outputHeight;
    if (rejectionWanted) {
        outputs.rejection = graph.createTexture({.width = width,
                                                 .height = height,
                                                 .format = rhi::Format::RGBA8Unorm,
                                                 .sampled = true,
                                                 .storageWrite = true},
                                                "lmx.render.temporalRejection");
    }
    if (reprojectedWanted) {
        outputs.reprojected = graph.createTexture({.width = width,
                                                   .height = height,
                                                   .format = rhi::Format::RGBA16Float,
                                                   .sampled = true,
                                                   .storageWrite = true},
                                                  "lmx.render.temporalReprojected");
    }

    // The resolve's declaration exactly: the accumulating kernel reads all six inputs and writes
    // the same three outputs, so one pass shape serves both extents.
    ComputePassDesc upscaleDesc;
    upscaleDesc.shaderTextureReads.push_back(inputs.sceneColor);
    upscaleDesc.shaderTextureReads.push_back(inputs.depth);
    upscaleDesc.shaderTextureReads.push_back(inputs.previousDepth);
    upscaleDesc.shaderTextureReads.push_back(inputs.motion);
    upscaleDesc.shaderTextureReads.push_back(inputs.reactive);
    // Declared on a reset frame too, on declareResolve()'s terms: the kernel is told through
    // `historyValid` not to read it.
    upscaleDesc.shaderTextureReads.push_back(inputs.history);
    upscaleDesc.bufferReads.push_back(inputs.exposure);
    upscaleDesc.textureWrites.push_back(inputs.colorSlot);
    if (rejectionWanted) {
        upscaleDesc.textureWrites.push_back(outputs.rejection);
    }
    if (reprojectedWanted) {
        upscaleDesc.textureWrites.push_back(outputs.reprojected);
    }

    const bool historyValid = inputs.resetReason == HistoryResetReason::None;
    const TemporalUpscaleParams params = temporalUpscaleParams(
        inputs, historyValid, (rejectionWanted ? 1u : 0u) | (reprojectedWanted ? 2u : 0u));

    const GraphTexture output = inputs.colorSlot;
    const GraphTexture rejection = outputs.rejection;
    const GraphTexture reprojected = outputs.reprojected;
    graph.addComputePass(
        "lmx.pass.temporal.upscale", std::move(upscaleDesc),
        [this, &commands, inputs, output, rejection, reprojected, rejectionWanted,
         reprojectedWanted, params](const PassResources& resources) {
            const auto bindRead = [&](uint32_t slot, GraphTexture handle) {
                const GraphResult<rhi::Texture*> texture = resources.texture(handle);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindTexture(slot, **texture);
            };
            bindRead(kResolveSceneColorSlot, inputs.sceneColor);
            bindRead(kResolveDepthSlot, inputs.depth);
            bindRead(kResolvePreviousDepthSlot, inputs.previousDepth);
            bindRead(kResolveMotionSlot, inputs.motion);
            bindRead(kResolveReactiveSlot, inputs.reactive);
            bindRead(kResolveHistorySlot, inputs.history);

            const GraphResult<rhi::Texture*> target = resources.texture(output);
            LMX_ASSERT(target.has_value(), target.error().message);
            const GraphResult<rhi::Buffer*> exposure = resources.buffer(inputs.exposure);
            LMX_ASSERT(exposure.has_value(), exposure.error().message);

            commands.bindComputePipeline(*m_temporalUpscalePipeline);
            commands.bindStorageTexture(kResolveOutputSlot, **target, {},
                                        rhi::StorageAccess::Write);
            // declareResolve()'s rule: the argument table entry has to hold a writable texture even
            // where the kernel's corresponding writeDiagnostics bit makes it write nothing.
            if (rejectionWanted) {
                const GraphResult<rhi::Texture*> texture = resources.texture(rejection);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindStorageTexture(kResolveRejectionSlot, **texture, {},
                                            rhi::StorageAccess::Write);
            } else {
                commands.bindStorageTexture(kResolveRejectionSlot, *m_diagnosticFallback, {},
                                            rhi::StorageAccess::Write);
            }
            if (reprojectedWanted) {
                const GraphResult<rhi::Texture*> texture = resources.texture(reprojected);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindStorageTexture(kResolveReprojectedSlot, **texture, {},
                                            rhi::StorageAccess::Write);
            } else {
                commands.bindStorageTexture(kResolveReprojectedSlot, *m_diagnosticFallback, {},
                                            rhi::StorageAccess::Write);
            }
            commands.bindStorageBuffer(kResolveExposureSlot, **exposure, rhi::StorageAccess::Read);
            // The clamped sampler: a tap of either Catmull-Rom fetch that reaches the edge of the
            // active rectangle must answer with that edge rather than with the opposite one.
            commands.bindSampler(kResolveSamplerSlot, *m_sampler);
            commands.bindFrameData(kResolveParamsSlot, params);
            commands.dispatch(divRoundUp(params.outputWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(params.outputHeight, kComputeThreadsPerGroup2D), 1);
        });

    if (rejectionWanted) {
        outputs.rejection = nextVersion(outputs.rejection);
    }
    if (reprojectedWanted) {
        outputs.reprojected = nextVersion(outputs.reprojected);
    }
    // declareResolve()'s export, for its reason: the accumulation's real consumer is the next
    // frame, so nothing in this frame keeps the slot alive on its own account.
    outputs.resolved = nextVersion(inputs.colorSlot);
    graph.exportTexture(outputs.resolved);
}

} // namespace lmx::render
