//----------------------------------------------------------------------------------------------------------------------
/// @file PerformancePanel.h
/// @brief Declares the Performance panel's drawing entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/PerformanceModel.h"

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The shell's dock builder places the window under
/// exactly this name, so both sides read it from here.
inline constexpr const char* kPerformancePanelWindowName = "Performance";

/// Draws the Performance panel over one coherent `PerformanceModel::snapshot()`: the wall-clock
/// frame interval and FPS, the Viewport panel's logical size and the scene target's pixel extent
/// shown as separate labelled values, the rolling per-pass GPU table (Pass/Average/Latest/
/// Min-Max/Samples, schedule order), the `Timed pass sum` with its explicit
/// not-total-GPU-frame-time caveat, and the snapshot's frame ID, object/draw counts, and transient
/// memory. `open` follows the window's close button, exactly as `ImGui::Begin` writes it.
///
/// Pause and Clear History are model calls (`PerformanceModel::setPaused`,
/// `PerformanceModel::clearHistory`) -- this panel holds no timing state of its own, so what the
/// controls do and what the snapshot shows can never drift apart.
void drawPerformancePanel(bool& open, PerformanceModel& model);

} // namespace lmx::app
