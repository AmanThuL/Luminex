//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveNative.cpp
/// @brief Declares native temporal accumulation and raw history commits.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Common/Dispatch.h"
#include "Render/Common/GraphResources.h"
#include "Render/Passes/Temporal/TemporalResolve.h"
#include "Render/Passes/Temporal/TemporalResolveInternal.h"
#include "Render/Passes/Temporal/TemporalRoots.h"

#include "Core/Diagnostics/Assert.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <utility>

namespace lmx::render {
using temporal_detail::TemporalResolveParams;

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
    const TemporalResolveParams params =
        buildTemporalResolveParams({.extents = inputs.extents,
                                    .camera = inputs.camera,
                                    .previousCamera = inputs.previousCamera,
                                    .historyValid = historyValid,
                                    .rejectionWanted = rejectionWanted,
                                    .reprojectedWanted = reprojectedWanted});

    declareReconstruction(graph, commands, inputs,
                          {.parameters = std::as_bytes(std::span{&params, 1}),
                           .parameterAlignment = alignof(TemporalResolveParams),
                           .paramsName = "TemporalResolveParams",
                           .pipeline = m_resolvePipeline.get(),
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
    // FrameData.h's frameData() alignment: the mirror type's own.
    const auto parameterAlignment = static_cast<uint32_t>(declaration.parameterAlignment);
    const rojoRHI::Pipeline pipeline = declaration.pipeline;
    const std::string_view paramsName = declaration.paramsName;

    const GraphTexture output = inputs.colorSlot;
    const GraphTexture rejection = outputs.rejection;
    const GraphTexture reprojected = outputs.reprojected;
    graph.addComputePass(
        declaration.passName, std::move(resolveDesc),
        [this, &commands, inputs, output, rejection, reprojected, rejectionWanted,
         reprojectedWanted, parameters, parameterSize, parameterAlignment, pipeline, paramsName,
         width, height](const PassResources& resources) {
            const auto paramsAddress = commands.uploadFrameData(
                std::span{parameters.data(), parameterSize}, parameterAlignment);
            engine::registerFrameRoot(paramsName, paramsAddress);
            commands.setPipeline(pipeline);
            // A diagnostic the frame does not declare still gets a real storage view in its root;
            // the kernel's writeDiagnostics bit is what keeps it from writing there.
            const auto storageViewOr = [&](bool wanted, GraphTexture handle) {
                return static_cast<uint32_t>(wanted ? resources.storageView(handle, 0)
                                                    : m_diagnosticFallback.storageView(0));
            };
            engine::registerFrameRoot(
                "TemporalResolveRoot",
                commands.pushRoot(TemporalResolveRoot{
                    .paramsAddr = paramsAddress,
                    .exposureAddr = resources.address(inputs.exposure),
                    .sceneColor = static_cast<uint32_t>(resources.view(inputs.sceneColor)),
                    .depth = static_cast<uint32_t>(resources.view(inputs.depth)),
                    .previousDepth = static_cast<uint32_t>(resources.view(inputs.previousDepth)),
                    .motion = static_cast<uint32_t>(resources.view(inputs.motion)),
                    .reactive = static_cast<uint32_t>(resources.view(inputs.reactive)),
                    .history = static_cast<uint32_t>(resources.view(inputs.history)),
                    .output = static_cast<uint32_t>(resources.storageView(output, 0)),
                    .rejection = storageViewOr(rejectionWanted, rejection),
                    .reprojected = storageViewOr(reprojectedWanted, reprojected),
                    // Clamping keeps reconstruction taps at the active rectangle edge on that edge.
                    .sampler = static_cast<uint32_t>(m_sampler.get())}));
#ifndef NDEBUG
            std::array<rojoRHI::ResourceAccess, 10> accesses{};
            uint32_t accessCount = 0;
            const auto read = [&](GraphTexture handle) {
                accesses[accessCount++] = {.texture = resources.texture(handle).texture,
                                           .range = {0, 1, 0, 1},
                                           .stage = rojoRHI::Stage::ComputeShader,
                                           .access = rojoRHI::Access::ShaderRead};
            };
            const auto write = [&](GraphTexture handle) {
                accesses[accessCount++] = {.texture = resources.texture(handle).texture,
                                           .range = {0, 1, 0, 1},
                                           .stage = rojoRHI::Stage::ComputeShader,
                                           .access = rojoRHI::Access::ShaderWrite};
            };
            read(inputs.sceneColor);
            read(inputs.depth);
            read(inputs.previousDepth);
            read(inputs.motion);
            read(inputs.reactive);
            read(inputs.history);
            write(output);
            if (rejectionWanted) {
                write(rejection);
            }
            if (reprojectedWanted) {
                write(reprojected);
            }
            accesses[accessCount++] = {.buffer = resources.buffer(inputs.exposure),
                                       .size = 2 * sizeof(float),
                                       .stage = rojoRHI::Stage::ComputeShader,
                                       .access = rojoRHI::Access::ShaderRead};
            commands.declareAccess(std::span{accesses}.first(accessCount));
#endif
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
            const rojoRHI::TextureRegion region{.extent = {width, height, 1}};
            commands.copyTexture(resources.texture(sceneColor).texture, region,
                                 resources.texture(history).texture, region);
        });
    // Nothing else in the frame consumes it -- the consumer is the next frame -- so the export is
    // what keeps the copy alive through culling.
    const GraphTexture committed = nextVersion(history);
    graph.exportTexture(committed);
    return committed;
}

} // namespace lmx::render
