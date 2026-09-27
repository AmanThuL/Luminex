//----------------------------------------------------------------------------------------------------------------------
/// @file PerformancePanel.h
/// @brief Declares the Performance panel's drawing entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/Performance/PerformanceModel.h"
#include "App/Panels/Performance/MeasurementPanel.h"

namespace lmx::app {

/// The persistent Dear ImGui name of the independent Performance window.
inline constexpr const char* kPerformancePanelWindowName = "Performance";
/// Dockable summary identity, independent of the detached window and its saved bounds.
inline constexpr const char* kPerformanceSummaryWindowName = "Performance##Summary";

/// Shell-owned native-window presentation state; independent of metric and measurement lifetime.
struct PerformancePanelState {
    bool requestLiveTab =
        false; ///< Select Live once when opening timing details from another panel.
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

/// Draws the compact snapshot without a window class; true requests detached Live details.
bool drawPerformanceSummary(bool& open, const PerformanceModel& model);

} // namespace lmx::app
