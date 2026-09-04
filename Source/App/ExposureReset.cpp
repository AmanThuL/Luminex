//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureReset.cpp
/// @brief Implements the pure decision behind spec 9's four exposure-feedback reset triggers.
//----------------------------------------------------------------------------------------------------------------------

#include "App/ExposureReset.h"

namespace lmx::app {

//======================================================================================================================
bool shouldResetExposure(const ExposureResetContext& previous,
                         const ExposureResetContext& current) {
    if (!previous.sceneId.has_value()) {
        return true; // First frame: no prior state to continue metering from.
    }
    if (current.sceneId != previous.sceneId) {
        return true; // Scene switch: the previous scene's metering says nothing about this one.
    }
    if (current.autoExposureEnabled && !previous.autoExposureEnabled) {
        return true; // Enable transition: the feedback loop has produced nothing yet.
    }
    if (current.width != previous.width || current.height != previous.height) {
        return true; // Resize: the histogram's binning covered a differently-sized image.
    }
    return false;
}

//======================================================================================================================
void setAutoExposureEnabled(EditorRenderSettings& settings, ExposureResetContext& exposureContext,
                            bool& exposureResetPending, bool enabled) {
    settings.autoExposureEnabled = enabled;
    ExposureResetContext candidate = exposureContext;
    candidate.autoExposureEnabled = enabled;
    if (shouldResetExposure(exposureContext, candidate)) {
        exposureResetPending = true;
    }
    exposureContext = candidate;
}

} // namespace lmx::app
