//----------------------------------------------------------------------------------------------------------------------
/// @file Camera.cpp
/// @brief Implements free-fly camera movement and reversed-depth transforms.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Types/Camera.h"

#include "Core/Math/Projection.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace lmx::engine {

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
    // farZ is untouched by all of this. It stays on the Camera as scene data (SceneCamera carries
    // it, the editor round-trips it) and this projection simply does not read it -- see the note
    // on the field in Camera.h.
    return perspectiveReversedInfinite(fovY, aspect, nearZ);
}

} // namespace lmx::engine
