//----------------------------------------------------------------------------------------------------------------------
/// @file SceneSessionDocument.cpp
/// @brief Captures explicit saved camera requests independently from viewport navigation.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/SceneSession.h"

#include "Core/Math/Aabb.h"
#include "Engine/Asset/Document/Orientation.h"

#include "Core/Diagnostics/Assert.h"

#include <cmath>

namespace lmx::app {

//======================================================================================================================
rojoRHI::Result<void> SceneSession::setSceneCamera() {
    if (!m_loaded || m_measurementActive)
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                           "Saved camera requires a document and no active measurement"});
    // The document decodes yaw in [-pi, pi]; wrap the live camera with the same value so the view
    // is unchanged and the request stays saveable.
    if (std::isfinite(m_camera.yaw))
        m_camera.yaw = asset::unwrapYaw(0.0f, m_camera.yaw);
    const auto& camera = m_camera;
    if (!isFinite(camera.position) || !std::isfinite(camera.yaw) || !std::isfinite(camera.pitch) ||
        !std::isfinite(camera.fovY) || camera.fovY <= 0 || camera.fovY >= glm::pi<float>() ||
        !std::isfinite(camera.nearZ) || camera.nearZ <= 0 || !(camera.farZ > camera.nearZ))
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                           "Saved camera has an invalid pose or perspective lens"});
    auto& saved = m_documentStates.at(m_scene).sceneCamera;
    const auto& previous = saved ? *saved : scene().initialCamera;
    if (previous.position == camera.position && previous.yaw == camera.yaw &&
        previous.pitch == camera.pitch && previous.fovY == camera.fovY &&
        previous.nearZ == camera.nearZ && previous.farZ == camera.farZ)
        return {};
    saved = engine::SceneCamera{camera.position, camera.yaw,   camera.pitch,
                                camera.fovY,     camera.nearZ, camera.farZ};
    notifyPersistentEdit();
    return {};
}

//======================================================================================================================
void SceneSession::adoptDocumentCamera(const engine::SceneCamera& camera) {
    LMX_ASSERT(m_loaded, "saved camera adoption requires a loaded document");
    if (auto& saved = m_documentStates.at(m_scene).sceneCamera)
        saved = camera;
}

//======================================================================================================================
engine::SceneCamera SceneSession::authoredSceneCamera() const {
    LMX_ASSERT(m_loaded, "authored scene camera requires a loaded document");
    return documentState().sceneCamera.value_or(scene().initialCamera);
}

//======================================================================================================================
rojoRHI::Result<void> SceneSession::setSceneCamera(const engine::SceneCamera& requested) {
    if (!m_loaded || m_measurementActive)
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                           "Authored camera requires a document and no measurement"});
    auto camera = requested;
    if (!isFinite(camera.position) || !std::isfinite(camera.yaw) || !std::isfinite(camera.pitch) ||
        std::abs(camera.pitch) > glm::half_pi<float>() || !std::isfinite(camera.fovY) ||
        camera.fovY <= 0 || camera.fovY >= glm::pi<float>() || !std::isfinite(camera.nearZ) ||
        camera.nearZ <= 0 || !(camera.farZ > camera.nearZ))
        return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                                              "Authored camera has an invalid pose or lens"});
    camera.yaw = asset::unwrapYaw(0.0f, camera.yaw);
    const auto previous = authoredSceneCamera();
    if (previous.position == camera.position && previous.yaw == camera.yaw &&
        previous.pitch == camera.pitch && previous.fovY == camera.fovY &&
        previous.nearZ == camera.nearZ && previous.farZ == camera.farZ)
        return {};
    m_documentStates.at(m_scene).sceneCamera = camera;
    notifyPersistentEdit();
    return {};
}
} // namespace lmx::app
