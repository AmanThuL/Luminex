#pragma once

// CPU mirror of the display transform Shaders/DisplayTransform.slang applies: the Khronos PBR
// Neutral tone map followed by the sRGB encode from Shaders/Encode.slang. Rendering tests that
// probe the display target state their expectation as a scene-linear radiance and push it through
// here, so the number in the test is derived from the same math the GPU runs rather than measured
// off an image.
//
// Kept beside the tests rather than in Source/: nothing that ships needs a CPU copy of the
// transform, and a mirror that the shader is checked against has to be written independently of it.

#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace lmx::test {

//======================================================================================================================
// IEC 61966-2-1, the decode side. Mirrors engine::srgbToLinear / render::srgbToLinear.
inline float srgbDecode(float encoded) {
    return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

//======================================================================================================================
// Mirrors Shaders/Encode.slang's linearToSrgbChannel, saturation included.
inline float srgbEncode(float linear) {
    const float c = std::clamp(linear, 0.0f, 1.0f);
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

//======================================================================================================================
// Mirrors Shaders/DisplayTransform.slang's pbrNeutralToneMap. Not per-channel: the black offset
// keys off the smallest channel and the shoulder desaturates toward the peak, so a colour's three
// channels move together.
inline glm::vec3 pbrNeutralToneMap(glm::vec3 color) {
    constexpr float kStartCompression = 0.8f - 0.04f;
    constexpr float kDesaturation = 0.15f;

    const float x = std::min(color.r, std::min(color.g, color.b));
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color -= offset;

    const float peak = std::max(color.r, std::max(color.g, color.b));
    if (peak < kStartCompression) {
        return color;
    }
    constexpr float d = 1.0f - kStartCompression;
    const float newPeak = 1.0f - d * d / (peak + d - kStartCompression);
    color *= newPeak / peak;

    const float g = 1.0f - 1.0f / (kDesaturation * (peak - newPeak) + 1.0f);
    return color + (glm::vec3(newPeak) - color) * g;
}

//======================================================================================================================
// The byte a scene-linear colour lands on in the display target, per channel.
inline std::array<int, 3> displayBytes(const glm::vec3& linear) {
    const glm::vec3 mapped = pbrNeutralToneMap(linear);
    return {static_cast<int>(srgbEncode(mapped.r) * 255.0f + 0.5f),
            static_cast<int>(srgbEncode(mapped.g) * 255.0f + 0.5f),
            static_cast<int>(srgbEncode(mapped.b) * 255.0f + 0.5f)};
}

//======================================================================================================================
// The same, for an achromatic radiance -- all three channels agree, so one number answers.
inline int displayByte(float linear) {
    return displayBytes(glm::vec3(linear))[0];
}

//======================================================================================================================
// The scene-linear radiance behind a display byte, for probes whose expected value was pinned as
// an sRGB byte before any tone map stood between the shading and the target.
inline float linearOfSrgbByte(int byte) {
    return srgbDecode(static_cast<float>(byte) / 255.0f);
}

} // namespace lmx::test
