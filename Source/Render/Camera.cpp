#include "Render/Camera.h"

#include "Core/Assert.h"

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
    LMX_ASSERT(aspect > 0.0f, "Camera::projectionMatrix: aspect must be positive");
    LMX_ASSERT(nearZ > 0.0f, "Camera::projectionMatrix: nearZ must be positive -- the reversed "
                             "projection divides by the eye distance and scales by nearZ");

    // Reversed infinite-far perspective, written out rather than assembled from glm because glm
    // has no such factory and the algebra is four numbers.
    //
    // Start from the right-handed [0,1] perspective glm does have: clip.w = -z_view, and clip.z
    // interpolates the near and far planes. Drop the far plane entirely (let it go to infinity)
    // and reverse the ends, so that
    //
    //     depth = clip.z / clip.w = nearZ / (-z_view)
    //
    // which is 1 at z_view = -nearZ and falls toward 0 as the point recedes, reaching it only in
    // the limit. Matching that against the row form gives clip.z = nearZ * 1 -- a constant, so
    // m[2][2] = 0 and m[3][2] = nearZ -- while m[2][3] = -1 keeps clip.w = -z_view.
    //
    // Reversed rather than conventional because depth is stored in a float: the exponent packs
    // its precision around 0, and 0 is where the reciprocal spends nearly all of the distance.
    // Conventional [0,1] depth puts the float's dense end at the near plane, where the reciprocal
    // is already changing fast, and starves everything beyond it.
    //
    // farZ is untouched by all of this. It stays on the Camera as scene data (SceneCamera carries
    // it, the editor round-trips it) and this projection simply does not read it -- see the note
    // on the field in Camera.h.
    const float tanHalfFovY = std::tan(fovY * 0.5f);
    glm::mat4 projection{0.0f};
    projection[0][0] = 1.0f / (aspect * tanHalfFovY);
    projection[1][1] = 1.0f / tanHalfFovY;
    projection[2][3] = -1.0f;
    projection[3][2] = nearZ;
    return projection;
}

} // namespace lmx::render
