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

namespace lmx::rhi {
class Buffer;
}

namespace lmx::render {

/// Motion cannot be derived from the instance's previous transform.
constexpr uint32_t kInstanceMotionInvalid = 1u;
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
/// Per-draw instance selector binding in scene and shadow entries.
constexpr uint32_t kDrawUniformsSlot = 1;

/// Stable instance slot contents; matrices use column-major object-to-world transforms.
struct alignas(16) InstanceRow {
    glm::mat4 model{1.0f};         ///< Current object-to-world transform.
    glm::mat4 previousModel{1.0f}; ///< Last accepted frame's object-to-world transform.
    glm::mat4 normalMatrix{1.0f};  ///< Inverse transpose of the current linear transform.
    uint32_t meshRow = 0;          ///< Stable geometry slot.
    uint32_t materialRow = 0;      ///< Stable material slot.
    uint32_t flags = 0;            ///< Instance motion flags.
    float emissiveScale = 1.0f;    ///< Multiplier of scene-linear material emissive radiance.
};
static_assert(sizeof(InstanceRow) == 208);
static_assert(offsetof(InstanceRow, model) == 0);
static_assert(offsetof(InstanceRow, previousModel) == 64);
static_assert(offsetof(InstanceRow, normalMatrix) == 128);
static_assert(offsetof(InstanceRow, meshRow) == 192);
static_assert(offsetof(InstanceRow, materialRow) == 196);
static_assert(offsetof(InstanceRow, flags) == 200);
static_assert(offsetof(InstanceRow, emissiveScale) == 204);

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
struct MeshRow {
    uint32_t firstIndex = 0;  ///< First uint32 index in the shared index buffer.
    uint32_t indexCount = 0;  ///< Number of indices in this mesh.
    uint32_t firstVertex = 0; ///< First vertex in the shared vertex buffer.
    uint32_t vertexCount = 0; ///< Number of vertices in this mesh.
};
static_assert(sizeof(MeshRow) == 16);
static_assert(offsetof(MeshRow, firstIndex) == 0);
static_assert(offsetof(MeshRow, indexCount) == 4);
static_assert(offsetof(MeshRow, firstVertex) == 8);
static_assert(offsetof(MeshRow, vertexCount) == 12);

/// Per-draw selector into the paced instance table; padding is always zero.
struct DrawUniforms {
    uint32_t instanceRow = 0; ///< Stable instance slot selected for this draw.
    uint32_t padding[3]{};    ///< Explicit constant-buffer tail padding.
};
static_assert(sizeof(DrawUniforms) == 16);
static_assert(offsetof(DrawUniforms, instanceRow) == 0);

/// Non-owning frame bindings; Scene owns all buffers until submitted readers have retired.
struct SceneTables {
    rhi::Buffer* vertices = nullptr;  ///< Immutable packed geometry vertices.
    rhi::Buffer* indices = nullptr;   ///< Immutable rebased uint32 geometry indices.
    rhi::Buffer* meshes = nullptr;    ///< Immutable mesh rows for the paced frame slot.
    rhi::Buffer* instances = nullptr; ///< Instance rows for the paced frame slot.
    rhi::Buffer* materials = nullptr; ///< Material rows for the paced frame slot.
    uint32_t meshCount = 0;           ///< Number of addressable mesh slots.
    uint32_t instanceCount = 0;       ///< Number of addressable instance slots, including holes.
    uint32_t materialCount = 0;       ///< Number of addressable material slots, including holes.
};

} // namespace lmx::render
