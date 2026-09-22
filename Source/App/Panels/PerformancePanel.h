//----------------------------------------------------------------------------------------------------------------------
/// @file PerformancePanel.h
/// @brief Declares the Performance panel's drawing entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/Performance/PerformanceModel.h"
#include "App/Panels/MeasurementPanel.h"

namespace lmx::app {

/// The persistent Dear ImGui name of the independent Performance window.
inline constexpr const char* kPerformancePanelWindowName = "Performance";

/// Shell-owned native-window presentation state; independent of metric and measurement lifetime.
struct PerformancePanelState {
    bool ownsPlatformWindow = false; ///< Suppresses the ImGui title bar once native chrome exists.
    bool resetPlacement =
        false; ///< Re-centers/resizes on the next open after layout migration/reset.
    bool requestFocus =
        false; ///< One explicit open/show request, consumed once a native window exists.
};

/// Draws coherent, responsive metrics with sortable GPU timing rows and a labeled wall-clock
/// interval plot. Freeze, Resume and Clear change the model before the snapshot is read; selection
/// and sorting affect presentation only. `open` follows the ImGui window close button.
void drawPerformancePanel(bool& open, PerformanceModel& model, PerformancePanelState& state,
                          MeasurementPanelContext* measurement = nullptr);

} // namespace lmx::app
