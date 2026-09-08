//----------------------------------------------------------------------------------------------------------------------
/// @file DynamicResolution.cpp
/// @brief Implements the pure per-frame dynamic-resolution policy.
//----------------------------------------------------------------------------------------------------------------------

#include "App/DynamicResolution.h"

namespace lmx::app {

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
    if (settings.dynamicResolutionEnabled) {
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
    }
    // Disabled: settings.renderScale is left at the controller's last value -- editing it there is
    // the manual slider's job, which the Inspector disables while dynamic resolution is on.
    state.wasEnabled = settings.dynamicResolutionEnabled;
}

} // namespace lmx::app
