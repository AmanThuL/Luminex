//----------------------------------------------------------------------------------------------------------------------
/// @file Scene.h
/// @brief Declares scene data, transforms, views, and the glTF scene loader.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Math/Aabb.h"
#include "Core/Math/Transform.h"
#include "Engine/Asset/Asset.h"
#include "Engine/Asset/Model/SceneAnimation.h"
#include "Engine/Scene/MaterialRecord.h"
#include "Engine/Scene/SceneIds.h"
#include "Engine/Scene/SceneTableStats.h"
#include "Engine/Types/Camera.h"
#include "Engine/Types/DirectionalLight.h"
#include "Engine/Types/DrawItem.h"
#include "Engine/Types/LocalLight.h"
#include "Engine/Types/Mesh.h"
#include "Engine/Types/MotionClass.h"
#include <rojoRHI/RHI.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lmx::engine {

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
    engine::MotionClass motionClass = engine::MotionClass::Rigid;
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
engine::Camera cameraFromScene(const SceneCamera& sceneCamera);

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
    MeshId addMesh(engine::MeshData data, std::string_view label);
    /// Takes ownership of a non-null texture and assigns a fresh identity.
    TextureId addTexture(std::unique_ptr<rojoRHI::Texture> texture);
    /// Adds shared scene-linear factors, asserting every supplied texture identity resolves.
    MaterialId addMaterial(MaterialRecord material);
    /// Adds an instance with valid mesh/material handles and seeds its own previous pose.
    InstanceId addObject(SceneObject object);
    /// Removes a live instance, invalidates its handle and preserves later rows.
    void removeObject(InstanceId id);
    /// Removes an unreferenced texture; stale or still-referenced identities are misuse.
    /// Submitted references retain the resource until three paced frames after its last use.
    void removeTexture(TextureId id);
    /// Adds a local point or spot light, validating it the way `engine::makeLightRow` does and
    /// failing with `InvalidDesc` for invalid parameters or once `engine::kMaxLocalLights` live
    /// lights already exist. May be called before or after finalize.
    rojoRHI::Result<LightId> addLight(const engine::LocalLight& light);
    /// Removes a live local light and returns whether the identity resolved; a stale or foreign
    /// identity is reported rather than asserted. Preserves later row slots.
    bool removeLight(LightId id);
    /// Replaces a live local light's parameters, validating them the way `addLight` does; an
    /// invalid identity or invalid light fails with `InvalidDesc` and leaves the light unchanged.
    rojoRHI::Result<void> updateLight(LightId id, const engine::LocalLight& light);
    /// Returns the live local light's authored parameters, or null for an unresolvable identity.
    const engine::LocalLight* light(LightId id) const;
    /// Returns every existing local light identity, including disabled lights, by row slot.
    std::span<const LightId> localLights() const;
    /// Number of enabled lights contributing to rendering; disabled lights still occupy capacity.
    uint32_t enabledLightCount() const;
    /// Authored rig identities, including disabled or subsequently removed lights.
    std::span<const LightId> rigLightIds() const { return m_rigLightIds; }
    /// Records the authored rig identities that rigLightIds() reports.
    void setRigLightIds(std::vector<LightId> ids) { m_rigLightIds = std::move(ids); }
    /// Resolves a `LightOrbitTrack::light` creation-order index (the order `addLight` was called,
    /// among lights added before `finalize`) to the `LightId` it was assigned, for playback and
    /// session code outside Scene. The index space freezes at `finalize`: a light added afterward
    /// -- a runtime pile addition, say -- has no index and is static by contract, however many
    /// lights are later added and removed. Returns `nullopt` only for an index at or beyond that
    /// frozen count; a later-removed light's id still resolves here, and callers distinguish it
    /// from a live one via `light()`.
    std::optional<LightId> animationLightId(uint32_t index) const;
    /// Returns the live object or null for stale, foreign or invalid identities.
    SceneObject* tryObject(InstanceId id);
    /// Returns the live object or null; the pointer is invalidated by object-list mutations.
    const SceneObject* tryObject(InstanceId id) const;
    /// Returns immutable geometry metadata or null for an unresolvable identity.
    const engine::MeshRow* tryMesh(MeshId id) const;
    /// Returns reliable mesh-local bounds, or none for invalid identities or degenerate geometry.
    std::optional<Aabb> meshBounds(MeshId id) const;
    /// Returns editable material data or null for an unresolvable identity.
    MaterialRecord* tryMaterial(MaterialId id);
    /// Returns material data or null for an unresolvable identity.
    const MaterialRecord* tryMaterial(MaterialId id) const;
    /// Returns the owned texture or null for an unresolvable identity.
    rojoRHI::Texture* tryTexture(TextureId id) const;
    /// Returns an editable material, asserting the identity resolves.
    MaterialRecord& material(MaterialId id);
    /// Returns a material, asserting the identity resolves.
    const MaterialRecord& material(MaterialId id) const;
    /// Merges immutable geometry and allocates three paced table slots; returns GPU failures.
    rojoRHI::Result<void> finalize(rojoRHI::Device& device);
    /// Updates this frame's retired slot after Device::beginFrame and before any declaration.
    /// Frame numbers must strictly advance and match the owning device; returns growth failures.
    rojoRHI::Result<void> prepareFrame(uint64_t frameNumber);
    /// Reports live counts, allocation capacities and the last preparation's upload work.
    SceneTableStats tableStats() const;
    /// Monotonic coverage revision, refreshed for public object/material edits by prepareFrame.
    /// Add/remove identities advance it immediately; motion history and lighting edits do not.
    uint64_t coverageEpoch() const;
    /// Returns borrowed bindings for the prepared slot; valid through that frame's execution.
    /// An unfinalized CPU scene returns empty bindings and cannot be submitted to the renderer.
    engine::SceneTables tables() const;
    std::string name;                   ///< User-facing scene name.
    std::vector<SceneObject> objects;   ///< Editable draw instances.
    engine::DirectionalLight lights[3]; ///< Fixed-size analytic light set.
    glm::vec4 boundingSphere{0.f};      ///< World-space center in xyz and radius in w.
    std::optional<MeshId> skySphere;    ///< Geometry used by the sky pass without an instance.
    std::unique_ptr<rojoRHI::Texture> skyCubemap; ///< Authored linear-radiance environment.
    /// Image-based lighting generated from the same authored sky radiance skyCubemap carries
    /// (Engine/Asset/Texture/Ibl.h): a cosine-convolved irradiance cube, a GGX-prefiltered radiance
    /// chain, and the split-sum DFG table. Published together with the sky by the scene-build path,
    /// so a scene that has a skyCubemap has all three.
    std::unique_ptr<rojoRHI::Texture> irradianceMap;     ///< Diffuse irradiance cubemap.
    std::unique_ptr<rojoRHI::Texture> prefilteredEnvMap; ///< GGX-prefiltered environment chain.
    std::unique_ptr<rojoRHI::Texture> dfgLut; ///< Split-sum material response lookup table.
    /// Immutable authored grid-light count set before finalize; zero outside LightLab. Remaining
    /// initial local lights are the authored overflow pile, independently editable by the session.
    uint32_t lightLabGridCount = 0;
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
    /// fields, every emissive track into its object's `emissiveStrength`, and every light orbit
    /// track into its light's position through `updateLight` (a removed light's track is skipped,
    /// not a contract violation). Light motion never advances `coverageEpoch`. Track object indices
    /// and sampled poses are validated where tracks are built, so a pose that cannot be decomposed
    /// here is a contract violation.
    void animate(double seconds);

    /// Samples the nonempty camera track at `animationTime` and assigns position, yaw, and pitch.
    /// Lens state remains owned by the caller and is unchanged.
    void followCameraTrack(engine::Camera& camera) const;

    /// Fills `items` (cleared first, one DrawItem per object, in object order) after validating
    /// every object. `items` is caller-owned rather than a Scene member so it can live on the App's
    /// per-frame stack; render::buildSceneView borrows it into the frame's SceneView, so it and the
    /// scene resources must remain alive through pass declaration and graph execution.
    void fillDrawItems(std::vector<engine::DrawItem>& items) const;

private:
    std::vector<LightId> m_rigLightIds;
    void validateObjects() const;
    struct Storage;
    std::unique_ptr<Storage> m_storage;
};

/// Authors scene content, such as local lights, into a loaded scene before it is finalized.
using SceneAuthoring = std::function<rojoRHI::Result<void>(Scene&)>;

/// Builds a Scene from the .gltf or .glb file at `path` (absolute, or relative to the working
/// directory): uploads its meshes, materials and textures, carries its first animation in as rigid
/// tracks, poses the scene at the clip's t = 0, fits the bounding sphere to that posed geometry,
/// seeds every object's previous transform, and attaches the shared neutral environment. `name`
/// names the scene and prefixes every GPU object's debug label. A nonempty `beforeFinalize` runs
/// last before finalize, so lights it adds receive animation indices; its failure fails the load.
/// The camera is left at its default for the caller to fit.
asset::AssetResult<std::unique_ptr<Scene>> loadGltfScene(rojoRHI::Device& device,
                                                         std::string_view path,
                                                         std::string_view name,
                                                         const SceneAuthoring& beforeFinalize = {});

} // namespace lmx::engine
