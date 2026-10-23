//----------------------------------------------------------------------------------------------------------------------
/// @file DisplayFrameData.h
/// @brief Declares the display frame data and the pure function that builds it.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

namespace lmx::render {

/// Mirrors Shaders/Passes/Display/DisplayTransform.slang's DisplayParams.
struct DisplayParams {
    float bloomIntensity; ///< 0 -- scene-linear multiplier of the bloom texture; 0 skips its load.
};
static_assert(sizeof(DisplayParams) == 4, "must match DisplayTransform.slang's DisplayParams");

/// Builds the display parameters. With bloom off the intensity is the exact zero the shader reads
/// as "skip the bloom texture load", whatever intensity was requested.
inline DisplayParams buildDisplayParams(bool bloomEnabled, float bloomIntensity) {
    return {.bloomIntensity = bloomEnabled ? bloomIntensity : 0.0f};
}

} // namespace lmx::render
