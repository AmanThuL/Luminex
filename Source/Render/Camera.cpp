#include "Render/Camera.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace lmx::render {

//======================================================================================================================
glm::vec3 Camera::forward() const {
    const float cp = std::cos(pitch);
    return glm::normalize(glm::vec3{std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp});
}

//======================================================================================================================
glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), glm::vec3{0.0f, 1.0f, 0.0f}));
}

//======================================================================================================================
void Camera::move(const glm::vec3& localDelta) {
    position += right() * localDelta.x + glm::vec3{0.0f, 1.0f, 0.0f} * localDelta.y +
                forward() * localDelta.z;
}

//======================================================================================================================
void Camera::look(float yawDelta, float pitchDelta) {
    yaw += yawDelta;
    // Hard stop short of the poles: at ±π/2 forward() and world-up are parallel and right()
    // degenerates. 0.01 rad ≈ 0.6° of headroom.
    pitch = std::clamp(pitch + pitchDelta, -glm::half_pi<float>() + 0.01f,
                       glm::half_pi<float>() - 0.01f);
}

//======================================================================================================================
glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position, position + forward(), glm::vec3{0.0f, 1.0f, 0.0f});
}

//======================================================================================================================
glm::mat4 Camera::projectionMatrix(float aspect) const {
    // perspectiveRH_ZO: Metal clip-space depth is [0,1]. glm::perspective would silently
    // hand back the GL [-1,1] convention and cost half the depth-buffer precision.
    return glm::perspectiveRH_ZO(fovY, aspect, nearZ, farZ);
}

} // namespace lmx::render
