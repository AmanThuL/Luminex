//----------------------------------------------------------------------------------------------------------------------
/// @file Orientation.h
/// @brief Declares exact glTF quaternion searches and lossless directional-strength encoding.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <optional>

namespace lmx::asset {

/// glTF linear colour and power-of-two intensity; doubles retain every finite float strength.
struct EncodedStrength {
    glm::dvec3 colour{0.0}; ///< Linear RGB components in [0,1].
    double intensity = 1.0; ///< Finite power of two, at least the largest input component.
};

/// Rotates glTF's local -Z axis and normalizes the resulting world-space direction.
glm::vec3 directionForRotation(glm::quat rotation);
/// Returns a seed quaternion rotating local -Z to the supplied normalized world direction.
glm::quat rotationForDirection(glm::vec3 direction);
/// Returns a seed quaternion for the no-roll camera yaw/pitch convention, both in radians.
glm::quat rotationForCamera(float yaw, float pitch);
/// Decodes an object quaternion to XYZ degrees in the editor's Y * X * Z rotation order.
/// Uses double intermediates before the final float conversion to avoid losing degree preimages.
/// Preserves quaternion-derived zero signs; translation and signed or nonuniform scale stay
/// untouched.
glm::vec3 eulerDegreesForRotation(glm::quat rotation);
/// Searches at most ±4 float ULPs per Y * X * Z seed quaternion component.
/// A zero seed also searches its opposite signed zero, which nextafter skips.
/// Returns only a bitwise exact Euler decoding; invalid or unmatched degrees return nullopt.
std::optional<glm::quat> exactRotationForEulerDegrees(glm::vec3 eulerDegrees);
/// Adds the nearest full-turn offset using the camera rail's exact float arithmetic.
float unwrapYaw(float previous, float yaw);
/// Decodes yaw/pitch in radians, with yaw unwrapped relative to previousYaw.
glm::vec2 cameraAnglesForRotation(glm::quat rotation, float previousYaw);
/// Searches at most ±4 float ULPs per seed component and returns only an exactly decoding value.
/// Invalid or unmatched camera angles return nullopt; callers must never silently approximate.
std::optional<glm::quat> exactRotationForCamera(float yaw, float pitch, float previousYaw);
/// Searches at most ±4 float ULPs per seed component and returns only the exact input direction.
std::optional<glm::quat> exactRotationForDirection(glm::vec3 direction);
/// Losslessly encodes a finite nonnegative float RGB strength; invalid input asserts.
EncodedStrength encodeStrength(glm::vec3 strength);
/// Reconstructs the directional float strength by multiplying in double precision.
glm::vec3 decodeStrength(const EncodedStrength& strength);

} // namespace lmx::asset
