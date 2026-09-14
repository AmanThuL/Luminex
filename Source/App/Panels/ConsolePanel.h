//----------------------------------------------------------------------------------------------------------------------
/// @file ConsolePanel.h
/// @brief Declares the read-only bounded editor Console panel.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/ConsoleModel.h"

namespace lmx::app {

inline constexpr const char* kConsolePanelWindowName = "Console"; ///< Dockable Console window name.

/// Draws filtered retained logs, explicit loss counters and independent Freeze/Clear/Copy actions.
/// The model and ImGui are used only on the main thread; log producers access ConsoleLog directly.
void drawConsolePanel(bool& open, ConsoleModel& model);

} // namespace lmx::app
