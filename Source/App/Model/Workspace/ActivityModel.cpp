//----------------------------------------------------------------------------------------------------------------------
/// @file ActivityModel.cpp
/// @brief Selects an activity from existing work and recent controller changes.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Workspace/ActivityModel.h"

#include <format>

namespace lmx::app {

//======================================================================================================================
std::optional<Activity> currentActivity(const ActivityInputs& inputs) {
    if (inputs.measure) {
        const auto& measure = *inputs.measure;
        switch (measure.phase) {
        case MeasurementState::Warmup:
        case MeasurementState::Measuring: {
            const std::string verb =
                measure.phase == MeasurementState::Warmup ? "Warmup" : "Measuring";
            const std::optional<float> progress =
                measure.total ? std::optional{static_cast<float>(measure.done) / measure.total}
                              : std::nullopt;
            return Activity{
                Actor::Operator, verb, progress, true,
                std::format("Measurement · {} {} / {} frames", verb, measure.done, measure.total)};
        }
        case MeasurementState::Draining:
            return Activity{Actor::Operator, "Finishing", std::nullopt, true,
                            "Measurement · waiting for GPU retirement"};
        case MeasurementState::Idle:
        case MeasurementState::Complete:
        case MeasurementState::Cancelled:
            break;
        }
    }
    if (inputs.capturePending)
        return Activity{Actor::Operator, "Capturing", std::nullopt, false,
                        "Operator-requested GPU capture pending"};
    if (inputs.documentWork)
        return Activity{Actor::System, *inputs.documentWork, std::nullopt, false,
                        std::format("Scene document work · {}", *inputs.documentWork)};
    if (inputs.controller) {
        const auto& change = *inputs.controller;
        const double age = inputs.now - change.at;
        if (age >= 0.0 && age < 2.0)
            return Activity{
                Actor::System, "Adjusting resolution", std::nullopt, false,
                std::format("Dynamic resolution · scale {:.3f} → {:.3f}", change.from, change.to)};
    }
    return std::nullopt;
}

} // namespace lmx::app
