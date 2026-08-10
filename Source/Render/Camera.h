//----------------------------------------------------------------------------------------------------------------------
/// @file Camera.h
/// @brief Declares the free-fly camera and its view and projection transforms.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <glm/glm.hpp>

namespace lmx::render {

/// Free-fly camera: yaw/pitch orientation, no roll. yaw 0 faces -Z, positive yaw turns toward
/// +X; pitch 0 is level, positive pitch looks up; both in radians.
class Camera {
public:
    /// Camera position in right-handed world space.
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;                 ///< Horizontal look angle in radians.
    float pitch = 0.0f;               ///< Vertical look angle in radians.
    float fovY = glm::radians(60.0f); ///< Vertical field of view in radians.
    float nearZ = 0.1f;               ///< Positive near-plane distance in world units.
    /// Scene data, not a projection parameter: projectionMatrix is reversed with an *infinite*
    /// far plane and never reads this. It stays because a scene authors it (engine::SceneCamera
    /// carries it, the editor round-trips it) and because a caller that wants a distance to cull
    /// or fit against has nowhere else to read one from.
    float farZ = 100.0f;
    float moveSpeed = 3.0f; ///< Translation speed in world units per second.

    /// Returns the normalized world-space look direction.
    glm::vec3 forward() const;
    /// Returns the normalized world-space right direction.
    glm::vec3 right() const;
    /// Moves by a pre-scaled local delta: x is right, y world-up, and z forward.
    void move(const glm::vec3& localDelta);
    /// Applies radian look deltas and clamps pitch to ±(π/2 − 0.01).
    void look(float yawDelta, float pitchDelta);
    /// Builds the right-handed world-to-view transform.
    glm::mat4 viewMatrix() const;
    /// Reversed infinite-far perspective for Metal's [0,1] clip depth: the near plane maps to 1
    /// and depth falls toward 0 with distance, never reaching it. Derived in Camera.cpp.
    glm::mat4 projectionMatrix(float aspect) const;
};

} // namespace lmx::render
