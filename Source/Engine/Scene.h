//----------------------------------------------------------------------------------------------------------------------
/// @file Scene.h
/// @brief Declares scene data, transforms, views, and scene loaders.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset.h"
#include "RHI/RHI.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lmx::engine {

/// Editable scene instance referencing one mesh and material.
struct SceneObject {
    std::string name;            ///< User-facing instance name.
    glm::vec3 position{0.f};     ///< World-space translation.
    glm::vec3 eulerDegrees{0.f}; ///< XYZ Euler rotation in degrees.
    glm::vec3 scale{1.f};        ///< Per-axis object scale.
    uint32_t meshIndex = 0;      ///< Index into `Scene::meshes`.
    uint32_t materialIndex = 0;  ///< Index into `Scene::materials`.

    /// Builds the object's model matrix from its authored transform fields.
    glm::mat4 modelMatrix() const;
};

/// What decomposeTransform below extracts from a general 4x4.
struct DecomposedTransform {
    glm::vec3 position{0.f};     ///< Extracted translation.
    glm::vec3 eulerDegrees{0.f}; ///< Extracted XYZ Euler rotation in degrees.
    glm::vec3 scale{1.f};        ///< Extracted per-axis scale.
};

/// Extracts translation, Euler rotation, and scale when `world` is decomposable.
std::optional<DecomposedTransform> decomposeTransform(const glm::mat4& world);

/// Authored initial camera pose copied into a renderer camera at scene activation.
struct SceneCamera {
    glm::vec3 position;                  ///< Initial world-space position.
    float yaw, pitch, fovY, nearZ, farZ; ///< Radian angles and positive view distances.
};

/// Owns renderable scene resources, instances, lighting, and initial view state.
class Scene {
public:
    std::string name;                                    ///< User-facing scene name.
    std::vector<render::Mesh> meshes;                    ///< GPU meshes referenced by objects.
    std::vector<std::unique_ptr<rhi::Texture>> textures; ///< Material texture ownership.
    std::vector<render::Material> materials;             ///< texture pointers reach into `textures`
    std::vector<SceneObject> objects;                    ///< Editable draw instances.
    render::DirectionalLight lights[3];                  ///< Fixed-size analytic light set.
    glm::vec4 boundingSphere{0.f};            ///< World-space center in xyz and radius in w.
    render::Mesh skySphere;                   ///< Geometry used by the sky pass.
    std::unique_ptr<rhi::Texture> skyCubemap; ///< Authored linear-radiance environment.
    /// Image-based lighting generated from the same authored sky radiance skyCubemap carries
    /// (Engine/Ibl.h): a cosine-convolved irradiance cube, a GGX-prefiltered radiance chain, and
    /// the split-sum DFG table. Published together with the sky by the scene-build path, so a scene
    /// that has a skyCubemap has all three.
    std::unique_ptr<rhi::Texture> irradianceMap;     ///< Diffuse irradiance cubemap.
    std::unique_ptr<rhi::Texture> prefilteredEnvMap; ///< GGX-prefiltered environment chain.
    std::unique_ptr<rhi::Texture> dfgLut;            ///< Split-sum material response lookup table.
    SceneCamera initialCamera{}; ///< Camera pose restored when the scene becomes active.

    /// Fills `items` (cleared first, one DrawItem per object, in object order) and returns the
    /// SceneView Render consumes this frame. `items` is caller-owned rather than a Scene member so
    /// it can live on the App's per-frame stack -- render::Renderer::render() only needs the span
    /// to outlive the one render() call that reads it.
    render::SceneView view(std::vector<render::DrawItem>& items, render::ShadowFilter filter,
                           bool wireframe) const;
};

/// Crytek Sponza from the McGuire Computer Graphics Archive. `xmake setup` converts the pinned OBJ
/// archive to core glTF; camera and bounding sphere are computed from the loaded AABB.
AssetResult<std::unique_ptr<Scene>> loadSponzaScene(rhi::Device& device);

/// Khronos' DamagedHelmet sample (Assets/Fetched/DamagedHelmet, fetched by `xmake setup`). No
/// floor -- a model showcase, floating near the origin.
AssetResult<std::unique_ptr<Scene>> loadHelmetScene(rhi::Device& device);

/// Deterministic code-generated diagnostics: a material sweep sphere grid plus horizontal color,
/// texture, normal, and depth lanes. A fetched studio HDRI upgrades its lighting, with a neutral
/// deterministic fallback that keeps the scene always available.
AssetResult<std::unique_ptr<Scene>> loadMaterialLabScene(rhi::Device& device);

} // namespace lmx::engine
