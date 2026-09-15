//----------------------------------------------------------------------------------------------------------------------
/// @file SelectionBounds.cpp
/// @brief Implements conservative world bounds and scale-aware selected-object framing.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/SelectionBounds.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lmx::app {
//======================================================================================================================
std::optional<render::Aabb> objectWorldBounds(const scene::Scene& scene,
                                              const scene::SceneObject& object) {
    const auto local = scene.meshBounds(object.mesh);
    return local ? render::transformAabb(object.modelMatrix(), *local) : std::nullopt;
}

//======================================================================================================================
std::optional<render::Aabb> selectedObjectBounds(const scene::Scene& scene,
                                                 const EditorSelection& selection) {
    if (selection.subject != EditorSubject::Object || selection.index >= scene.objects.size()) {
        return std::nullopt;
    }
    return objectWorldBounds(scene, scene.objects[selection.index]);
}

//======================================================================================================================
bool frameSelection(render::Camera& camera, const render::Aabb& bounds, float aspect) {
    if (!render::isValidAabb(bounds) || !std::isfinite(aspect) || aspect <= 0.0f ||
        !std::isfinite(camera.fovY) || camera.fovY <= 0.0f || camera.fovY >= 3.13f ||
        !std::isfinite(camera.yaw) || !std::isfinite(camera.pitch) ||
        std::abs(std::cos(camera.pitch)) < 1e-5f) {
        return false;
    }
    const glm::dvec3 center = (glm::dvec3(bounds.minimum) + glm::dvec3(bounds.maximum)) * 0.5;
    const double radius = std::max(glm::length(glm::dvec3(bounds.maximum) - center), 1e-6);
    const double halfVertical = static_cast<double>(camera.fovY) * 0.5;
    const double halfHorizontal = std::atan(std::tan(halfVertical) * aspect);
    const double distance = radius * 1.15 / std::sin(std::min(halfVertical, halfHorizontal));
    const glm::vec3 position(center - glm::dvec3(camera.forward()) * distance);
    if (!render::isFinite(position) || !std::isfinite(distance) ||
        distance > static_cast<double>(std::numeric_limits<float>::max()) * 0.5) {
        return false;
    }
    camera.position = position;
    camera.nearZ = static_cast<float>(std::max(radius * 0.01, 1e-8));
    camera.moveSpeed = static_cast<float>(radius);
    return true;
}

} // namespace lmx::app
