//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureFrameData.cpp
/// @brief Builds the exposure frame data from plain values.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Exposure/ExposureFrameData.h"

#include <cmath>

namespace lmx::render {

//======================================================================================================================
ExposureSeedParams buildExposureSeedParams(float exposureEv) {
    return {.exposure = std::exp2(exposureEv)};
}

} // namespace lmx::render
