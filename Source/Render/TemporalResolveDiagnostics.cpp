//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveDiagnostics.cpp
/// @brief Declares reprojection diagnostics and temporal debug display.
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
using temporal_detail::SpatialUpscaleParams;
using temporal_detail::spatialUpscaleParams;
using temporal_detail::viewReadsRejection;
using temporal_detail::viewReadsReprojected;

namespace {

// Mirrors Shaders/TemporalReproject.slang's TemporalReprojectParams.
struct TemporalReprojectParams {
    uint32_t width = 0; // The output extent: the pass's dispatch bound.
    uint32_t height = 0;
    uint32_t renderWidth = 0; // The active rectangle of the scene colour and the motion target.
    uint32_t renderHeight = 0;
    uint32_t allocatedWidth = 0; // The allocation of those two, which their UVs are taken over.
    uint32_t allocatedHeight = 0;
    glm::vec2 jitterOffset{0.0f};
};
static_assert(sizeof(TemporalReprojectParams) == 32,
              "must match TemporalReproject.slang's TemporalReprojectParams");

// Mirrors Shaders/TemporalDebugView.slang's TemporalDebugViewParams.
struct TemporalDebugViewParams {
    uint32_t view = 0;
    uint32_t renderWidth = 0; // The motion target's active rectangle; every other input is output.
    uint32_t renderHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
};
static_assert(sizeof(TemporalDebugViewParams) == 20,
              "must match TemporalDebugView.slang's TemporalDebugViewParams");

// TemporalReproject.slang's slot map.
constexpr uint32_t kReprojectHistorySlot = 0;    // texture
constexpr uint32_t kReprojectSceneColorSlot = 1; // texture
constexpr uint32_t kReprojectMotionSlot = 2;     // texture
constexpr uint32_t kReprojectDiagnosticSlot = 3; // storage texture
constexpr uint32_t kReprojectSamplerSlot = 0;    // sampler
constexpr uint32_t kReprojectParamsSlot = 0;     // buffer

// TemporalDebugView.slang's slot map, plus the view selectors its fragment branches on.
constexpr uint32_t kDebugViewMotionSlot = 0;      // texture
constexpr uint32_t kDebugViewDiagnosticSlot = 1;  // texture
constexpr uint32_t kDebugViewRejectionSlot = 2;   // texture
constexpr uint32_t kDebugViewReprojectedSlot = 3; // texture
constexpr uint32_t kDebugViewResolvedSlot = 4;    // texture
constexpr uint32_t kDebugViewParamsSlot = 0;      // buffer

//======================================================================================================================
// The view selector Shaders/TemporalDebugView.slang branches on. Off never reaches the shader --
// the pass is not declared in that mode -- so it maps to the same value MotionVectors does rather
// than to a code the fragment has no branch for.
uint32_t debugViewSelector(TemporalDebugView view) {
    return view == TemporalDebugView::Off ? 1u : static_cast<uint32_t>(view);
}

//======================================================================================================================
// Shaders/TemporalReproject.slang's block: the pass runs over the output extent and resamples the
// render extent's active rectangle to meet it, on spatialUpscaleParams()' terms.
TemporalReprojectParams reprojectParams(const TemporalInputs& inputs) {
    const SpatialUpscaleParams resampling = spatialUpscaleParams(inputs);
    return TemporalReprojectParams{.width = resampling.outputWidth,
                                   .height = resampling.outputHeight,
                                   .renderWidth = resampling.renderWidth,
                                   .renderHeight = resampling.renderHeight,
                                   .allocatedWidth = resampling.allocatedWidth,
                                   .allocatedHeight = resampling.allocatedHeight,
                                   .jitterOffset = resampling.jitterOffset};
}

} // namespace

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
         params = reprojectParams(inputs)](const PassResources& resources) {
            const GraphResult<rojoRHI::Texture*> historyTexture = resources.texture(inputs.history);
            LMX_ASSERT(historyTexture.has_value(), historyTexture.error().message);
            const GraphResult<rojoRHI::Texture*> sceneTexture =
                resources.texture(inputs.sceneColor);
            LMX_ASSERT(sceneTexture.has_value(), sceneTexture.error().message);
            const GraphResult<rojoRHI::Texture*> motionTexture = resources.texture(inputs.motion);
            LMX_ASSERT(motionTexture.has_value(), motionTexture.error().message);
            const GraphResult<rojoRHI::Texture*> target = resources.texture(diagnostic);
            LMX_ASSERT(target.has_value(), target.error().message);

            commands.bindComputePipeline(*m_reprojectPipeline);
            commands.bindTexture(kReprojectHistorySlot, **historyTexture);
            commands.bindTexture(kReprojectSceneColorSlot, **sceneTexture);
            commands.bindTexture(kReprojectMotionSlot, **motionTexture);
            commands.bindStorageTexture(kReprojectDiagnosticSlot, **target, {},
                                        rojoRHI::StorageAccess::Write);
            commands.bindSampler(kReprojectSamplerSlot, *m_sampler);
            commands.bindFrameData(kReprojectParamsSlot, params);
            commands.dispatch(divRoundUp(params.width, kComputeThreadsPerGroup2D),
                              divRoundUp(params.height, kComputeThreadsPerGroup2D), 1);
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
            const GraphResult<rojoRHI::Texture*> motionTexture = resources.texture(motion);
            LMX_ASSERT(motionTexture.has_value(), motionTexture.error().message);

            commands.bindPipeline(*m_debugViewPipeline);
            commands.bindTexture(kDebugViewMotionSlot, **motionTexture);
            // A view that reads none of these binds the 1x1 fallback: every texel the
            // shader loads lies outside it, and an out-of-bounds Load answers with zeroes
            // -- which is the shader's own "nothing to show" for each of them.
            const auto bindOptional = [&](uint32_t slot, bool wanted, GraphTexture handle) {
                if (!wanted) {
                    commands.bindTexture(slot, *m_viewFallback);
                    return;
                }
                const GraphResult<rojoRHI::Texture*> texture = resources.texture(handle);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindTexture(slot, **texture);
            };
            bindOptional(kDebugViewDiagnosticSlot, readsDiagnostic, diagnostic);
            bindOptional(kDebugViewRejectionSlot, readsRejection, rejection);
            bindOptional(kDebugViewReprojectedSlot, readsReprojected, reprojected);
            bindOptional(kDebugViewResolvedSlot, readsResolved, resolved);

            const TemporalDebugViewParams params{.view = debugViewSelector(debugView),
                                                 .renderWidth = extents.renderWidth,
                                                 .renderHeight = extents.renderHeight,
                                                 .outputWidth = extents.outputWidth,
                                                 .outputHeight = extents.outputHeight};
            commands.bindFrameData(kDebugViewParamsSlot, params);
            commands.draw(3);
        });
    return nextVersion(displayResult);
}

} // namespace lmx::render
