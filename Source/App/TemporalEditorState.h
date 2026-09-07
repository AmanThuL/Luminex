//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalEditorState.h
/// @brief Declares the editor's scene-generation counter and camera-cut latch.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/EditorRenderSettings.h"
#include "Engine/SceneLibrary.h"

#include <cstdint>

namespace lmx::app {

/// Editor-owned temporal bookkeeping the shell keeps across scene switches, entirely separate from
/// the scene's own animation clock (Engine/Scene.h): a monotonic counter `TemporalSettings::
/// sceneGeneration` is built from, and a one-shot camera-cut request the Inspector's "Camera cut"
/// button raises. Pure and SDL/ImGui-free, like ExposureReset.h, so it is unit-testable without a
/// device.
struct TemporalEditorState {
    /// Bumped by `onSceneSelected` on every call, including the very first -- a different value is
    /// what tells the renderer's history a scene switch happened (spec sections 4-6), independent
    /// of a manually requested camera cut.
    uint64_t sceneGeneration = 0;
    /// Set by `requestCameraCut`, cleared by `consumeCameraCut`. A camera teleport is an event, not
    /// a state, so this is a latch rather than a level.
    bool cameraCutPending = false;
};

/// Marks that the camera teleported this frame (the Inspector's "Camera cut" button). One-shot:
/// `consumeCameraCut` reports it exactly once.
void requestCameraCut(TemporalEditorState& state);

/// True exactly once per `requestCameraCut` call; consuming clears the latch, which is what turns
/// "a cut was requested" into "the next frame's SceneView says so."
bool consumeCameraCut(TemporalEditorState& state);

/// Called for every scene selection, including the first: bumps `state.sceneGeneration`
/// unconditionally. Selecting any scene leaves `settings` untouched -- TemporalLab now opens with
/// the same defaults as every other scene.
void onSceneSelected(TemporalEditorState& state, EditorRenderSettings& settings,
                     engine::SceneId id);

} // namespace lmx::app
