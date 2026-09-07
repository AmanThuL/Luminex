//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalEditorState.cpp
/// @brief Implements the editor's scene generation counter, camera-cut latch, and default one-shot.
//----------------------------------------------------------------------------------------------------------------------

#include "App/TemporalEditorState.h"

#include "Core/Assert.h"

#include <optional>

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
void onSceneSelected(TemporalEditorState& state, EditorRenderSettings& settings,
                     engine::SceneId id) {
    ++state.sceneGeneration;
    if (state.temporalLabDefaultsApplied) {
        return;
    }
    const std::optional<engine::SceneId> temporalLab = engine::parseSceneId("temporal-lab");
    LMX_ASSERT(temporalLab.has_value(), "TemporalEditorState: catalog has no 'temporal-lab' scene");
    if (id == *temporalLab) {
        settings.temporalEnabled = true;
        settings.temporalDebugView = render::TemporalDebugView::MotionVectors;
        state.temporalLabDefaultsApplied = true;
    }
}

} // namespace lmx::app
