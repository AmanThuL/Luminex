//----------------------------------------------------------------------------------------------------------------------
/// @file GltfLoader.h
/// @brief Declares decoded glTF asset structures and loading operations.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset.h"
#include "Engine/GeometryGenerator.h"
#include "Engine/SceneAnimation.h"
#include "Render/AlphaMode.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lmx::engine {

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
    render::AlphaMode alphaMode = render::AlphaMode::Opaque; ///< Opaque or alpha-tested coverage.
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

/// One draw: a mesh (GltfScene::meshes index), the material it draws with, and its node's world
/// transform (glTF's full T*R*S chain composed through every ancestor). This is the file's
/// authored rest pose; an animated instance's motion lives in `GltfScene::tracks`, whose first key
/// need not equal this transform.
struct GltfInstance {
    uint32_t meshIndex = 0;     ///< Index into `GltfScene::meshes`.
    uint32_t materialIndex = 0; ///< Index into `GltfScene::materials`.
    glm::mat4 world{1.f};       ///< Flattened object-to-world transform.
};

/// One instance's animation, resampled from the glTF clip into world-space poses. The clip's node
/// hierarchy is evaluated at bake time, so a key needs no parent chain to reconstruct. A STEP clip
/// is resampled into plain keys like a LINEAR one: at kAnimationBakeRate, every one of the clip's
/// held values lands on a key, and the scene clock only ever samples at those key times.
struct GltfAnimationTrack {
    uint32_t instanceIndex = 0; ///< Index into `GltfScene::instances`.
    std::vector<RigidKey> keys; ///< World-space poses sampled at a fixed rate, time-sorted.
};

/// Decoded active scene with flattened mesh, material, image, and instance arrays.
struct GltfScene {
    std::vector<GeoData> meshes; ///< one per active-scene primitive, tangents authored-or-generated
    std::vector<GltfMaterial> materials; ///< Decoded materials referenced by active instances.
    std::vector<GltfImage> images;       ///< indexed like glTF; unreferenced entries stay empty
    std::vector<GltfInstance> instances; ///< node transforms flattened at the clip's rest pose
    /// One entry per instance the first animation moves, in instance order. Empty when the file
    /// has no animation or none of its channels reach an active instance.
    std::vector<GltfAnimationTrack> tracks;
    double animationDuration = 0.0; ///< Baked clip length in seconds; 0 when there are no tracks.
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
/// The file's *first* animation, if it has one, is baked into `tracks`: every LINEAR or STEP
/// translation, rotation and scale channel is evaluated at kAnimationBakeRate over the clip, the
/// affected nodes' world transforms are recomposed through the hierarchy, and each animated
/// instance's world pose becomes a track of world-space keys. Later animations are ignored.
/// CUBICSPLINE samplers, morph-target channels or geometry, skins, an animated node carrying a
/// matrix transform, and a baked pose that does not decompose into translation, rotation and scale
/// all fail with AssetErrorCode::Unsupported.
AssetResult<GltfScene> loadGltf(std::string_view path);

} // namespace lmx::engine
