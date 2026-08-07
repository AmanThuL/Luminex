#pragma once
#include "Render/Camera.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"

#include <glm/glm.hpp>

#include <span>
#include <string>
#include <vector>

namespace lmx::app {

// One editable thing in the scene. Deliberately a flat struct with public fields and no scene
// graph: the Inspector edits it in place, `makeDrawItems` turns it into an
// `lmx::render::DrawItem`, and that is the entire model M2 has (ADR 0004, spec §3).
struct SceneObject {
    std::string name; // Inspector display
    glm::vec3 position{0.0f};
    glm::vec3 eulerDegrees{0.0f};
    float scale = 1.0f;
    glm::vec4 baseColor{1.0f};
    const render::Mesh* mesh = nullptr; // non-owning; the meshes outlive the scene
    bool rotating = false;              // spins about +Y at 45 deg/s when set

    // Composed T * R_spin * R_euler * S, uniform scale only -- Mesh.slang transforms normals by
    // the model matrix's upper 3x3, which is only a valid normal transform without shear.
    glm::mat4 modelMatrix(float timeSeconds) const;
};

// The meshes `makeDefaultScene` points at, created together: their proportions (a ground plane
// wide enough to fall outside the default frustum, unit cubes standing on it) are a property of
// the pair, not of either one.
struct SceneMeshes {
    render::Mesh cube;
    render::Mesh plane;
};
rhi::Result<SceneMeshes> createSceneMeshes(rhi::Device& device);

// The M2 scene: a gray ground plane and three lit cubes, the middle one spinning. Shared by the
// editor and the `--screenshot` path so the screenshot is evidence about the app rather than
// about a second scene that happens to look similar (M1's TriangleAssets rule).
std::vector<SceneObject> makeDefaultScene(const render::Mesh& cube, const render::Mesh& plane);

// The pose `makeDefaultScene` was composed for: back and above the origin, tilted down far
// enough to frame the plane and all three cubes.
render::Camera makeDefaultCamera();

// Refills `out` with this frame's draw list. Takes the destination by reference rather than
// returning a vector because the editor calls it every frame; the screenshot path passes a
// local. Objects without a mesh are skipped -- DrawItem::mesh may not be null.
void makeDrawItems(std::span<const SceneObject> scene, float timeSeconds,
                   std::vector<render::DrawItem>& out);

} // namespace lmx::app
