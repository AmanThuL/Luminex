//----------------------------------------------------------------------------------------------------------------------
/// @file PerformancePanel.h
/// @brief Declares the Performance panel's drawing entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/PerformanceModel.h"
#include "App/Panels/MeasurementPanel.h"

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The shell's dock builder places the window under
/// exactly this name, so both sides read it from here.
inline constexpr const char* kPerformancePanelWindowName = "Performance";

/// Draws coherent, responsive metrics with sortable GPU timing rows and a labeled wall-clock
/// interval plot. Freeze, Resume and Clear change the model before the snapshot is read; selection
/// and sorting affect presentation only. `open` follows the ImGui window close button.
void drawPerformancePanel(bool& open, PerformanceModel& model,
                          MeasurementPanelContext* measurement = nullptr);

} // namespace lmx::app
