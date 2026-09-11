//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalEditorState.cpp
/// @brief Implements the editor's scene generation counter and camera-cut latch.
//----------------------------------------------------------------------------------------------------------------------

#include "App/TemporalEditorState.h"

#include "Render/VendorTemporalScaler.h"

namespace lmx::app {

//======================================================================================================================
std::string_view reconstructionName(render::ReconstructionMode mode,
                                    const rhi::TemporalScalerSupport& support) {
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
                     engine::SceneId /*id*/) {
    ++state.sceneGeneration;
}

} // namespace lmx::app
