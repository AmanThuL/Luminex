//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveNative.cpp
/// @brief Declares native temporal accumulation and raw history commits.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/Dispatch.h"
#include "Render/Passes/Temporal/TemporalResolve.h"
#include "Render/Passes/Temporal/TemporalResolveInternal.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Scalar.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <utility>

namespace lmx::render {
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

namespace {

// Mirrors Shaders/Passes/Temporal/TemporalResolve.slang's TemporalResolveParams.
struct TemporalResolveParams {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t historyValid = 0;
    uint32_t writeDiagnostics = 0; // Bit 0: rejection; bit 1: reprojected history.
    glm::mat4 inverseViewProjection{1.0f};
    glm::mat4 previousViewProjection{1.0f};
    float previousNearZ = 0.0f;
    float pad[3] = {0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(TemporalResolveParams) == 160,
              "must match TemporalResolve.slang's TemporalResolveParams");

} // namespace

//======================================================================================================================
void TemporalResolve::declareResolve(RenderGraph& graph, rojoRHI::CommandList& commands,
                                     const TemporalInputs& inputs, bool rejectionWanted,
                                     bool reprojectedWanted, TemporalResolveOutputs& outputs) {
    // Only a frame whose render extent is its output extent reaches here, so the two are the same
    // number; naming the output one is what states which of them the pass and its diagnostics are
    // sized by.
    const uint32_t width = inputs.extents.outputWidth;
    const uint32_t height = inputs.extents.outputHeight;
    if (rejectionWanted) {
        outputs.rejection = graph.createTexture({.width = width,
                                                 .height = height,
                                                 .format = rojoRHI::Format::RGBA8Unorm,
                                                 .sampled = true,
                                                 .storageWrite = true},
                                                "lmx.render.temporalRejection");
    }
    if (reprojectedWanted) {
        outputs.reprojected = graph.createTexture({.width = width,
                                                   .height = height,
                                                   .format = rojoRHI::Format::RGBA16Float,
                                                   .sampled = true,
                                                   .storageWrite = true},
                                                  "lmx.render.temporalReprojected");
    }

    ComputePassDesc resolveDesc;
    resolveDesc.shaderTextureReads.push_back(inputs.sceneColor);
    resolveDesc.shaderTextureReads.push_back(inputs.depth);
    resolveDesc.shaderTextureReads.push_back(inputs.previousDepth);
    resolveDesc.shaderTextureReads.push_back(inputs.motion);
    resolveDesc.shaderTextureReads.push_back(inputs.reactive);
    // Declared on a reset frame too: the kernel is told through `historyValid` not to read it,
    // and declaring the same set either way is what keeps one pass shape for both.
    resolveDesc.shaderTextureReads.push_back(inputs.history);
    resolveDesc.bufferReads.push_back(inputs.exposure);
    resolveDesc.textureWrites.push_back(inputs.colorSlot);
    if (rejectionWanted) {
        resolveDesc.textureWrites.push_back(outputs.rejection);
    }
    if (reprojectedWanted) {
        resolveDesc.textureWrites.push_back(outputs.reprojected);
    }

    const bool historyValid = inputs.resetReason == HistoryResetReason::None;
    const TemporalResolveParams params{
        .width = width,
        .height = height,
        .historyValid = historyValid ? 1u : 0u,
        .writeDiagnostics = (rejectionWanted ? 1u : 0u) | (reprojectedWanted ? 2u : 0u),
        .inverseViewProjection = inputs.camera.inverseViewProjection,
        .previousViewProjection = inputs.previousCamera.viewProjection,
        .previousNearZ = inputs.previousCamera.nearZ};

    const GraphTexture output = inputs.colorSlot;
    const GraphTexture rejection = outputs.rejection;
    const GraphTexture reprojected = outputs.reprojected;
    graph.addComputePass(
        "lmx.pass.temporal.resolve", std::move(resolveDesc),
        [this, &commands, inputs, output, rejection, reprojected, rejectionWanted,
         reprojectedWanted, params, width, height](const PassResources& resources) {
            const auto bindRead = [&](uint32_t slot, GraphTexture handle) {
                const GraphResult<rojoRHI::Texture*> texture = resources.texture(handle);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindTexture(slot, **texture);
            };
            bindRead(kResolveSceneColorSlot, inputs.sceneColor);
            bindRead(kResolveDepthSlot, inputs.depth);
            bindRead(kResolvePreviousDepthSlot, inputs.previousDepth);
            bindRead(kResolveMotionSlot, inputs.motion);
            bindRead(kResolveReactiveSlot, inputs.reactive);
            bindRead(kResolveHistorySlot, inputs.history);

            const GraphResult<rojoRHI::Texture*> target = resources.texture(output);
            LMX_ASSERT(target.has_value(), target.error().message);
            const GraphResult<rojoRHI::Buffer*> exposure = resources.buffer(inputs.exposure);
            LMX_ASSERT(exposure.has_value(), exposure.error().message);

            commands.bindComputePipeline(*m_resolvePipeline);
            commands.bindStorageTexture(kResolveOutputSlot, **target, {},
                                        rojoRHI::StorageAccess::Write);
            // The argument table entry has to hold a writable texture even where the kernel's
            // corresponding writeDiagnostics bit makes it write nothing.
            if (rejectionWanted) {
                const GraphResult<rojoRHI::Texture*> texture = resources.texture(rejection);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindStorageTexture(kResolveRejectionSlot, **texture, {},
                                            rojoRHI::StorageAccess::Write);
            } else {
                commands.bindStorageTexture(kResolveRejectionSlot, *m_diagnosticFallback, {},
                                            rojoRHI::StorageAccess::Write);
            }
            if (reprojectedWanted) {
                const GraphResult<rojoRHI::Texture*> texture = resources.texture(reprojected);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindStorageTexture(kResolveReprojectedSlot, **texture, {},
                                            rojoRHI::StorageAccess::Write);
            } else {
                commands.bindStorageTexture(kResolveReprojectedSlot, *m_diagnosticFallback, {},
                                            rojoRHI::StorageAccess::Write);
            }
            commands.bindStorageBuffer(kResolveExposureSlot, **exposure,
                                       rojoRHI::StorageAccess::Read);
            commands.bindSampler(kResolveSamplerSlot, *m_sampler);
            commands.bindFrameData(kResolveParamsSlot, params);
            const auto groups = dispatchGroups2D(width, height);
            commands.dispatch(groups[0], groups[1], 1);
        });

    if (rejectionWanted) {
        outputs.rejection = nextVersion(outputs.rejection);
    }
    if (reprojectedWanted) {
        outputs.reprojected = nextVersion(outputs.reprojected);
    }
    // Nothing in this frame keeps the slot alive on its own account -- bloom and display read it,
    // but the accumulation's real consumer is the next frame -- so the version is exported the way
    // M6.1's commit was.
    outputs.resolved = nextVersion(inputs.colorSlot);
    graph.exportTexture(outputs.resolved);
}

//======================================================================================================================
GraphTexture TemporalResolve::declareHistoryCommit(RenderGraph& graph,
                                                   rojoRHI::CommandList& commands,
                                                   const TemporalInputs& inputs) {
    // Declared only where the two extents agree, so the copy covers the whole slot; the caller
    // routes an upscaled frame to declareSpatialCommit() instead.
    const uint32_t width = inputs.extents.renderWidth;
    const uint32_t height = inputs.extents.renderHeight;

    CopyPassDesc commitDesc;
    commitDesc.textureSources.push_back(inputs.sceneColor);
    commitDesc.textureDestinations.push_back(inputs.colorSlot);
    const GraphTexture sceneColor = inputs.sceneColor;
    const GraphTexture history = inputs.colorSlot;
    graph.addCopyPass(
        "lmx.pass.temporal.commitHistory", std::move(commitDesc),
        [&commands, sceneColor, history, width, height](const PassResources& resources) {
            const GraphResult<rojoRHI::Texture*> sceneTexture = resources.texture(sceneColor);
            LMX_ASSERT(sceneTexture.has_value(), sceneTexture.error().message);
            const GraphResult<rojoRHI::Texture*> historyTexture = resources.texture(history);
            LMX_ASSERT(historyTexture.has_value(), historyTexture.error().message);

            const rojoRHI::TextureCopyRegion region{.width = width, .height = height};
            commands.copyTexture(**sceneTexture, region, **historyTexture, region);
        });
    // Nothing else in the frame consumes it -- the consumer is the next frame -- so the export is
    // what keeps the copy alive through culling.
    const GraphTexture committed = nextVersion(history);
    graph.exportTexture(committed);
    return committed;
}

} // namespace lmx::render
