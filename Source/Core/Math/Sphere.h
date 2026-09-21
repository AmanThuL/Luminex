//----------------------------------------------------------------------------------------------------------------------
/// @file Sphere.h
/// @brief Declares a bounding sphere and its conversions and intersection with an Aabb.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Math/Aabb.h"

#include <glm/glm.hpp>

namespace lmx {

/// A sphere in the coordinate space of its owner.
struct Sphere {
    glm::vec3 center{0.0f}; ///< Sphere centre.
    float radius = 0.0f;    ///< Sphere radius.
};

/// Returns the sphere centred on the box's midpoint, with a radius reaching every corner.
inline Sphere boundingSphere(const Aabb& box) {
    const glm::vec3 center = (box.minimum + box.maximum) * 0.5f;
    return Sphere{center, glm::length(box.maximum - center)};
}

/// Packs a sphere's centre and radius into a vec4, radius in `w`.
inline glm::vec4 toVec4(const Sphere& sphere) {
    return glm::vec4(sphere.center, sphere.radius);
}

/// Returns whether the box and the sphere touch or overlap; a shared boundary point counts as
/// intersecting, so a sphere resting exactly on a face is not disjoint.
inline bool intersects(const Aabb& box, const Sphere& sphere) {
    const glm::vec3 closest = glm::clamp(sphere.center, box.minimum, box.maximum);
    const glm::vec3 delta = closest - sphere.center;
    return glm::dot(delta, delta) <= sphere.radius * sphere.radius;
}

} // namespace lmx
