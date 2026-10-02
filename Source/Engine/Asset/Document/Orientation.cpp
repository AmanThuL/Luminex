//----------------------------------------------------------------------------------------------------------------------
/// @file Orientation.cpp
/// @brief Implements exact quaternion neighbourhood searches and directional-strength coding.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Document/Orientation.h"

#include "Core/Diagnostics/Assert.h"

#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace lmx::asset {
namespace {

//======================================================================================================================
bool sameBits(float a, float b) {
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}

//======================================================================================================================
template <typename Predicate>
std::optional<glm::quat> search(glm::quat seed, Predicate&& matches) {
    if (matches(seed))
        return seed;
    std::array<std::array<float, 9>, 4> neighbours;
    for (int component = 0; component < 4; ++component) {
        neighbours[component][0] = seed[component];
        float lower = seed[component];
        float upper = seed[component];
        for (int distance = 1; distance <= 4; ++distance) {
            lower = std::nextafter(lower, -std::numeric_limits<float>::infinity());
            upper = std::nextafter(upper, std::numeric_limits<float>::infinity());
            neighbours[component][distance * 2 - 1] = lower;
            neighbours[component][distance * 2] = upper;
        }
    }
    for (const float x : neighbours[0])
        for (const float y : neighbours[1])
            for (const float z : neighbours[2])
                for (const float w : neighbours[3]) {
                    const glm::quat candidate(w, x, y, z);
                    if (matches(candidate))
                        return candidate;
                }
    return std::nullopt;
}

} // namespace

//======================================================================================================================
glm::vec3 directionForRotation(glm::quat rotation) {
    return glm::normalize(rotation * glm::vec3(0.0f, 0.0f, -1.0f));
}

//======================================================================================================================
glm::quat rotationForDirection(glm::vec3 direction) {
    if (direction.x == 0.0f && direction.y == 0.0f && direction.z == 1.0f)
        return glm::quat(0.0f, 0.0f, 1.0f, 0.0f);
    if (direction.z > 0.0f) {
        const float crossLength = std::hypot(direction.x, direction.y);
        const float sineHalf = std::sqrt((1.0f + direction.z) * 0.5f);
        return glm::quat(crossLength / (2.0f * sineHalf), direction.y * (sineHalf / crossLength),
                         -direction.x * (sineHalf / crossLength), 0.0f);
    }
    return glm::normalize(glm::quat(1.0f - direction.z, direction.y, -direction.x, 0.0f));
}

//======================================================================================================================
glm::quat rotationForCamera(float yaw, float pitch) {
    return glm::angleAxis(-yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
           glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
}

//======================================================================================================================
glm::vec3 eulerDegreesForRotation(glm::quat rotation) {
    float yaw, pitch, roll;
    glm::extractEulerAngleYXZ(glm::mat4_cast(rotation), yaw, pitch, roll);
    auto degrees = glm::degrees(glm::vec3(pitch, yaw, roll));
    for (int k = 0; k < 3; ++k)
        if (degrees[k] == 0.0f)
            degrees[k] = 0.0f;
    return degrees;
}

//======================================================================================================================
float unwrapYaw(float previous, float yaw) {
    return previous + std::remainder(yaw - previous, glm::two_pi<float>());
}

//======================================================================================================================
glm::vec2 cameraAnglesForRotation(glm::quat rotation, float previousYaw) {
    const auto direction = directionForRotation(rotation);
    return {unwrapYaw(previousYaw, std::atan2(direction.x, -direction.z)),
            std::asin(std::clamp(direction.y, -1.0f, 1.0f))};
}

//======================================================================================================================
std::optional<glm::quat> exactRotationForCamera(float yaw, float pitch, float previousYaw) {
    if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(previousYaw) ||
        std::abs(pitch) > glm::half_pi<float>())
        return std::nullopt;
    return search(rotationForCamera(yaw, pitch), [=](glm::quat candidate) {
        const auto angles = cameraAnglesForRotation(candidate, previousYaw);
        return sameBits(angles.x, yaw) && sameBits(angles.y, pitch);
    });
}

//======================================================================================================================
std::optional<glm::quat> exactRotationForDirection(glm::vec3 direction) {
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        std::abs(glm::dot(direction, direction) - 1.0f) > 1e-5f)
        return std::nullopt;
    return search(rotationForDirection(direction), [=](glm::quat candidate) {
        const auto decoded = directionForRotation(candidate);
        return sameBits(decoded.x, direction.x) && sameBits(decoded.y, direction.y) &&
               sameBits(decoded.z, direction.z);
    });
}

//======================================================================================================================
EncodedStrength encodeStrength(glm::vec3 strength) {
    for (int component = 0; component < 3; ++component)
        LMX_ASSERT(std::isfinite(strength[component]) && strength[component] >= 0.0f,
                   "Directional strength must be finite and nonnegative");
    const double largest = std::max({double(strength.x), double(strength.y), double(strength.z)});
    int exponent = 0;
    const double mantissa = std::frexp(largest, &exponent);
    const double intensity = largest == 0.0 ? 1.0 : std::ldexp(1.0, exponent - (mantissa == 0.5));
    return {glm::dvec3(strength) / intensity, intensity};
}

//======================================================================================================================
glm::vec3 decodeStrength(const EncodedStrength& strength) {
    return glm::vec3(strength.colour * strength.intensity);
}

} // namespace lmx::asset
