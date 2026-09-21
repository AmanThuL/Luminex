//----------------------------------------------------------------------------------------------------------------------
/// @file SceneTables.h
/// @brief Declares shared scene table row layouts and borrowed GPU bindings.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace rojoRHI {
class Buffer;
}

namespace lmx::engine {

/// Motion cannot be derived from the instance's previous transform.
constexpr uint32_t kInstanceMotionInvalid = 1u;
/// Bounds cannot safely reject this instance.
constexpr uint32_t kInstanceBoundsUnreliable = 2u;
/// Visible instance-row list binding in scene and shadow entries.
constexpr uint32_t kVisibleRowsSlot = 4;
/// Material samples the resolved normal texture.
constexpr uint32_t kMaterialHasNormalMap = 1u;
/// Material applies alpha-test coverage.
constexpr uint32_t kMaterialMasked = 2u;
/// Masked material renders both sides and reverses back-face normals.
constexpr uint32_t kMaterialDoubleSided = 4u;
/// Shared instance buffer binding in scene, shadow and selection entries.
constexpr uint32_t kSceneInstancesSlot = 5;
/// Shared material buffer binding in scene, shadow and selection entries.
constexpr uint32_t kSceneMaterialsSlot = 6;
/// Mesh-row binding reserved for the shared row ABI reader.
constexpr uint32_t kSceneMeshesSlot = 7;
/// Local point/spot light-row binding reserved for the shared row ABI reader.
constexpr uint32_t kSceneLightsSlot = 8;
/// Per-froxel `(offset, count)` cluster-record binding read by the scene entries' local-light loop.
constexpr uint32_t kLightClusterGridSlot = 9;
/// Flat cluster index-list binding read by the scene entries' local-light loop.
constexpr uint32_t kLightClusterIndexSlot = 10;
/// Per-pass `LocalLightParams` frame-data binding selecting the local-light path.
constexpr uint32_t kLocalLightParamsSlot = 11;
/// Maximum live local lights per scene (docs/milestones/m7/m7.5.md's light table).
constexpr uint32_t kMaxLocalLights = 4096;
/// Per-draw instance selector binding in scene and shadow entries.
constexpr uint32_t kDrawUniformsSlot = 1;

/// Stable instance slot contents; matrices use column-major object-to-world transforms.
struct alignas(16) InstanceRow {
    glm::mat4 model{1.0f};          ///< Current object-to-world transform.
    glm::mat4 previousModel{1.0f};  ///< Last accepted frame's object-to-world transform.
    glm::mat4 normalMatrix{1.0f};   ///< Inverse transpose of the current linear transform.
    uint32_t meshRow = 0;           ///< Stable geometry slot.
    uint32_t materialRow = 0;       ///< Stable material slot.
    uint32_t flags = 0;             ///< Instance motion flags.
    float emissiveScale = 1.0f;     ///< Multiplier of scene-linear material emissive radiance.
    glm::vec3 worldBoundsMin{0.0f}; ///< World-space minimum, valid unless bounds flag is set.
    uint32_t boundsPadding0 = 0;    ///< Explicit ABI padding.
    glm::vec3 worldBoundsMax{0.0f}; ///< World-space maximum, valid unless bounds flag is set.
    uint32_t boundsPadding1 = 0;    ///< Explicit ABI padding.
};
static_assert(sizeof(InstanceRow) == 240);
static_assert(offsetof(InstanceRow, model) == 0);
static_assert(offsetof(InstanceRow, previousModel) == 64);
static_assert(offsetof(InstanceRow, normalMatrix) == 128);
static_assert(offsetof(InstanceRow, meshRow) == 192);
static_assert(offsetof(InstanceRow, materialRow) == 196);
static_assert(offsetof(InstanceRow, flags) == 200);
static_assert(offsetof(InstanceRow, emissiveScale) == 204);
static_assert(offsetof(InstanceRow, worldBoundsMin) == 208);
static_assert(offsetof(InstanceRow, boundsPadding0) == 220);
static_assert(offsetof(InstanceRow, worldBoundsMax) == 224);
static_assert(offsetof(InstanceRow, boundsPadding1) == 236);

/// Stable material slot contents; factors are scene-linear and textures remain per draw.
struct alignas(16) MaterialRow {
    glm::mat4 uvTransform{1.0f};    ///< UV transform applied before material sampling.
    glm::vec4 albedo{1.0f};         ///< Scene-linear base color and linear alpha.
    glm::vec3 emissive{0.0f};       ///< Scene-linear emitted radiance before instance scaling.
    float roughness = 0.5f;         ///< Perceptual roughness factor.
    float metallic = 0.0f;          ///< Dielectric-to-metal mixture factor.
    float occlusionStrength = 1.0f; ///< Occlusion-map interpolation strength.
    float alphaCutoff = 0.5f;       ///< Nonnegative alpha-test threshold.
    uint32_t flags = 0;             ///< Normal-map, masked and double-sided flags.
};
static_assert(sizeof(MaterialRow) == 112);
static_assert(offsetof(MaterialRow, uvTransform) == 0);
static_assert(offsetof(MaterialRow, albedo) == 64);
static_assert(offsetof(MaterialRow, emissive) == 80);
static_assert(offsetof(MaterialRow, roughness) == 92);
static_assert(offsetof(MaterialRow, metallic) == 96);
static_assert(offsetof(MaterialRow, occlusionStrength) == 100);
static_assert(offsetof(MaterialRow, alphaCutoff) == 104);
static_assert(offsetof(MaterialRow, flags) == 108);

/// Indexed range in the scene geometry pool; indices are rebased and base vertex is zero.
struct alignas(16) MeshRow {
    uint32_t firstIndex = 0;     ///< First uint32 index in the shared index buffer.
    uint32_t indexCount = 0;     ///< Number of indices in this mesh.
    uint32_t firstVertex = 0;    ///< First vertex in the shared vertex buffer.
    uint32_t vertexCount = 0;    ///< Number of vertices in this mesh.
    glm::vec3 boundsMin{1.0f};   ///< Mesh-local minimum; inverted for unreliable geometry.
    uint32_t boundsPadding0 = 0; ///< Explicit ABI padding.
    glm::vec3 boundsMax{-1.0f};  ///< Mesh-local maximum; inverted for unreliable geometry.
    uint32_t boundsPadding1 = 0; ///< Explicit ABI padding.
};
static_assert(sizeof(MeshRow) == 48);
static_assert(offsetof(MeshRow, firstIndex) == 0);
static_assert(offsetof(MeshRow, indexCount) == 4);
static_assert(offsetof(MeshRow, firstVertex) == 8);
static_assert(offsetof(MeshRow, vertexCount) == 12);
static_assert(offsetof(MeshRow, boundsMin) == 16);
static_assert(offsetof(MeshRow, boundsPadding0) == 28);
static_assert(offsetof(MeshRow, boundsMax) == 32);
static_assert(offsetof(MeshRow, boundsPadding1) == 44);

/// Stable local point/spot light slot contents; `range == 0` marks a free slot. `strength` is
/// `colour * intensity` decoded once at build, `spotScale`/`spotOffset` encode the cone term for
/// both light types (a point light stores 0/1), and `boundCentre`/`boundRadius` are the CPU-
/// computed world bounding sphere `LocalLightMath.h` derives -- see docs/milestones/m7/m7.5.md's
/// light table.
struct alignas(16) LightRow {
    glm::vec3 position{0.0f};               ///< World-space origin, metres.
    float range = 0.0f;                     ///< Metres; zero marks a free slot.
    glm::vec3 strength{0.0f};               ///< Linear-RGB `colour * intensity`.
    float spotScale = 0.0f;                 ///< Cone slope; zero for a point light.
    glm::vec3 direction{0.0f, 0.0f, -1.0f}; ///< Unit ray-travel direction.
    float spotOffset = 1.0f;                ///< Cone offset; one for a point light.
    glm::vec3 boundCentre{0.0f};            ///< World-space bounding-sphere centre.
    float boundRadius = 0.0f;               ///< Inflated world-space bounding-sphere radius.
};
static_assert(sizeof(LightRow) == 64);
static_assert(offsetof(LightRow, position) == 0);
static_assert(offsetof(LightRow, range) == 12);
static_assert(offsetof(LightRow, strength) == 16);
static_assert(offsetof(LightRow, spotScale) == 28);
static_assert(offsetof(LightRow, direction) == 32);
static_assert(offsetof(LightRow, spotOffset) == 44);
static_assert(offsetof(LightRow, boundCentre) == 48);
static_assert(offsetof(LightRow, boundRadius) == 60);

/// Per-draw selector into the paced instance table; padding is always zero.
struct DrawUniforms {
    uint32_t firstEntry = 0; ///< First entry in the visible instance-row list.
    uint32_t padding[3]{};   ///< Explicit constant-buffer tail padding.
};
static_assert(sizeof(DrawUniforms) == 16);
static_assert(offsetof(DrawUniforms, firstEntry) == 0);

/// Non-owning frame bindings; Scene owns all buffers until submitted readers have retired.
/// CPU rows may be consumed only before the next scene preparation or object-table mutation.
struct SceneTables {
    rojoRHI::Buffer* vertices = nullptr;  ///< Immutable packed geometry vertices.
    rojoRHI::Buffer* indices = nullptr;   ///< Immutable rebased uint32 geometry indices.
    rojoRHI::Buffer* meshes = nullptr;    ///< Immutable mesh rows for the paced frame slot.
    rojoRHI::Buffer* instances = nullptr; ///< Instance rows for the paced frame slot.
    rojoRHI::Buffer* materials = nullptr; ///< Material rows for the paced frame slot.
    uint32_t meshCount = 0;               ///< Number of addressable mesh slots.
    uint32_t instanceCount = 0; ///< Number of addressable instance slots, including holes.
    uint32_t materialCount = 0; ///< Number of addressable material slots, including holes.
    std::span<const InstanceRow> instanceRows; ///< CPU rows exactly matching the prepared GPU slot.
    uint32_t instanceCapacity = 0; ///< Allocated instance slots; draw buffers grow with this count.
    rojoRHI::Buffer* lights = nullptr;   ///< Local light rows for the paced slot, or null if none.
    uint32_t lightRowCount = 0;          ///< Addressable light row slots, including free slots.
    uint32_t lightCapacity = 0;          ///< Allocated light slots; zero until one exists.
    std::span<const LightRow> lightRows; ///< CPU rows exactly matching the prepared GPU slot.
    uint32_t liveLightCount = 0; ///< Enabled local lights this frame; zero disables local passes.
};

} // namespace lmx::engine
