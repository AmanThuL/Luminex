//----------------------------------------------------------------------------------------------------------------------
/// @file SceneSessionDocument.cpp
/// @brief Captures explicit saved camera requests independently from viewport navigation.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/SceneSession.h"

#include "Core/Math/Aabb.h"

#include <cmath>

namespace lmx::app {

//======================================================================================================================
rojoRHI::Result<void> SceneSession::setSceneCamera() {
    if (!m_loaded || m_measurementActive)
        return std::unexpected(
            rojoRHI::Error{rojoRHI::ErrorCode::InvalidDesc,
                           "Saved camera requires a document and no active measurement"});
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
} // namespace lmx::app
