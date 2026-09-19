//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalEditorState.cpp
/// @brief Implements the editor's scene generation counter and camera-cut latch.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/TemporalEditorState.h"

#include "Render/Temporal.h"

namespace lmx::app {

//======================================================================================================================
std::string_view reconstructionName(render::ReconstructionMode mode,
                                    const rojoRHI::TemporalScalerSupport& support) {
    switch (mode) {
    case render::ReconstructionMode::Raw:
        return "Raw";
    case render::ReconstructionMode::NativeTaa:
        return "Native TAA";
    case render::ReconstructionMode::VendorTemporal:
        return support.available ? support.name : "Vendor temporal (unavailable)";
    }
    return "Native TAA";
}

//======================================================================================================================
render::TemporalDebugView clampTemporalDebugView(render::TemporalDebugView view,
                                                 render::ReconstructionMode effectiveMode) {
    return effectiveMode == render::ReconstructionMode::VendorTemporal &&
                   render::nativeOnlyTemporalView(view)
               ? render::TemporalDebugView::Off
               : view;
}

//======================================================================================================================
void observeDeclaredTemporal(TemporalEditorState& state, const EditorRenderSettings& settings,
                             const render::TemporalStatus& status, uint64_t frameId) {
    const bool changed = state.declaredFrameId == 0 ||
                         state.declaredSceneGeneration != state.sceneGeneration ||
                         state.declaredTemporalEnabled != settings.temporalEnabled ||
                         state.declaredRequest != settings.reconstruction;
    if (changed) {
        state.compatibleFromFrame = frameId;
        state.liveTimedPassSumMilliseconds.reset();
        state.liveMeasurementFrame = 0;
    }
    state.declaredFrameId = frameId;
    state.declaredSceneGeneration = state.sceneGeneration;
    state.declaredTemporalEnabled = settings.temporalEnabled;
    state.declaredRequest = settings.reconstruction;
    if (status.lastReset != render::HistoryResetReason::None) {
        state.lastResetReason = status.lastReset;
        state.lastResetFrame = status.lastResetFrame;
    }
}

//======================================================================================================================
void observeRetiredTemporal(TemporalEditorState& state, const RetainedFrame* frame) {
    if (!frame || !frame->timed || state.declaredFrameId == 0 ||
        state.declaredSceneGeneration != state.sceneGeneration ||
        frame->record.frameId < state.compatibleFromFrame ||
        frame->record.frameId <= state.liveMeasurementFrame) {
        return;
    }
    double sum = 0.0;
    for (const auto& timing : frame->timings) {
        sum += timing.gpuMilliseconds;
    }
    state.liveTimedPassSumMilliseconds = sum;
    state.liveMeasurementFrame = frame->record.frameId;
}

//======================================================================================================================
TemporalPresentation temporalPresentation(const TemporalEditorState& state,
                                          const EditorRenderSettings& settings,
                                          const render::TemporalStatus& status,
                                          const rojoRHI::TemporalScalerSupport& support,
                                          uint32_t outputWidth, uint32_t outputHeight) {
    TemporalPresentation result;
    result.requestedName = reconstructionName(settings.reconstruction, support);
    result.waitingForDeclaration = state.declaredFrameId == 0 ||
                                   state.declaredSceneGeneration != state.sceneGeneration ||
                                   state.declaredTemporalEnabled != settings.temporalEnabled ||
                                   state.declaredRequest != settings.reconstruction;
    result.temporalActive = settings.temporalEnabled && !result.waitingForDeclaration;
    result.effectiveName = !settings.temporalEnabled ? "Off"
                           : result.waitingForDeclaration
                               ? "Waiting for declaration"
                               : reconstructionName(status.reconstruction, support);
    result.extents = settings.temporalEnabled ? status.extents
                                              : render::FrameExtents{outputWidth, outputHeight,
                                                                     outputWidth, outputHeight};
    result.effectiveScale = settings.temporalEnabled ? status.renderScale : 1.0f;
    if (result.temporalActive &&
        settings.reconstruction == render::ReconstructionMode::VendorTemporal) {
        if (status.vendorFallback == render::VendorFallback::Unsupported) {
            result.fallbackReason =
                "Vendor reconstruction is unavailable on this device; using Native TAA.";
        } else if (status.vendorFallback == render::VendorFallback::CreationFailed) {
            result.fallbackReason =
                "Vendor scaler creation failed for this output size; using Native TAA.";
        }
    }
    return result;
}

//======================================================================================================================
void requestCameraCut(TemporalEditorState& state) {
    state.cameraCutPending = true;
}

//======================================================================================================================
bool consumeCameraCut(TemporalEditorState& state) {
    const bool pending = state.cameraCutPending;
    state.cameraCutPending = false;
    return pending;
}

//======================================================================================================================
void onSceneSelected(TemporalEditorState& state, EditorRenderSettings& /*settings*/,
                     scene::SceneId /*id*/) {
    ++state.sceneGeneration;
    state.liveTimedPassSumMilliseconds.reset();
    state.liveMeasurementFrame = 0;
}

} // namespace lmx::app
