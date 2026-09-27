//----------------------------------------------------------------------------------------------------------------------
/// @file GltfLoader.h
/// @brief Declares decoded glTF asset structures and loading operations.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Asset.h"
#include "Engine/Asset/Model/GeometryGenerator.h"
#include "Engine/Asset/Model/SceneAnimation.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::asset {

/// Decoded glTF coverage supported by the CPU loader; BLEND is rejected.
enum class GltfAlphaMode {
    Opaque, ///< Fully opaque coverage.
    Mask,   ///< Alpha-tested coverage.
};

/// Decoded glTF material factors and source-image indices.
struct GltfMaterial {
    glm::vec4 baseColorFactor{1.f};            ///< Linear RGBA base-color multiplier.
    int baseColorImage = -1, normalImage = -1; ///< indices into images
    float metallic = 1.f, roughness = 1.f;     ///< glTF metallic and perceptual roughness factors.
    /// glTF 2.0 packs this single texture as roughness = G, metallic = B (R and A unused).
    int metallicRoughnessImage = -1;
    /// glTF 2.0 packs ambient occlusion as R.
    int occlusionImage = -1;
    float occlusionStrength = 1.0f; ///< Strength applied to the occlusion texture.
    int emissiveImage = -1;         ///< Emissive source-image index, or -1 when absent.
    GltfAlphaMode alphaMode = GltfAlphaMode::Opaque; ///< Opaque or alpha-tested coverage.
    float alphaCutoff = 0.5f; ///< glTF MASK cutoff for base-color texture alpha times factor alpha.
    bool doubleSided = false; ///< Authored back-face visibility, honored by MASK materials.
    glm::vec3 emissiveFactor{0.f}; ///< linear, per the glTF spec -- not an sRGB-authored constant
};

/// stb-decoded, always tightly packed RGBA8 (4 bytes/pixel, no row padding). Entries not referenced
/// by the active scene remain empty; preserving the glTF image index avoids a second remapping
/// table.
struct GltfImage {
    uint32_t width = 0, height = 0; ///< Decoded dimensions in pixels.
    std::vector<std::byte> rgba8;   ///< Tightly packed RGBA8 texels.
};

/// One source node, indexed exactly as the glTF node array, including inactive and empty nodes.
struct GltfNode {
    std::string name;    ///< Authored node name; empty when absent, without mesh-name fallback.
    int32_t parent = -1; ///< Source parent index, or -1 for a root.
    std::vector<uint32_t> instances; ///< Active primitive indices into `GltfScene::instances`.
    bool animated = false;       ///< Whether any clip targets this node or one of its ancestors.
    glm::vec3 translation{0.0f}; ///< Authored local translation, in asset units.
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Authored local quaternion.
    glm::vec3 scale{1.0f};                      ///< Authored local per-axis scale.
    std::optional<glm::mat4> matrix; ///< Authored local matrix when present, superseding TRS.
};

/// One draw: a mesh (GltfScene::meshes index), the material it draws with, and its node's world
/// transform (glTF's full T*R*S chain composed through every ancestor). This is the file's
/// authored rest pose; an animated instance's motion lives in `GltfScene::tracks`, whose first key
/// need not equal this transform.
struct GltfInstance {
    uint32_t node = 0;          ///< Source index into `GltfScene::nodes`.
    uint32_t meshIndex = 0;     ///< Index into `GltfScene::meshes`.
    uint32_t materialIndex = 0; ///< Index into `GltfScene::materials`.
    glm::mat4 world{1.f};       ///< Flattened object-to-world transform.
    std::string sourceName; ///< Authored node name, falling back to mesh name; empty if unnamed.
    /// Authored material name distinguishing a primitive in a multi-primitive mesh; empty when
    /// the mesh has one primitive or its material is unnamed. Does not change renderer identity.
    std::string materialQualifier;
};

/// One instance's animation, resampled from the glTF clip into world-space poses. The clip's node
/// hierarchy is evaluated at bake time, so a key needs no parent chain to reconstruct. A STEP clip
/// is resampled into plain keys like a LINEAR one: at kAnimationBakeRate, every one of the clip's
/// held values lands on a key, and the scene clock only ever samples at those key times.
struct GltfAnimationTrack {
    uint32_t instanceIndex = 0; ///< Index into `GltfScene::instances`.
    std::vector<RigidKey> keys; ///< World-space poses sampled at a fixed rate, time-sorted.
};

/// The node-local component driven by one glTF animation channel.
enum class GltfAnimationPath {
    Translation, ///< Local translation in asset units, stored in key XYZ.
    Rotation,    ///< Local quaternion stored as key XYZW.
    Scale,       ///< Local per-axis scale, stored in key XYZ.
};

/// One baked local component sample; the unused W of translation and scale is zero.
struct GltfAnimationKey {
    double time = 0.0; ///< Seconds from clip start at the bake rate, including the exact endpoint.
    glm::vec4 value{0.0f}; ///< Component value interpreted by `GltfAnimationChannel::path`.
};

/// One source channel, retained in source order for component-wise last-wins composition.
struct GltfAnimationChannel {
    uint32_t node = 0; ///< Source index into `GltfScene::nodes`.
    GltfAnimationPath path = GltfAnimationPath::Translation; ///< The driven local component.
    bool step = false; ///< Hold the previous sample; otherwise interpolate, slerping rotations.
    std::vector<GltfAnimationKey> keys; ///< Local samples over the owning clip's entire duration.
};

/// One source clip with local channel samples for playback that loops clips independently.
struct GltfAnimationClip {
    std::string name;      ///< Authored animation name, empty when absent.
    double duration = 0.0; ///< Longest channel endpoint in seconds; zero for an empty clip.
    std::vector<GltfAnimationChannel> channels; ///< Source-order channels; later conflicts win.
};

/// Decoded active scene with flattened draw arrays and the complete source node/clip identity.
struct GltfScene {
    std::vector<GeoData> meshes; ///< one per active-scene primitive, tangents authored-or-generated
    std::vector<GltfMaterial> materials;  ///< Decoded materials referenced by active instances.
    std::vector<GltfImage> images;        ///< indexed like glTF; unreferenced entries stay empty
    std::vector<GltfNode> nodes;          ///< All source nodes in their original index order.
    std::vector<GltfAnimationClip> clips; ///< Every source clip, in source order.
    std::vector<GltfInstance> instances;  ///< node transforms flattened at the clip's rest pose
    /// One entry per animated active instance, in instance order. Every clip shares one clock;
    /// channels clamp at their endpoints and later source clips/channels win component conflicts.
    /// Independently looping playback instead consumes `clips` and the rest hierarchy in `nodes`.
    std::vector<GltfAnimationTrack> tracks;
    double animationDuration = 0.0; ///< Longest source clip length in seconds; 0 without animation.
};

/// Loads a .glb (embedded buffers/images) or .gltf (+ external .bin and image files, resolved
/// relative to path's directory) via cgltf + stb_image. Only meshes, materials, and base-color,
/// normal, metallic-roughness, occlusion, or emissive images reachable from the active scene are
/// decoded. Every used primitive becomes GeoData;
/// primitives without a TANGENT attribute get one generated (per-triangle from UV deltas,
/// accumulated per-vertex, Gram-Schmidt orthogonalized against the normal, w from the bitangent
/// cross sign; degenerate/absent UVs fall back to cross(up, normal)). Node transforms are
/// flattened after an active-scene traversal via cgltf's ancestor-chain composition, at the file's
/// authored rest pose.
///
/// Materials preserve OPAQUE/MASK coverage, the MASK cutoff and authored double-sided flag.
/// Referenced BLEND materials fail with AssetErrorCode::Unsupported.
///
/// Every animation is baked into `clips` as local component samples at kAnimationBakeRate,
/// retaining each clip's duration. LINEAR, STEP and CUBICSPLINE translation, rotation and scale
/// channels are supported; cubic rotations normalize the component-wise Hermite result. `tracks`
/// also retains the combined world-space bake over the longest clip on one non-wrapping clock,
/// using source-order last-wins component assignment. Morph-target channels or geometry, skins,
/// an animated node carrying a matrix transform, and a world pose that does not decompose into
/// translation, rotation and scale fail with AssetErrorCode::Unsupported.
AssetResult<GltfScene> loadGltf(std::string_view path);

} // namespace lmx::asset
