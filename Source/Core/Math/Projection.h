//----------------------------------------------------------------------------------------------------------------------
/// @file Projection.h
/// @brief Declares the reversed-depth perspective projection and the orthographic fit to a sphere.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Sphere.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace lmx {

/// Returns a right-handed perspective projection with an infinite far plane and reversed [0, 1]
/// depth: a point on the near plane lands at depth 1, and depth falls toward 0 as the point
/// recedes, reaching it only at infinity. `fovY` is the full vertical field of view in radians;
/// `aspect` is width over height. Both `aspect` and `nearZ` must be positive.
inline glm::mat4 perspectiveReversedInfinite(float fovY, float aspect, float nearZ) {
    LMX_ASSERT(aspect > 0.0f, "perspectiveReversedInfinite: aspect must be positive");
    LMX_ASSERT(nearZ > 0.0f, "perspectiveReversedInfinite: nearZ must be positive");

    const float tanHalfFovY = std::tan(fovY * 0.5f);
    glm::mat4 projection{0.0f};
    projection[0][0] = 1.0f / (aspect * tanHalfFovY);
    projection[1][1] = 1.0f / tanHalfFovY;
    projection[2][3] = -1.0f;
    projection[3][2] = nearZ;
    return projection;
}

/// A view and an orthographic projection that together frame a volume.
struct OrthoFit {
    glm::mat4 view;       ///< World-to-view transform, right-handed, looking down -z.
    glm::mat4 projection; ///< View-to-clip orthographic projection with reversed [0, 1] depth.
};

/// Fits a right-handed orthographic view volume to `sphere`, looking along `direction`.
///
/// The eye sits two radii back from the centre along `direction` and looks at the centre, so the
/// volume spans one radius either side of the centre in x and y and runs from r to 3r in depth.
/// Depth is reversed: the near plane maps to 1 and the far plane to 0. `direction` need not be
/// normalised but must not be zero, and the radius must be positive. When `direction` is nearly
/// parallel to world +y, the view's up vector falls back to +z so the look-at basis stays defined.
inline OrthoFit fitOrthoToSphere(const Sphere& sphere, const glm::vec3& direction) {
    LMX_ASSERT(sphere.radius > 0.0f, "fitOrthoToSphere: the sphere's radius must be positive");
    LMX_ASSERT(glm::length(direction) > 0.0f,
               "fitOrthoToSphere: the direction must not be the zero vector");

    const glm::vec3 center = sphere.center;
    const float radius = sphere.radius;

    const glm::vec3 unitDirection = glm::normalize(direction);
    const glm::vec3 eye = center - 2.0f * radius * unitDirection;

    constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
    const glm::vec3 up = std::abs(glm::dot(unitDirection, kWorldUp)) > 0.999f
                             ? glm::vec3{0.0f, 0.0f, 1.0f}
                             : kWorldUp;
    const glm::mat4 view = glm::lookAtRH(eye, center, up);

    // The view looks down -z, so the positive near and far distances are taken from -centerView.z.
    // Handing orthoRH_ZO the far distance as its near and the near distance as its far is what
    // reverses the depth; x and y are unaffected.
    const glm::vec3 centerView = glm::vec3(view * glm::vec4(center, 1.0f));
    const float nearDistance = -centerView.z - radius;
    const float farDistance = -centerView.z + radius;
    const glm::mat4 projection =
        glm::orthoRH_ZO(centerView.x - radius, centerView.x + radius, centerView.y - radius,
                        centerView.y + radius, farDistance, nearDistance);
    return {.view = view, .projection = projection};
}

} // namespace lmx
