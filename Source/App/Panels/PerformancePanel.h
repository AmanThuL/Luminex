//----------------------------------------------------------------------------------------------------------------------
/// @file PerformancePanel.h
/// @brief Declares the Performance panel's drawing entry point and the observations it displays.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/PassTimingHistory.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The shell's dock builder places the window under
/// exactly this name, so both sides read it from here.
inline constexpr const char* kPerformancePanelWindowName = "Performance";

/// The rolling observations the Performance panel displays, borrowed for one draw call.
///
/// Every span and reference names storage the shell owns and keeps alive across the call; the panel
/// retains nothing.
struct PerformancePanelContext {
    uint32_t viewportWidth = 0;          ///< Viewport panel width in backing pixels.
    uint32_t viewportHeight = 0;         ///< Viewport panel height in backing pixels.
    bool viewportHovered = false;        ///< Whether the pointer is over the Viewport panel.
    bool viewportFocused = false;        ///< Whether the Viewport panel has keyboard focus.
    uint32_t sceneTargetWidth = 0;       ///< Renderer color-target width in pixels.
    uint32_t sceneTargetHeight = 0;      ///< Renderer color-target height in pixels.
    std::span<const float> frameTimesMs; ///< Ring of wall-clock frame times, oldest at the cursor.
    size_t frameTimeCursor = 0;          ///< Index of the oldest entry in `frameTimesMs`.
    /// Published pass summaries in schedule order; empty until a frame's timings have retired.
    std::span<const PassTimingSummary> passTimings;
    /// Whether timing collection and publication are frozen. The panel's checkbox writes it, and
    /// the shell reads it when deciding whether to sample the newest retired frame.
    bool& passTimingsPaused;
};

/// Draws the Performance panel: frame rate, viewport and scene-target extents, the frame-time plot,
/// and the rolling per-pass GPU table. `open` follows the window's close button, exactly as
/// `ImGui::Begin` writes it.
///
/// A changed ordered pass-label sequence resets every timing series upstream of this panel, so the
/// rows here never average timings from unlike graph shapes.
void drawPerformancePanel(bool& open, const PerformancePanelContext& context);

} // namespace lmx::app
