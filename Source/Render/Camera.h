#pragma once
#include <glm/glm.hpp>

namespace lmx::render {

// Free-fly camera: yaw/pitch orientation, no roll. yaw 0 faces -Z, positive yaw turns toward
// +X; pitch 0 is level, positive pitch looks up; both in radians.
class Camera {
public:
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fovY = glm::radians(60.0f);
    float nearZ = 0.1f;
    // Scene data, not a projection parameter: projectionMatrix is reversed with an *infinite*
    // far plane and never reads this. It stays because a scene authors it (engine::SceneCamera
    // carries it, the editor round-trips it) and because a caller that wants a distance to cull
    // or fit against has nowhere else to read one from.
    float farZ = 100.0f;
    float moveSpeed = 3.0f;

    glm::vec3 forward() const;
    glm::vec3 right() const;
    void move(const glm::vec3& localDelta);      // x=right, y=world-up, z=forward (pre-scaled)
    void look(float yawDelta, float pitchDelta); // radians; pitch clamped to ±(π/2 − 0.01)
    glm::mat4 viewMatrix() const;
    // Reversed infinite-far perspective for Metal's [0,1] clip depth: the near plane maps to 1
    // and depth falls toward 0 with distance, never reaching it. Derived in Camera.cpp.
    glm::mat4 projectionMatrix(float aspect) const;
};

} // namespace lmx::render
