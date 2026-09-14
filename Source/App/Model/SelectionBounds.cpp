//----------------------------------------------------------------------------------------------------------------------
/// @file SelectionBounds.cpp
/// @brief Implements conservative world bounds and scale-aware selected-object framing.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/SelectionBounds.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lmx::app {
namespace {

//======================================================================================================================
bool finiteVector(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

//======================================================================================================================
bool validBounds(const scene::ObjectBounds& bounds) {
    return finiteVector(bounds.minimum) && finiteVector(bounds.maximum) &&
           glm::all(glm::lessThanEqual(bounds.minimum, bounds.maximum));
}

} // namespace

//======================================================================================================================
std::optional<scene::ObjectBounds> objectWorldBounds(const scene::SceneObject& object) {
    if (!object.localBounds || !validBounds(*object.localBounds)) {
        return std::nullopt;
    }
    const glm::mat4 transform = object.modelMatrix();
    scene::ObjectBounds bounds{.minimum = glm::vec3(std::numeric_limits<float>::max()),
                               .maximum = glm::vec3(std::numeric_limits<float>::lowest())};
    for (unsigned corner = 0; corner < 8; ++corner) {
        const glm::vec3 local(
            (corner & 1) ? object.localBounds->maximum.x : object.localBounds->minimum.x,
            (corner & 2) ? object.localBounds->maximum.y : object.localBounds->minimum.y,
            (corner & 4) ? object.localBounds->maximum.z : object.localBounds->minimum.z);
        const glm::vec3 world(transform * glm::vec4(local, 1.0f));
        if (!finiteVector(world)) {
            return std::nullopt;
        }
        bounds.minimum = glm::min(bounds.minimum, world);
        bounds.maximum = glm::max(bounds.maximum, world);
    }
    return bounds;
}

//======================================================================================================================
std::optional<scene::ObjectBounds> selectedObjectBounds(const scene::Scene& scene,
                                                        const EditorSelection& selection) {
    if (selection.subject != EditorSubject::Object || selection.index >= scene.objects.size()) {
        return std::nullopt;
    }
    return objectWorldBounds(scene.objects[selection.index]);
}

//======================================================================================================================
bool frameSelection(render::Camera& camera, const scene::ObjectBounds& bounds, float aspect) {
    if (!validBounds(bounds) || !std::isfinite(aspect) || aspect <= 0.0f ||
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
    if (!finiteVector(position) || !std::isfinite(distance) ||
        distance > static_cast<double>(std::numeric_limits<float>::max()) * 0.5) {
        return false;
    }
    camera.position = position;
    camera.nearZ = static_cast<float>(std::max(radius * 0.01, 1e-8));
    camera.moveSpeed = static_cast<float>(radius);
    return true;
}

} // namespace lmx::app
