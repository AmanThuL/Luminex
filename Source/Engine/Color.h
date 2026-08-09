#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

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

} // namespace lmx::engine
