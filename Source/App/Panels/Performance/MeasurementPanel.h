//----------------------------------------------------------------------------------------------------------------------
/// @file MeasurementPanel.h
/// @brief Declares interactive measurement controls and one-frame action intents.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "App/Model/Performance/MeasurementRun.h"
#include <string>
namespace lmx::app {
/// Explicit action requested by measurement controls.
enum class MeasurementAction {
    None,  ///< No action requested.
    Export ///< Write the completed or cancelled report to the chosen path.
};
/// Shell-owned fields borrowed while drawing Performance.
struct MeasurementPanelContext {
    const MeasurementRun& run;                          ///< Complete or ongoing shared run model.
    uint32_t& warmup;                                   ///< Warmup frame count for the next run.
    uint32_t& frames;                                   ///< Measured frame count for the next run.
    std::string& exportPath;                            ///< Destination for explicit Export.
    const std::string& feedback;                        ///< Export/start outcome.
    MeasurementAction action = MeasurementAction::None; ///< One action raised this frame.
    bool reveal = false; ///< Expand the section when the top transport starts a measurement.
};
/// Draws the Measure section inside an existing Performance window.
void drawMeasurementSection(MeasurementPanelContext& context);
} // namespace lmx::app
