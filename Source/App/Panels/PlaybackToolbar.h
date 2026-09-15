//----------------------------------------------------------------------------------------------------------------------
/// @file PlaybackToolbar.h
/// @brief Declares the editor's shared scene and measurement playback controls.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <string_view>

namespace lmx::app {

/// One playback intent for the shell to apply after drawing the toolbar.
enum class PlaybackToolbarAction {
    None,            ///< No playback control was activated.
    Play,            ///< Start or resume the selected run mode.
    Pause,           ///< Pause scene playback while retaining its starting state.
    Stop,            ///< Stop the active run and restore its starting state.
    Step,            ///< Advance a paused scene by one fixed frame.
    ShowMeasurement, ///< Open and focus measurement settings/results without starting a run.
};

/// Borrowed state for one toolbar draw; the shell owns playback and measurement execution.
struct PlaybackToolbarContext {
    bool& measureOnPlay;            ///< Selects Measure instead of Scene for the next Play action.
    bool& followCameraRail;         ///< Whether scene playback follows its authored camera path.
    bool playing = false;           ///< Scene playback is currently advancing.
    bool active = false;            ///< A run retains a starting-state snapshot until Stop.
    bool measurementActive = false; ///< A fixed, uninterrupted measurement sequence is running.
    bool hasCameraRail = false;     ///< The scene provides an authored camera path.
    bool canMeasure = true;         ///< Whether the current measurement plan can start.
    double timeSeconds = 0.0;       ///< Current scene time displayed in seconds.
    std::string_view status; ///< Compact run state or measurement progress supplied by the shell.
    std::string_view disabledReason; ///< Explanation when a measurement cannot start.
};

/// Reserves a constant-height top sidebar and returns one playback intent. Call after the main
/// menu bar and before DockSpaceOverViewport so docking excludes the toolbar's work area.
PlaybackToolbarAction drawPlaybackToolbar(const PlaybackToolbarContext& context);

} // namespace lmx::app
