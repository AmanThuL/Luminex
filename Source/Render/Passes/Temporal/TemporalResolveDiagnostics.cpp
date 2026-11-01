//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveDiagnostics.cpp
/// @brief Declares reprojection diagnostics and temporal debug display.
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
using temporal_detail::viewReadsRejection;
using temporal_detail::viewReadsReprojected;

namespace temporal_detail {

//======================================================================================================================
// Which of the resolve's two diagnostics a view consumes. A view reading neither leaves both
// undeclared, which is what keeps the shipped frame from allocating or writing either.
bool viewReadsRejection(TemporalDebugView view) {
    return view == TemporalDebugView::RejectionMask || view == TemporalDebugView::BlendWeight;
}

//======================================================================================================================
bool viewReadsReprojected(TemporalDebugView view) {
    return view == TemporalDebugView::ReprojectedHistory;
}

} // namespace temporal_detail

//======================================================================================================================
GraphTexture TemporalResolve::declareReprojection(RenderGraph& graph,
                                                  rojoRHI::CommandList& commands,
                                                  const TemporalInputs& inputs) {
    // The output extent, at every scale: the history and the display the view draws over are both
    // that size, and the current colour is resampled out of the active rectangle to meet them. It
    // also keeps the transient's descriptor independent of the render scale, so the transient pool
    // sees one footprint across every scale.
    const uint32_t width = inputs.extents.outputWidth;
    const uint32_t height = inputs.extents.outputHeight;
    const GraphTexture diagnostic = graph.createTexture({.width = width,
                                                         .height = height,
                                                         .format = rojoRHI::Format::RGBA16Float,
                                                         .sampled = true,
                                                         .storageWrite = true},
                                                        "lmx.render.temporalDiagnostic");

    ComputePassDesc reprojectDesc;
    reprojectDesc.shaderTextureReads.push_back(inputs.history);
    reprojectDesc.shaderTextureReads.push_back(inputs.sceneColor);
    reprojectDesc.shaderTextureReads.push_back(inputs.motion);
    reprojectDesc.textureWrites.push_back(diagnostic);
    graph.addComputePass(
        "lmx.pass.temporal.reproject", std::move(reprojectDesc),
        [this, &commands, inputs, diagnostic,
         params = buildTemporalReprojectParams(inputs.extents, inputs.camera.jitterPixels)](
            const PassResources& resources) {
            const auto paramsAddress = frameData(commands, params);
            engine::registerFrameRoot("TemporalReprojectParams", paramsAddress);
            commands.setPipeline(m_reprojectPipeline.get());
            engine::registerFrameRoot(
                "TemporalReprojectRoot",
                commands.pushRoot(TemporalReprojectRoot{
                    .paramsAddr = paramsAddress,
                    .history = static_cast<uint32_t>(resources.view(inputs.history)),
                    .sceneColor = static_cast<uint32_t>(resources.view(inputs.sceneColor)),
                    .motion = static_cast<uint32_t>(resources.view(inputs.motion)),
                    .diagnostic = static_cast<uint32_t>(resources.storageView(diagnostic, 0)),
                    .sampler = static_cast<uint32_t>(m_sampler.get())}));
#ifndef NDEBUG
            const auto read = [&](GraphTexture handle) {
                return rojoRHI::ResourceAccess{.texture = resources.texture(handle).texture,
                                               .range = {0, 1, 0, 1},
                                               .stage = rojoRHI::Stage::ComputeShader,
                                               .access = rojoRHI::Access::ShaderRead};
            };
            const std::array<rojoRHI::ResourceAccess, 4> accesses{
                {read(inputs.history),
                 read(inputs.sceneColor),
                 read(inputs.motion),
                 {.texture = resources.texture(diagnostic).texture,
                  .range = {0, 1, 0, 1},
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderWrite}}};
            commands.declareAccess(accesses);
#endif
            const auto groups = dispatchGroups2D(params.width, params.height);
            commands.dispatch(groups[0], groups[1], 1);
        });
    return nextVersion(diagnostic);
}

//======================================================================================================================
GraphTexture TemporalResolve::declareDebugView(RenderGraph& graph, rojoRHI::CommandList& commands,
                                               TemporalDebugView debugView,
                                               const TemporalInputs& inputs,
                                               const TemporalResolveOutputs& outputs,
                                               GraphTexture diagnostic, bool readsDiagnostic,
                                               GraphTexture displayResult) {
    const bool nativeTaa = inputs.mode == ReconstructionMode::NativeTaa;
    const bool readsRejection = nativeTaa && viewReadsRejection(debugView);
    const bool readsReprojected =
        inputs.mode != ReconstructionMode::Raw && viewReadsReprojected(debugView);
    // The age lives in the resolved colour's alpha, which under Raw is a copy of the frame's own
    // coverage rather than a count -- the view still reads it, and shows what the mode produced.
    const bool readsResolved = debugView == TemporalDebugView::HistoryAge;

    PassDesc debugDesc;
    debugDesc.textureReads.push_back(inputs.motion);
    if (readsDiagnostic) {
        debugDesc.textureReads.push_back(diagnostic);
    }
    if (readsRejection) {
        debugDesc.textureReads.push_back(outputs.rejection);
    }
    if (readsReprojected) {
        debugDesc.textureReads.push_back(outputs.reprojected);
    }
    if (readsResolved) {
        debugDesc.textureReads.push_back(outputs.resolved);
    }
    debugDesc.color = ColorAttachment{
        .handle = displayResult, .load = LoadOp::Clear, .store = StoreOp::Store, .clearColor = {}};
    const GraphTexture rejection = outputs.rejection;
    const GraphTexture reprojected = outputs.reprojected;
    const GraphTexture resolved = outputs.resolved;
    const GraphTexture motion = inputs.motion;
    const FrameExtents extents = inputs.extents;
    graph.addPass(
        "lmx.pass.temporal.debugView", std::move(debugDesc),
        [this, &commands, motion, diagnostic, rejection, reprojected, resolved, readsDiagnostic,
         readsRejection, readsReprojected, readsResolved, debugView,
         extents](const PassResources& resources) {
            // A view that reads none of these carries the 1x1 fallback: every texel the shader
            // loads lies outside it, and an out-of-bounds Load answers with zeroes -- which is the
            // shader's own "nothing to show" for each of them.
            const auto viewOr = [&](bool wanted, GraphTexture handle) {
                return static_cast<uint32_t>(wanted ? resources.view(handle)
                                                    : m_viewFallback.view());
            };
            const auto params = buildTemporalDebugViewParams(debugView, extents);
            const auto paramsAddress = frameData(commands, params);
            engine::registerFrameRoot("TemporalDebugViewParams", paramsAddress);
            commands.setPipeline(m_debugViewPipeline.get());
            engine::registerFrameRoot("TemporalDebugViewRoot",
                                      commands.pushRoot(TemporalDebugViewRoot{
                                          .paramsAddr = paramsAddress,
                                          .motion = static_cast<uint32_t>(resources.view(motion)),
                                          .diagnostic = viewOr(readsDiagnostic, diagnostic),
                                          .rejection = viewOr(readsRejection, rejection),
                                          .reprojected = viewOr(readsReprojected, reprojected),
                                          .resolved = viewOr(readsResolved, resolved)}));
#ifndef NDEBUG
            std::array<rojoRHI::ResourceAccess, 5> accesses{};
            uint32_t accessCount = 0;
            const auto read = [&](bool wanted, GraphTexture handle) {
                if (wanted) {
                    accesses[accessCount++] = {.texture = resources.texture(handle).texture,
                                               .range = {0, 1, 0, 1},
                                               .stage = rojoRHI::Stage::FragmentShader,
                                               .access = rojoRHI::Access::ShaderRead};
                }
            };
            read(true, motion);
            read(readsDiagnostic, diagnostic);
            read(readsRejection, rejection);
            read(readsReprojected, reprojected);
            read(readsResolved, resolved);
            commands.declareAccess(std::span{accesses}.first(accessCount));
#endif
            commands.draw(3);
        });
    return nextVersion(displayResult);
}

} // namespace lmx::render
