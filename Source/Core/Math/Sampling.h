//----------------------------------------------------------------------------------------------------------------------
/// @file Sampling.h
/// @brief Declares cube-map face/solid-angle helpers and GGX half-vector importance sampling.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lmx {

/// Returns the unnormalized direction for a cube face's `[-1, 1]` parameterization `(u, v)`.
/// Cube-face coordinates are also used for taps just outside a face, so a reprojected direction
/// need not point exactly at the face itself.
inline glm::vec3 cubeFaceDirection(uint32_t face, float u, float v) {
    switch (face) {
    case 0:
        return {1.0f, -v, -u};
    case 1:
        return {-1.0f, -v, u};
    case 2:
        return {u, 1.0f, v};
    case 3:
        return {u, -1.0f, -v};
    case 4:
        return {u, -v, 1.0f};
    default:
        return {-u, -v, -1.0f};
    }
}

/// Signed solid angle of the `[-1, x] x [-1, y]` corner region of a cube face, in the face's own
/// `[-1, 1]` parameterization. Differencing four of these gives one texel's solid angle.
inline float cubeAreaElement(float x, float y) {
    return std::atan2(x * y, std::sqrt(x * x + y * y + 1.0f));
}

/// Returns the solid angle subtended by one texel of a `faceSize`-square cube face.
inline float cubeTexelSolidAngle(uint32_t x, uint32_t y, uint32_t faceSize) {
    const float inverse = 1.0f / static_cast<float>(faceSize);
    const float u = 2.0f * (static_cast<float>(x) + 0.5f) * inverse - 1.0f;
    const float v = 2.0f * (static_cast<float>(y) + 0.5f) * inverse - 1.0f;
    return cubeAreaElement(u - inverse, v - inverse) - cubeAreaElement(u - inverse, v + inverse) -
           cubeAreaElement(u + inverse, v - inverse) + cubeAreaElement(u + inverse, v + inverse);
}

/// Returns a GGX half vector drawn from the NDF, in a tangent frame whose normal is +Z.
inline glm::vec3 sampleGgxHalfVector(const glm::vec2& xi, float alpha) {
    const float alpha2 = alpha * alpha;
    const float phi = 2.0f * glm::pi<float>() * xi.x;
    const float cosTheta =
        std::sqrt(std::max(0.0f, (1.0f - xi.y) / (1.0f + (alpha2 - 1.0f) * xi.y)));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    return {sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
}

/// Returns the same half vector rotated into the frame of `normal`. Which orthonormal basis the
/// frame uses only rotates the sample pattern about the normal, so the arbitrary `up` choice
/// below is free -- it just has to stay away from being parallel to the normal.
inline glm::vec3 sampleGgxHalfVector(const glm::vec2& xi, float alpha, const glm::vec3& normal) {
    const glm::vec3 local = sampleGgxHalfVector(xi, alpha);
    const glm::vec3 up =
        std::abs(normal.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 tangentX = glm::normalize(glm::cross(up, normal));
    const glm::vec3 tangentY = glm::cross(normal, tangentX);
    return glm::normalize(tangentX * local.x + tangentY * local.y + normal * local.z);
}

} // namespace lmx
