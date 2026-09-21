//----------------------------------------------------------------------------------------------------------------------
/// @file Scene.cpp
/// @brief Implements scene object transforms and scene cameras.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Scene/Scene.h"

#include "Core/Math/Transform.h"
#include "Engine/Types/Camera.h"

#include <glm/glm.hpp>

namespace lmx::engine {

//======================================================================================================================
glm::mat4 SceneObject::modelMatrix() const {
    return composeTransform({.position = position, .eulerDegrees = eulerDegrees, .scale = scale});
}

//======================================================================================================================
engine::Camera cameraFromScene(const SceneCamera& sceneCamera) {
    engine::Camera camera;
    camera.position = sceneCamera.position;
    camera.yaw = sceneCamera.yaw;
    camera.pitch = sceneCamera.pitch;
    camera.fovY = sceneCamera.fovY;
    camera.nearZ = sceneCamera.nearZ;
    camera.farZ = sceneCamera.farZ;
    return camera;
}

} // namespace lmx::engine
