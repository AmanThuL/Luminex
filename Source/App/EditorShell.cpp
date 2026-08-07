#include "App/EditorShell.h"

#include <glm/gtc/matrix_transform.hpp>

#include <utility>

namespace lmx::app {

namespace {

// The one animation M2 has. Slow enough that a screenshot taken at any moment still reads as a
// cube rather than as motion blur in the reader's head.
constexpr float kSpinDegreesPerSecond = 45.0f;

// Half-extent of the ground plane. Large enough that its edge falls outside the default camera's
// frustum, so the scene reads as a floor rather than as a fourth floating object.
constexpr float kGroundHalfExtent = 5.0f;

// Cubes sit on the plane: makeCube() is a unit cube centred on its own origin, so +0.5 in y puts
// its bottom face exactly on y = 0.
constexpr float kCubeCenterY = 0.5f;
constexpr float kCubeSpacingX = 1.5f;

} // namespace

glm::mat4 SceneObject::modelMatrix(float timeSeconds) const {
    glm::mat4 model = glm::translate(glm::mat4{1.0f}, position);
    if (rotating) {
        model = glm::rotate(model, glm::radians(kSpinDegreesPerSecond * timeSeconds),
                            glm::vec3{0.0f, 1.0f, 0.0f});
    }
    // Y then X then Z, applied in that order to the object -- the convention the Inspector's
    // three drag fields are labelled against. Composed with glm::rotate rather than one of glm's
    // euler helpers so the order is readable here rather than in a header somewhere.
    model = glm::rotate(model, glm::radians(eulerDegrees.y), glm::vec3{0.0f, 1.0f, 0.0f});
    model = glm::rotate(model, glm::radians(eulerDegrees.x), glm::vec3{1.0f, 0.0f, 0.0f});
    model = glm::rotate(model, glm::radians(eulerDegrees.z), glm::vec3{0.0f, 0.0f, 1.0f});
    return glm::scale(model, glm::vec3{scale});
}

rhi::Result<SceneMeshes> createSceneMeshes(rhi::Device& device) {
    SceneMeshes meshes;
    auto cube = render::createMesh(device, render::makeCube(), "lmx.app.cube");
    if (!cube) {
        return std::unexpected(cube.error());
    }
    meshes.cube = std::move(*cube);
    auto plane =
        render::createMesh(device, render::makePlane(kGroundHalfExtent), "lmx.app.groundPlane");
    if (!plane) {
        return std::unexpected(plane.error());
    }
    meshes.plane = std::move(*plane);
    return meshes;
}

std::vector<SceneObject> makeDefaultScene(const render::Mesh& cube, const render::Mesh& plane) {
    // The plane's own vertex colour is already light gray (0.8); the base colour only takes it
    // down far enough that the lit cubes read as the brighter things in the frame.
    return {
        SceneObject{.name = "Ground", .baseColor = {0.7f, 0.7f, 0.72f, 1.0f}, .mesh = &plane},
        SceneObject{.name = "Red",
                    .position = {-kCubeSpacingX, kCubeCenterY, 0.0f},
                    .baseColor = {0.9f, 0.2f, 0.2f, 1.0f},
                    .mesh = &cube},
        SceneObject{.name = "Gold",
                    .position = {0.0f, kCubeCenterY, 0.0f},
                    .baseColor = {0.9f, 0.7f, 0.2f, 1.0f},
                    .mesh = &cube,
                    .rotating = true},
        SceneObject{.name = "Blue",
                    .position = {kCubeSpacingX, kCubeCenterY, 0.0f},
                    .baseColor = {0.2f, 0.4f, 0.9f, 1.0f},
                    .mesh = &cube},
    };
}

render::Camera makeDefaultCamera() {
    render::Camera camera;
    // Set explicitly rather than left to the class defaults: this pose is a property of the
    // *scene* above -- back far enough for the outer cubes, high enough and tilted down far
    // enough for the plane to read as ground.
    camera.position = {0.0f, 2.5f, 7.0f};
    camera.yaw = 0.0f;
    camera.pitch = -0.25f;
    return camera;
}

void makeDrawItems(std::span<const SceneObject> scene, float timeSeconds,
                   std::vector<render::DrawItem>& out) {
    out.clear();
    out.reserve(scene.size());
    for (const SceneObject& object : scene) {
        if (object.mesh == nullptr) {
            continue;
        }
        out.push_back({.mesh = object.mesh,
                       .model = object.modelMatrix(timeSeconds),
                       .baseColor = object.baseColor});
    }
}

} // namespace lmx::app
