//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalEditorState.h
/// @brief Declares the editor's scene-generation counter, camera-cut latch, and default one-shots.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/EditorRenderSettings.h"
#include "Engine/SceneLibrary.h"

#include <cstdint>

namespace lmx::app {

/// Editor-owned temporal bookkeeping the shell keeps across scene switches, entirely separate from
/// the scene's own animation clock (Engine/Scene.h): a monotonic counter `TemporalSettings::
/// sceneGeneration` is built from, a one-shot camera-cut request the Inspector's "Camera cut"
/// button raises, and whether TemporalLab's once-only defaults have already fired. Pure and
/// SDL/ImGui-free, like ExposureReset.h, so it is unit-testable without a device.
struct TemporalEditorState {
    /// Bumped by `onSceneSelected` on every call, including the very first -- a different value is
    /// what tells the renderer's history a scene switch happened (spec sections 4-6), independent
    /// of a manually requested camera cut.
    uint64_t sceneGeneration = 0;
    /// Set by `requestCameraCut`, cleared by `consumeCameraCut`. A camera teleport is an event, not
    /// a state, so this is a latch rather than a level.
    bool cameraCutPending = false;
    /// Whether TemporalLab's once-only defaults (temporal inputs and the Motion view turned on)
    /// have already been applied. Latched true the first time `onSceneSelected` is called with the
    /// TemporalLab scene, so a later reselect -- or the user's own edit in between -- never
    /// overrides the settings again.
    bool temporalLabDefaultsApplied = false;
};

/// Marks that the camera teleported this frame (the Inspector's "Camera cut" button). One-shot:
/// `consumeCameraCut` reports it exactly once.
void requestCameraCut(TemporalEditorState& state);

/// True exactly once per `requestCameraCut` call; consuming clears the latch, which is what turns
/// "a cut was requested" into "the next frame's SceneView says so."
bool consumeCameraCut(TemporalEditorState& state);

/// Called for every scene selection, including the first: bumps `state.sceneGeneration`
/// unconditionally, then -- only the first time `id` names the TemporalLab diagnostic scene and
/// only while `state.temporalLabDefaultsApplied` is still false -- turns on `settings.
/// temporalEnabled` and switches `settings.temporalDebugView` to `MotionVectors`, the pairing that
/// makes TemporalLab show its own point on first selection without extra clicks. Any other scene,
/// or a later reselect of TemporalLab, leaves `settings` untouched.
void onSceneSelected(TemporalEditorState& state, EditorRenderSettings& settings,
                     engine::SceneId id);

} // namespace lmx::app
