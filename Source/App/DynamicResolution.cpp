//----------------------------------------------------------------------------------------------------------------------
/// @file DynamicResolution.cpp
/// @brief Implements the pure per-frame dynamic-resolution policy.
//----------------------------------------------------------------------------------------------------------------------

#include "App/DynamicResolution.h"

namespace lmx::app {

//======================================================================================================================
bool dynamicResolutionActive(const EditorRenderSettings& settings) {
    return settings.dynamicResolutionEnabled && settings.temporalEnabled;
}

//======================================================================================================================
double frameGpuMilliseconds(std::span<const rhi::PassTiming> timings) {
    double total = 0.0;
    for (const rhi::PassTiming& timing : timings) {
        total += timing.gpuMilliseconds;
    }
    return total;
}

//======================================================================================================================
void applyDynamicResolution(DynamicResolutionState& state, render::ResolutionController& controller,
                            EditorRenderSettings& settings, const RetainedFrame* newestTimed) {
    const bool active = dynamicResolutionActive(settings);
    if (active) {
        if (!state.wasEnabled) {
            // Off->on: seed the controller from the manual slider so the picture does not jump.
            controller.reset(settings.renderScale);
        }
        if (controller.settings().budgetMilliseconds != settings.gpuBudgetMilliseconds) {
            render::ResolutionControllerSettings updated = controller.settings();
            updated.budgetMilliseconds = settings.gpuBudgetMilliseconds;
            controller.setSettings(updated);
        }
        if (newestTimed != nullptr && newestTimed->record.frameId != state.lastObservedFrame) {
            state.lastObservedMilliseconds = frameGpuMilliseconds(newestTimed->timings);
            controller.observe(newestTimed->record.frameId, state.lastObservedMilliseconds);
            state.lastObservedFrame = newestTimed->record.frameId;
        }
        settings.renderScale = controller.scale();
    } else if (newestTimed != nullptr) {
        // Idle: the frame ran at whatever scale the renderer chose without the controller, so its
        // timing is not a measurement of the controller's scale. Its number is still carried
        // forward, so becoming active again does not consume a frame from the idle period.
        state.lastObservedFrame = newestTimed->record.frameId;
    }
    // Idle: settings.renderScale is left at the controller's last value -- editing it there is the
    // manual slider's job, which the Inspector disables while dynamic resolution is on.
    state.wasEnabled = active;
}

} // namespace lmx::app
