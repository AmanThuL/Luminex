//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalEditorState.h
/// @brief Declares the editor's scene-generation counter and camera-cut latch.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/EditorRenderSettings.h"
#include "App/Model/FrameRecordRing.h"
#include "Engine/Catalog/SceneLibrary.h"

#include <cstdint>
#include <optional>

namespace lmx::app {

/// Editor-owned temporal bookkeeping the shell keeps across scene switches, entirely separate from
/// the scene's own animation clock (Scene/Scene.h): a monotonic counter `TemporalSettings::
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
    render::HistoryResetReason lastResetReason = render::HistoryResetReason::None; ///< Last event.
    uint64_t lastResetFrame = 0;  ///< Declared-frame counter paired with the retained event reason.
    uint64_t declaredFrameId = 0; ///< Device frame ID of the latest declared status.
    uint64_t compatibleFromFrame = 0;     ///< First frame in the current scene/mode configuration.
    uint64_t declaredSceneGeneration = 0; ///< Scene generation associated with declared status.
    bool declaredTemporalEnabled = false; ///< Whether the declared frame used temporal inputs.
    render::ReconstructionMode declaredRequest = render::ReconstructionMode::Raw; ///< Its request.
    std::optional<double> liveTimedPassSumMilliseconds; ///< Latest compatible retired measurement.
    uint64_t liveMeasurementFrame = 0; ///< Device frame ID paired with the live measurement.
};

/// Presentation of current requested settings alongside the most recently declared execution.
struct TemporalPresentation {
    std::string_view requestedName;    ///< Retained request, including while temporal is off.
    std::string_view effectiveName;    ///< Actual algorithm, Off, or Waiting for declaration.
    std::string_view fallbackReason;   ///< Nonempty when a vendor request fell back.
    bool waitingForDeclaration = true; ///< Current settings have not been declared yet.
    bool temporalActive = false;       ///< Temporal settings are enabled and match declared state.
    render::FrameExtents extents;      ///< Actual declared extent, or full output while disabled.
    float effectiveScale = 1.0f;       ///< Declared scale, or full resolution while disabled.
};

/// Records a declared frame without changing the renderer's per-frame reset semantics. Call even
/// while Inspector is hidden, immediately after declaration and before later settings edits.
void observeDeclaredTemporal(TemporalEditorState& state, const EditorRenderSettings& settings,
                             const render::TemporalStatus& status, uint64_t frameId);

/// Accepts a timing only from the current scene/mode generation; independent of metric freeze.
void observeRetiredTemporal(TemporalEditorState& state, const RetainedFrame* frame);

/// Derives truthful current-request versus declared-state labels without editing retained settings.
TemporalPresentation temporalPresentation(const TemporalEditorState& state,
                                          const EditorRenderSettings& settings,
                                          const render::TemporalStatus& status,
                                          const rojoRHI::TemporalScalerSupport& support,
                                          uint32_t outputWidth, uint32_t outputHeight);

/// Names a reconstruction option using the device capability's human-readable vendor name.
std::string_view reconstructionName(render::ReconstructionMode mode,
                                    const rojoRHI::TemporalScalerSupport& support);

/// Restricts native accumulation diagnostics only when the effective reconstruction is vendor.
render::TemporalDebugView clampTemporalDebugView(render::TemporalDebugView view,
                                                 render::ReconstructionMode effectiveMode);

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
