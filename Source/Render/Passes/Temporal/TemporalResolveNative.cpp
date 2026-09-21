//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveNative.cpp
/// @brief Declares native temporal accumulation and raw history commits.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Common/Dispatch.h"
#include "Render/Common/GraphResources.h"
#include "Render/Passes/Temporal/TemporalResolve.h"
#include "Render/Passes/Temporal/TemporalResolveInternal.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Scalar.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
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
    const bool historyValid = inputs.resetReason == HistoryResetReason::None;
    const TemporalResolveParams params{
        .width = width,
        .height = height,
        .historyValid = historyValid ? 1u : 0u,
        .writeDiagnostics = (rejectionWanted ? 1u : 0u) | (reprojectedWanted ? 2u : 0u),
        .inverseViewProjection = inputs.camera.inverseViewProjection,
        .previousViewProjection = inputs.previousCamera.viewProjection,
        .previousNearZ = inputs.previousCamera.nearZ};

    declareReconstruction(graph, commands, inputs,
                          {.parameters = std::as_bytes(std::span{&params, 1}),
                           .parameterAlignment = alignof(TemporalResolveParams),
                           .pipeline = *m_resolvePipeline,
                           .passName = "lmx.pass.temporal.resolve",
                           .width = width,
                           .height = height,
                           .rejectionWanted = rejectionWanted,
                           .reprojectedWanted = reprojectedWanted},
                          outputs);
}

//======================================================================================================================
void TemporalResolve::declareReconstruction(
    RenderGraph& graph, rojoRHI::CommandList& commands, const TemporalInputs& inputs,
    const temporal_detail::ReconstructionDeclaration& declaration,
    TemporalResolveOutputs& outputs) {
    const uint32_t width = declaration.width;
    const uint32_t height = declaration.height;
    const bool rejectionWanted = declaration.rejectionWanted;
    const bool reprojectedWanted = declaration.reprojectedWanted;
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

    // The caller's shader mirror is stack-owned; the deferred pass needs its own exact bytes.
    std::array<std::byte, sizeof(temporal_detail::TemporalUpscaleParams)> parameters{};
    const uint64_t parameterSize = declaration.parameters.size();
    LMX_ASSERT(parameterSize > 0 && parameterSize <= parameters.size(),
               "temporal reconstruction parameters must fit the owned callback block");
    std::memcpy(parameters.data(), declaration.parameters.data(), parameterSize);
    const uint64_t parameterAlignment =
        std::max(declaration.parameterAlignment, rojoRHI::kFrameDataAlignment);
    auto* pipeline = &declaration.pipeline;

    const GraphTexture output = inputs.colorSlot;
    const GraphTexture rejection = outputs.rejection;
    const GraphTexture reprojected = outputs.reprojected;
    graph.addComputePass(
        declaration.passName, std::move(resolveDesc),
        [this, &commands, inputs, output, rejection, reprojected, rejectionWanted,
         reprojectedWanted, parameters, parameterSize, parameterAlignment, pipeline, width,
         height](const PassResources& resources) {
            const auto bindRead = [&](uint32_t slot, GraphTexture handle) {
                auto& texture = lmx::render::texture(resources, handle);
                commands.bindTexture(slot, texture);
            };
            bindRead(kResolveSceneColorSlot, inputs.sceneColor);
            bindRead(kResolveDepthSlot, inputs.depth);
            bindRead(kResolvePreviousDepthSlot, inputs.previousDepth);
            bindRead(kResolveMotionSlot, inputs.motion);
            bindRead(kResolveReactiveSlot, inputs.reactive);
            bindRead(kResolveHistorySlot, inputs.history);

            auto& target = lmx::render::texture(resources, output);
            auto& exposure = lmx::render::buffer(resources, inputs.exposure);

            commands.bindComputePipeline(*pipeline);
            commands.bindStorageTexture(kResolveOutputSlot, target, {},
                                        rojoRHI::StorageAccess::Write);
            // The argument table entry has to hold a writable texture even where the kernel's
            // corresponding writeDiagnostics bit makes it write nothing.
            if (rejectionWanted) {
                auto& texture = lmx::render::texture(resources, rejection);
                commands.bindStorageTexture(kResolveRejectionSlot, texture, {},
                                            rojoRHI::StorageAccess::Write);
            } else {
                commands.bindStorageTexture(kResolveRejectionSlot, *m_diagnosticFallback, {},
                                            rojoRHI::StorageAccess::Write);
            }
            if (reprojectedWanted) {
                auto& texture = lmx::render::texture(resources, reprojected);
                commands.bindStorageTexture(kResolveReprojectedSlot, texture, {},
                                            rojoRHI::StorageAccess::Write);
            } else {
                commands.bindStorageTexture(kResolveReprojectedSlot, *m_diagnosticFallback, {},
                                            rojoRHI::StorageAccess::Write);
            }
            commands.bindStorageBuffer(kResolveExposureSlot, exposure,
                                       rojoRHI::StorageAccess::Read);
            // Clamping keeps reconstruction taps at the active rectangle edge on that edge.
            commands.bindSampler(kResolveSamplerSlot, *m_sampler);
            commands.bindFrameData(kResolveParamsSlot, parameters.data(), parameterSize,
                                   parameterAlignment);
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
            auto& sceneTexture = lmx::render::texture(resources, sceneColor);
            auto& historyTexture = lmx::render::texture(resources, history);

            const rojoRHI::TextureCopyRegion region{.width = width, .height = height};
            commands.copyTexture(sceneTexture, region, historyTexture, region);
        });
    // Nothing else in the frame consumes it -- the consumer is the next frame -- so the export is
    // what keeps the copy alive through culling.
    const GraphTexture committed = nextVersion(history);
    graph.exportTexture(committed);
    return committed;
}

} // namespace lmx::render
