//----------------------------------------------------------------------------------------------------------------------
/// @file LocalLightMath.cpp
/// @brief Implements the CPU mirror of Shaders/Modules/LocalLights.slang's ComputePunctualLight.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/LocalLightMath.h"

#include "Render/Bounds.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lmx::render {

namespace {

// Mirrors Shaders/Modules/Lighting.slang's constant of the same name.
constexpr float kPi = std::numbers::pi_v<float>;

// The frozen bound-radius inflation factor docs/milestones/m7.5.md's light table fixes.
constexpr float kBoundRadiusInflation = 1.0f + 1.0f / 1024.0f; // 1 + 2^-10

// A spot's tight cone sphere applies only up to this half-angle; docs/milestones/m7.5.md's light
// table falls back to the range sphere beyond it.
constexpr float kTightConeSphereLimit = std::numbers::pi_v<float> / 4.0f; // 45 degrees

//======================================================================================================================
// Mirrors Shaders/Modules/Lighting.slang's D_GGX.
float dGgx(float noh, float alpha) {
    const float a2 = alpha * alpha;
    const float d = noh * noh * (a2 - 1.0f) + 1.0f;
    return a2 / std::max(kPi * d * d, 1e-9f);
}

//======================================================================================================================
// Mirrors Shaders/Modules/Lighting.slang's V_SmithHeightCorrelated.
float vSmithHeightCorrelated(float nov, float nol, float alpha) {
    const float a2 = alpha * alpha;
    const float view = nov * std::sqrt(nol * nol * (1.0f - a2) + a2);
    const float light = nol * std::sqrt(nov * nov * (1.0f - a2) + a2);
    return 0.5f / std::max(view + light, 1e-5f);
}

//======================================================================================================================
// Mirrors Shaders/Modules/Lighting.slang's F_Schlick.
glm::vec3 fSchlick(const glm::vec3& f0, float voh) {
    const float f = 1.0f - voh;
    const float f5 = f * f * f * f * f;
    return f0 + (glm::vec3(1.0f) - f0) * f5;
}

} // namespace

//======================================================================================================================
rojoRHI::Result<LightRow> makeLightRow(const LocalLight& light) {
    // The only gate before these values reach the GPU row: a NaN or infinite component would
    // otherwise upload silently and corrupt shading or the bounding sphere.
    if (!isFinite(light.position) || !isFinite(light.colour) || !std::isfinite(light.intensity) ||
        !isFinite(light.direction)) {
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                       "makeLightRow: position, colour, intensity and direction must be finite"});
    }
    if (!std::isfinite(light.range) || light.range <= 0.0f) {
        return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                                          "makeLightRow: range must be finite and positive"});
    }

    LightRow row{};
    row.position = light.position;
    row.range = light.range;
    row.strength = light.colour * light.intensity;
    row.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    row.spotScale = 0.0f;
    row.spotOffset = 1.0f;

    if (light.type == LocalLightType::Spot) {
        if (!(light.innerCone >= 0.0f) || !(light.innerCone < light.outerCone)) {
            return std::unexpected(
                rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                           "makeLightRow: a spot needs 0 <= innerCone < outerCone"});
        }
        constexpr float kMaxOuterCone = 89.0f * std::numbers::pi_v<float> / 180.0f;
        if (!(light.outerCone <= kMaxOuterCone)) {
            return std::unexpected(rojoRHI::Error{
                rojoRHI::ErrorCode::InvalidDesc, "makeLightRow: outerCone must be at most 89 degrees"});
        }
        const float directionLength = glm::length(light.direction);
        if (!(directionLength > 1e-8f)) {
            return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                                              "makeLightRow: a spot needs a nonzero direction"});
        }
        row.direction = light.direction / directionLength;

        const float cosInner = std::cos(light.innerCone);
        const float cosOuter = std::cos(light.outerCone);
        row.spotScale = 1.0f / std::max(cosInner - cosOuter, 1e-4f);
        row.spotOffset = -cosOuter * row.spotScale;

        if (light.outerCone <= kTightConeSphereLimit) {
            const float radius = light.range / (2.0f * cosOuter);
            row.boundCentre = light.position + row.direction * radius;
            row.boundRadius = radius * kBoundRadiusInflation;
        } else {
            row.boundCentre = light.position;
            row.boundRadius = light.range * kBoundRadiusInflation;
        }
    } else {
        row.boundCentre = light.position;
        row.boundRadius = light.range * kBoundRadiusInflation;
    }

    return light.enabled ? row : LightRow{};
}

//======================================================================================================================
float punctualAttenuation(float distance, float range) {
    if (distance >= range) {
        return 0.0f;
    }
    constexpr float kDistanceFloor = 0.01f;
    const float ratio = distance / range;
    const float ratio4 = ratio * ratio * ratio * ratio;
    const float window = std::clamp(1.0f - ratio4, 0.0f, 1.0f);
    return (window * window) / std::max(distance * distance, kDistanceFloor * kDistanceFloor);
}

//======================================================================================================================
float spotTerm(float cosTheta, float spotScale, float spotOffset) {
    const float term = std::clamp(cosTheta * spotScale + spotOffset, 0.0f, 1.0f);
    return term * term;
}

//======================================================================================================================
bool lightReaches(const LightRow& row, glm::vec3 worldPosition) {
    const glm::vec3 toLight = row.position - worldPosition;
    const float distance = glm::length(toLight);
    if (distance >= row.range) {
        return false;
    }
    const glm::vec3 lightVec = toLight / std::max(distance, 1e-8f);
    const float cosTheta = glm::dot(-lightVec, row.direction);
    return spotTerm(cosTheta, row.spotScale, row.spotOffset) > 0.0f;
}

//======================================================================================================================
glm::vec3 computePunctualLight(const LightRow& row, glm::vec3 position, glm::vec3 normal,
                               glm::vec3 toEye, glm::vec3 baseColour, glm::vec3 f0, float metallic,
                               float alpha) {
    const glm::vec3 toLight = row.position - position;
    const float distance = glm::length(toLight);
    if (distance >= row.range) {
        return glm::vec3(0.0f);
    }
    const glm::vec3 lightVec = toLight / std::max(distance, 1e-8f);

    const float cosTheta = glm::dot(-lightVec, row.direction);
    const float cone = spotTerm(cosTheta, row.spotScale, row.spotOffset);
    if (cone <= 0.0f) {
        return glm::vec3(0.0f);
    }

    // Shaders/Modules/Lighting.slang's ComputeDirectionalLight saturates N.L before use; mirrored
    // here so a non-unit shading normal (common off interpolated vertex normals) matches the GPU
    // exactly rather than only flooring at zero.
    const float nol = std::clamp(glm::dot(normal, lightVec), 0.0f, 1.0f);
    if (nol <= 0.0f) {
        return glm::vec3(0.0f);
    }

    const float nov = std::max(glm::dot(normal, toEye), 1e-4f);
    const glm::vec3 halfVec = glm::normalize(toEye + lightVec);
    const float noh = std::clamp(glm::dot(normal, halfVec), 0.0f, 1.0f);
    const float voh = std::clamp(glm::dot(toEye, halfVec), 0.0f, 1.0f);

    const glm::vec3 fresnel = fSchlick(f0, voh);
    const glm::vec3 diffuse = (glm::vec3(1.0f) - fresnel) * (1.0f - metallic) * baseColour / kPi;
    const glm::vec3 specular = dGgx(noh, alpha) * vSmithHeightCorrelated(nov, nol, alpha) * fresnel;

    const float attenuation = punctualAttenuation(distance, row.range);
    return (diffuse + specular) * nol * row.strength * attenuation * cone;
}

} // namespace lmx::render
