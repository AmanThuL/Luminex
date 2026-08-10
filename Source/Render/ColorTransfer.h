//----------------------------------------------------------------------------------------------------------------------
/// @file ColorTransfer.h
/// @brief Provides renderer-local sRGB-to-linear transfer helpers.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>

namespace lmx::render {

/// The sRGB electro-optical transfer function (IEC 61966-2-1), for the one colour this layer is
/// handed in display space: the clear colour an editor colour picker writes. Everything else
/// reaching the renderer is already linear, because Engine decoded it at scene build.
///
/// It lives here rather than being borrowed from Engine because Render sits below Engine in the
/// dependency chain -- Engine includes Render headers, so the reverse would be a cycle.
constexpr float srgbToLinear(float encoded) {
    return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

/// Component-wise. The vec4 overload leaves alpha untouched: alpha is coverage, not colour, and
/// sRGB defines no transfer function for it.
inline glm::vec3 srgbToLinear(const glm::vec3& encoded) {
    return {srgbToLinear(encoded.x), srgbToLinear(encoded.y), srgbToLinear(encoded.z)};
}
/// Decodes RGB channels while preserving coverage alpha.
inline glm::vec4 srgbToLinear(const glm::vec4& encoded) {
    return {srgbToLinear(encoded.x), srgbToLinear(encoded.y), srgbToLinear(encoded.z), encoded.w};
}

} // namespace lmx::render
