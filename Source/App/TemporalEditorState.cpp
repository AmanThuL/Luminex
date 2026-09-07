//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalEditorState.cpp
/// @brief Implements the editor's scene generation counter and camera-cut latch.
//----------------------------------------------------------------------------------------------------------------------

#include "App/TemporalEditorState.h"

namespace lmx::app {

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
