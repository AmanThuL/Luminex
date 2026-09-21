//----------------------------------------------------------------------------------------------------------------------
/// @file LocalLightMath.h
/// @brief Declares the CPU mirror of Shaders/Common/LocalLights.slang's ComputePunctualLight.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Scene/SceneTables.h"
#include "Engine/Types/LocalLight.h"
#include <rojoRHI/Result.h>

#include <glm/vec3.hpp>

namespace lmx::render {

/// Builds `light`'s 64-byte GPU row: decodes strength and the cone term, and derives the world
/// bounding sphere docs/milestones/m7/m7.5.md's light table specifies. This is the only gate before
/// those values reach the GPU, so it fails with an RHI `InvalidDesc` error for any non-finite
/// `position`, `colour`, `intensity` or `direction` component, a nonpositive, non-finite or NaN
/// `range`, `innerCone >= outerCone`, `outerCone > 89deg`, or (for a spot) a zero direction; never
/// asserts. Disabled lights undergo the same validation and return a zero row.
rojoRHI::Result<LightRow> makeLightRow(const LocalLight& light);

/// The glTF/UE/Frostbite windowed inverse-square term `window(d) / max(d^2, (0.01 m)^2)`, with
/// `window(d) = saturate(1 - (d / range)^4)^2`; exactly zero for `distance >= range`.
float punctualAttenuation(float distance, float range);

/// `saturate(cosTheta * spotScale + spotOffset)^2`; a point light's stored scale 0 / offset 1
/// makes this identically 1, so one expression serves both light types.
float spotTerm(float cosTheta, float spotScale, float spotOffset);

/// Whether `worldPosition` lies inside `row`'s range and cone, ignoring the surface normal; a
/// direct-loop or clustering pass uses this to admit a light before any shading is evaluated.
bool lightReaches(const LightRow& row, glm::vec3 worldPosition);

/// CPU mirror of Shaders/Common/Lighting.slang's `ComputePunctualLight`: joins the same GGX/Smith/
/// Schlick BRDF core `ComputeDirectionalLight` uses with `row`'s distance attenuation and cone
/// term. Returns exact `glm::vec3(0)` through one ordered early-out when the surface is at or
/// beyond `row`'s range, the cone term is zero, or the surface normal faces away from the light;
/// N.L is saturated to [0, 1] before use, matching `ComputeDirectionalLight`.
glm::vec3 computePunctualLight(const LightRow& row, glm::vec3 position, glm::vec3 normal,
                               glm::vec3 toEye, glm::vec3 baseColour, glm::vec3 f0, float metallic,
                               float alpha);

} // namespace lmx::render
