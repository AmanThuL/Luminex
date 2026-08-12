//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphPanel.h
/// @brief Declares the Render Graph panel's drawing entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/FrameRecordRing.h"

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The shell's dock builder places the window under
/// exactly this name, so both sides read it from here.
inline constexpr const char* kRenderGraphPanelWindowName = "Render Graph";

/// Draws the Render Graph panel over the newest retained frame whose GPU timings have retired and
/// joined by frame ID: resources, schedule, culled passes, transitions, and transient placement,
/// plus a button that dumps that same record to a file next to the binary. `open` follows the
/// window's close button, exactly as `ImGui::Begin` writes it.
///
/// This is the exact compiled shape of one frame, not a rolling summary, and it says so when no
/// frame has retired yet -- true for the first few frames of a run, and not an error.
void drawRenderGraphPanel(bool& open, const FrameRecordRing& frameRecords);

} // namespace lmx::app
