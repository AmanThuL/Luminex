//----------------------------------------------------------------------------------------------------------------------
/// @file Scene.h
/// @brief Declares scene data, transforms, views, and scene loaders.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset.h"
#include "Engine/SceneAnimation.h"
#include "RHI/RHI.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
    /// The model matrix of the previous declared frame, maintained by `Scene::commitFrame()`.
    /// Equal to `modelMatrix()` until the object first moves.
    glm::mat4 previousModel{1.0f};
    /// How this object's motion is produced. `Invalid` marks a draw whose history must not be
    /// reprojected.
    render::MotionClass motionClass = render::MotionClass::Rigid;
    /// Multiplier `Scene::view()` applies to the material's authored emissive colour, written by
    /// `Scene::animate()` from an `EmissiveTrack`. Objects with no track keep the default of 1, so
    /// the authored colour passes through unchanged.
    float emissiveStrength = 1.0f;

    /// Builds the object's model matrix from its authored transform fields.
    glm::mat4 modelMatrix() const;
};

/// What decomposeTransform below extracts from a general 4x4.
struct DecomposedTransform {
    glm::vec3 position{0.f};     ///< Extracted translation.
    glm::vec3 eulerDegrees{0.f}; ///< Extracted XYZ Euler rotation in degrees.
    glm::vec3 scale{1.f};        ///< Extracted per-axis scale.
};

/// Extracts translation, Euler rotation, and scale when `world` is decomposable, in the Y-X-Z
/// order `SceneObject::modelMatrix` composes them, so the two round-trip. The factorisation is
/// proved by recomposing it, so shear -- a non-orthogonal basis, which no translate-rotate-scale
/// chain can produce -- is rejected rather than silently orthogonalised, as is a projective row.
/// A single zero-scale axis is accepted, because a collapsed object is a legitimate authored pose;
/// two or more leave no rotation to extract and are rejected.
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
    SceneAnimation animation;    ///< Tracks this scene plays; empty for a static scene.
    double animationTime = 0.0;  ///< Playback position in seconds, advanced by advanceAnimation().

    /// Collapses every object's motion onto its current pose, so the next frame reports no
    /// movement. Called when the scene becomes active or after a discontinuity.
    void resetMotion();

    /// Promotes every object's current model matrix to its previous one. The App calls this after
    /// a frame is declared, so a frame skipped before declaration never becomes "previous".
    void commitFrame();

    /// Advances `animationTime` by `dt` seconds, wrapping at `animation.duration` when the clip
    /// loops. A non-looping clip runs past its end and sampling clamps.
    void advanceAnimation(double dt);

    /// Samples every rigid track at `seconds` and writes the result into its object's transform
    /// fields, and every emissive track into its object's `emissiveStrength`. Track object indices
    /// and sampled poses are validated where tracks are built, so a pose that cannot be decomposed
    /// here is a contract violation.
    void animate(double seconds);

    /// Fills `items` (cleared first, one DrawItem per object, in object order) and returns the
    /// SceneView Render consumes this frame. `items` is caller-owned rather than a Scene member so
    /// it can live on the App's per-frame stack -- render::Renderer::render() only needs the span
    /// to outlive the one render() call that reads it.
    render::SceneView view(std::vector<render::DrawItem>& items, render::ShadowFilter filter,
                           bool wireframe) const;
};

/// Builds a Scene from the .gltf or .glb file at `path` (absolute, or relative to the working
/// directory): uploads its meshes, materials and textures, carries its first animation in as rigid
/// tracks, poses the scene at the clip's t = 0, fits the bounding sphere to that posed geometry,
/// seeds every object's previous transform, and attaches the shared neutral environment. `name`
/// names the scene and prefixes every GPU object's debug label. The camera is left at its default:
/// each catalog scene below fits its own after calling this.
AssetResult<std::unique_ptr<Scene>> loadGltfScene(rhi::Device& device, std::string_view path,
                                                  std::string_view name);

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

/// Khronos' CesiumMilkTruck sample (Assets/Fetched/CesiumMilkTruck, fetched by `xmake setup`).
/// Its wheel clip loads as rigid tracks, so the truck is the fetched rigid-motion reference;
/// camera and bounding sphere are computed from the loaded AABB.
AssetResult<std::unique_ptr<Scene>> loadMilkTruckScene(rhi::Device& device);

/// Deterministic code-generated temporal diagnostics: a checkerboard floor under a rotating cube,
/// a sphere orbiting a static reference cube, a row of oscillating poles, and one cube flagged
/// `render::MotionClass::Invalid`, all driven by looping tracks alongside a looping camera track.
AssetResult<std::unique_ptr<Scene>> loadTemporalLabScene(rhi::Device& device);

/// San Miguel's pinned realtime variant with masked foliage and a looping camera rail. Optional
/// assets are fetched by `xmake setup --san-miguel`; missing assets return NotFound with that hint.
AssetResult<std::unique_ptr<Scene>> loadSanMiguelScene(rhi::Device& device);

} // namespace lmx::engine
