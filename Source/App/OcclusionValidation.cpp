//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionValidation.cpp
/// @brief Validates the isolated offscreen active-extent recovery diagnostic.
//----------------------------------------------------------------------------------------------------------------------
#include "App/OcclusionValidation.h"
#include "Core/Util/Parse.h"
#include <cmath>
#include <cstdlib>

namespace lmx::app {
//======================================================================================================================
std::expected<std::optional<OcclusionScaleStep>, std::string>
readOcclusionScaleStep(bool unscored, bool temporalEnabled) {
    const char* frame = std::getenv("LMX_OCCLUSION_SCALE_CHANGE_FRAME");
    const char* value = std::getenv("LMX_OCCLUSION_SCALE_CHANGE_VALUE");
    if (!frame && !value)
        return std::optional<OcclusionScaleStep>{};
    OcclusionScaleStep step;
    if (!frame || !value || !parseNumber(std::string_view(frame), step.frame) ||
        !parseNumber(std::string_view(value), step.scale) || !std::isfinite(step.scale) ||
        step.scale < 0.5f || step.scale > 1.0f)
        return std::unexpected("Scale-step evidence requires both LMX_OCCLUSION_SCALE_CHANGE_FRAME "
                               "and LMX_OCCLUSION_SCALE_CHANGE_VALUE in [0.5, 1]");
    if (!unscored || !temporalEnabled)
        return std::unexpected("Scale-step evidence requires unscored temporal rendering");
    return std::optional(step);
}
} // namespace lmx::app
