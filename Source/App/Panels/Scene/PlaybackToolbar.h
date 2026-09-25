//----------------------------------------------------------------------------------------------------------------------
/// @file PlaybackToolbar.h
/// @brief Declares scene transport controls drawn inside the main menu bar.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <string_view>

namespace lmx::app {

/// One playback intent for the shell to apply after drawing the transport.
enum class PlaybackToolbarAction {
    None,  ///< No playback control was activated.
    Play,  ///< Start or resume scene playback.
    Pause, ///< Pause scene playback while retaining its starting state.
    Stop,  ///< Stop the active run and restore its starting state.
    Step,  ///< Advance a paused scene by one fixed frame.
};

/// Borrowed state for one transport draw; the shell owns playback and measurement execution.
struct PlaybackToolbarContext {
    bool& followCameraRail;         ///< Whether scene playback follows its authored camera path.
    bool playing = false;           ///< Scene playback is currently advancing.
    bool active = false;            ///< A run retains a starting-state snapshot until Stop.
    bool measurementActive = false; ///< A fixed, uninterrupted measurement sequence is running.
    bool hasCameraRail = false;     ///< The scene provides an authored camera path.
    std::string_view readout;       ///< Scene time or measurement progress, also used in tooltips.
};

/// Measures all visible buttons with the shared glyph or fallback-label widths and spacing.
float playbackToolbarButtonsWidth(const PlaybackToolbarContext& context);

/// Draws in the current menu row without reserving a sidebar or wrapping controls.
PlaybackToolbarAction drawPlaybackToolbar(const PlaybackToolbarContext& context, bool showReadout);

} // namespace lmx::app
