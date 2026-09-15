//----------------------------------------------------------------------------------------------------------------------
/// @file Scene.h
/// @brief Declares scene data, transforms, views, and scene loaders.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Asset/Asset.h"
#include "Asset/SceneAnimation.h"
#include "Asset/Transform.h"
#include "RHI/RHI.h"
#include "Render/Bounds.h"
#include "Render/Camera.h"
#include "Render/Mesh.h"
#include "Render/SceneView.h"
#include "Scene/MaterialRecord.h"
#include "Scene/SceneIds.h"
#include "Scene/SceneTableStats.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::scene {

/// Editable scene instance referencing one mesh and material.
struct SceneObject {
    std::string name; ///< Display name, qualified by authored material for multi-primitive assets.
    glm::vec3 position{0.f};     ///< World-space translation.
    glm::vec3 eulerDegrees{0.f}; ///< XYZ Euler rotation in degrees.
    glm::vec3 scale{1.f};        ///< Per-axis object scale.
    MeshId mesh;                 ///< Scene-owned immutable geometry.
    MaterialId material;         ///< Scene-owned shared material.
    InstanceId id;               ///< Stable row identity assigned by addObject().
    /// The model matrix of the previous declared frame, maintained by `Scene::commitFrame()`.
    /// Equal to `modelMatrix()` until the object first moves.
    glm::mat4 previousModel{1.0f};
    /// How this object's motion is produced. `Invalid` marks a draw whose history must not be
    /// reprojected.
    render::MotionClass motionClass = render::MotionClass::Rigid;
    /// Multiplier the scene shader applies to the material's authored emissive colour, written by
    /// `Scene::animate()` from an `EmissiveTrack`. Objects with no track keep the default of 1, so
    /// the authored colour passes through unchanged.
    float emissiveStrength = 1.0f;
    std::string sourceName;        ///< Exact authored node/mesh name, empty for generated objects.
    std::string materialQualifier; ///< Authored primitive material qualifier, empty when unneeded.

    /// Builds the object's model matrix from its authored transform fields.
    glm::mat4 modelMatrix() const;
};

/// Authored initial camera pose copied into a renderer camera at scene activation.
struct SceneCamera {
    glm::vec3 position;                  ///< Initial world-space position.
    float yaw, pitch, fovY, nearZ, farZ; ///< Radian angles and positive view distances.
};

/// Copies the authored scene camera pose into a renderer camera.
render::Camera cameraFromScene(const SceneCamera& sceneCamera);

/// Owns renderable scene resources, instances, lighting, and initial view state.
class Scene {
public:
    /// Constructs a distinct identity store; store exhaustion is a contract failure.
    Scene();
    /// Releases owned resources; the owner must first wait for all GPU use to retire.
    ~Scene();
    /// Transfers ownership and identities; borrowed references remain tied to this store.
    Scene(Scene&&) noexcept;
    /// Transfers ownership after the destination's GPU work has retired.
    Scene& operator=(Scene&&) noexcept;
    /// Adds immutable CPU geometry before finalize; invalid indices are misuse.
    MeshId addMesh(render::MeshData data, std::string_view label);
    /// Takes ownership of a non-null texture and assigns a fresh identity.
    TextureId addTexture(std::unique_ptr<rhi::Texture> texture);
    /// Adds shared scene-linear factors, asserting every supplied texture identity resolves.
    MaterialId addMaterial(MaterialRecord material);
    /// Adds an instance with valid mesh/material handles and seeds its own previous pose.
    InstanceId addObject(SceneObject object);
    /// Removes a live instance, invalidates its handle and preserves later rows.
    void removeObject(InstanceId id);
    /// Removes an unreferenced texture; stale or still-referenced identities are misuse.
    /// Submitted references retain the resource until three paced frames after its last use.
    void removeTexture(TextureId id);
    /// Returns the live object or null for stale, foreign or invalid identities.
    SceneObject* tryObject(InstanceId id);
    /// Returns the live object or null; the pointer is invalidated by object-list mutations.
    const SceneObject* tryObject(InstanceId id) const;
    /// Returns immutable geometry metadata or null for an unresolvable identity.
    const render::MeshRow* tryMesh(MeshId id) const;
    /// Returns reliable mesh-local bounds, or none for invalid identities or degenerate geometry.
    std::optional<render::Aabb> meshBounds(MeshId id) const;
    /// Returns editable material data or null for an unresolvable identity.
    MaterialRecord* tryMaterial(MaterialId id);
    /// Returns material data or null for an unresolvable identity.
    const MaterialRecord* tryMaterial(MaterialId id) const;
    /// Returns the owned texture or null for an unresolvable identity.
    rhi::Texture* tryTexture(TextureId id) const;
    /// Returns an editable material, asserting the identity resolves.
    MaterialRecord& material(MaterialId id);
    /// Returns a material, asserting the identity resolves.
    const MaterialRecord& material(MaterialId id) const;
    /// Merges immutable geometry and allocates three paced table slots; returns GPU failures.
    rhi::Result<void> finalize(rhi::Device& device);
    /// Updates this frame's retired slot after Device::beginFrame and before any declaration.
    /// Frame numbers must strictly advance and match the owning device; returns growth failures.
    rhi::Result<void> prepareFrame(uint64_t frameNumber);
    /// Reports live counts, allocation capacities and the last preparation's upload work.
    SceneTableStats tableStats() const;
    /// Returns borrowed bindings for the prepared slot; valid through that frame's execution.
    /// An unfinalized CPU scene returns empty bindings and cannot be submitted to the renderer.
    render::SceneTables tables() const;
    std::string name;                   ///< User-facing scene name.
    std::vector<SceneObject> objects;   ///< Editable draw instances.
    render::DirectionalLight lights[3]; ///< Fixed-size analytic light set.
    glm::vec4 boundingSphere{0.f};      ///< World-space center in xyz and radius in w.
    std::optional<MeshId> skySphere;    ///< Geometry used by the sky pass without an instance.
    std::unique_ptr<rhi::Texture> skyCubemap; ///< Authored linear-radiance environment.
    /// Image-based lighting generated from the same authored sky radiance skyCubemap carries
    /// (Asset/Ibl.h): a cosine-convolved irradiance cube, a GGX-prefiltered radiance chain, and
    /// the split-sum DFG table. Published together with the sky by the scene-build path, so a scene
    /// that has a skyCubemap has all three.
    std::unique_ptr<rhi::Texture> irradianceMap;     ///< Diffuse irradiance cubemap.
    std::unique_ptr<rhi::Texture> prefilteredEnvMap; ///< GGX-prefiltered environment chain.
    std::unique_ptr<rhi::Texture> dfgLut;            ///< Split-sum material response lookup table.
    SceneCamera initialCamera{};     ///< Camera pose restored when the scene becomes active.
    asset::SceneAnimation animation; ///< Tracks this scene plays; empty for a static scene.
    double animationTime = 0.0; ///< Playback position in seconds, advanced by advanceAnimation().

    /// Collapses every object's motion onto its current pose, so the next frame reports no
    /// movement. Called when the scene becomes active or after a discontinuity.
    void resetMotion();

    /// Promotes every object's current model matrix to its previous one. The App calls this after
    /// graph execution accepts the frame, so a skipped frame never becomes "previous".
    void commitFrame();

    /// Advances `animationTime` by `dt` seconds, wrapping at `animation.duration` when the clip
    /// loops. A non-looping clip runs past its end and sampling clamps.
    void advanceAnimation(double dt);

    /// Samples every rigid track at `seconds` and writes the result into its object's transform
    /// fields, and every emissive track into its object's `emissiveStrength`. Track object indices
    /// and sampled poses are validated where tracks are built, so a pose that cannot be decomposed
    /// here is a contract violation.
    void animate(double seconds);

    /// Samples the nonempty camera track at `animationTime` and assigns position, yaw, and pitch.
    /// Lens state remains owned by the caller and is unchanged.
    void followCameraTrack(render::Camera& camera) const;

    /// Fills `items` (cleared first, one DrawItem per object, in object order) and returns the
    /// SceneView Render consumes this frame. `items` is caller-owned rather than a Scene member so
    /// it can live on the App's per-frame stack. The returned view, items and scene resources must
    /// remain alive through pass declaration and graph execution.
    render::SceneView view(std::vector<render::DrawItem>& items, render::ShadowFilter filter,
                           bool wireframe) const;

private:
    void validateObjects() const;
    struct Storage;
    std::unique_ptr<Storage> m_storage;
};

/// Builds a Scene from the .gltf or .glb file at `path` (absolute, or relative to the working
/// directory): uploads its meshes, materials and textures, carries its first animation in as rigid
/// tracks, poses the scene at the clip's t = 0, fits the bounding sphere to that posed geometry,
/// seeds every object's previous transform, and attaches the shared neutral environment. `name`
/// names the scene and prefixes every GPU object's debug label. The camera is left at its default:
/// each catalog scene below fits its own after calling this.
asset::AssetResult<std::unique_ptr<Scene>> loadGltfScene(rhi::Device& device, std::string_view path,
                                                         std::string_view name);

/// Crytek Sponza from the McGuire Computer Graphics Archive. `xmake setup` converts the pinned OBJ
/// archive to core glTF; camera and bounding sphere are computed from the loaded AABB.
asset::AssetResult<std::unique_ptr<Scene>> loadSponzaScene(rhi::Device& device);

/// Khronos' DamagedHelmet sample (Assets/Fetched/DamagedHelmet, fetched by `xmake setup`). No
/// floor -- a model showcase, floating near the origin.
asset::AssetResult<std::unique_ptr<Scene>> loadHelmetScene(rhi::Device& device);

/// Deterministic code-generated diagnostics: a material sweep sphere grid plus horizontal color,
/// texture, normal, and depth lanes. A fetched studio HDRI upgrades its lighting, with a neutral
/// deterministic fallback that keeps the scene always available.
asset::AssetResult<std::unique_ptr<Scene>> loadMaterialLabScene(rhi::Device& device);

/// Khronos' CesiumMilkTruck sample (Assets/Fetched/CesiumMilkTruck, fetched by `xmake setup`).
/// Its wheel clip loads as rigid tracks, so the truck is the fetched rigid-motion reference;
/// camera and bounding sphere are computed from the loaded AABB.
asset::AssetResult<std::unique_ptr<Scene>> loadMilkTruckScene(rhi::Device& device);

/// Deterministic code-generated temporal diagnostics: a checkerboard floor under a rotating cube,
/// a sphere orbiting a static reference cube, a row of oscillating poles, and one cube flagged
/// `render::MotionClass::Invalid`, all driven by looping tracks alongside a looping camera track.
asset::AssetResult<std::unique_ptr<Scene>> loadTemporalLabScene(rhi::Device& device);

/// San Miguel's pinned realtime variant with masked foliage and a looping camera rail. Optional
/// assets are fetched by `xmake setup --san-miguel`; missing assets return NotFound with that hint.
asset::AssetResult<std::unique_ptr<Scene>> loadSanMiguelScene(rhi::Device& device);

/// Builds a seeded repeated-geometry visibility lab with exactly instanceCount candidates.
/// Counts from 1 through 1,048,576 include up to five initial-camera boundary probes.
asset::AssetResult<std::unique_ptr<Scene>> loadVisibilityLabScene(rhi::Device& device,
                                                                  uint32_t instanceCount = 4096);

} // namespace lmx::scene
