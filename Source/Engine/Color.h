#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>

namespace lmx::engine {

constexpr float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Component-wise. The vec4 overload leaves alpha untouched: alpha is coverage, not colour, and
// sRGB defines no transfer function for it.
inline glm::vec3 srgbToLinear(const glm::vec3& c) {
    return {srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z)};
}
inline glm::vec4 srgbToLinear(const glm::vec4& c) {
    return {srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z), c.w};
}

// The exact inverse of srgbToLinear -- same IEC 61966-2-1 piecewise curve Shaders/Encode.slang's
// linearToSrgbChannel uses, so a CPU-baked level and a shader-encoded pixel agree bit-for-bit up
// to float precision. Clamped first: c is expected in [0,1], and pow() of a negative base is
// undefined.
inline float linearToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

} // namespace lmx::engine
