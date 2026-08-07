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
    float farZ = 100.0f;
    float moveSpeed = 3.0f;

    glm::vec3 forward() const;
    glm::vec3 right() const;
    void move(const glm::vec3& localDelta);      // x=right, y=world-up, z=forward (pre-scaled)
    void look(float yawDelta, float pitchDelta); // radians; pitch clamped to ±(π/2 − 0.01)
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const; // perspectiveRH_ZO — Metal [0,1] depth
};

} // namespace lmx::render
