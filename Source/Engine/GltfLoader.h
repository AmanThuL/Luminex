//----------------------------------------------------------------------------------------------------------------------
/// @file GltfLoader.h
/// @brief Declares decoded glTF asset structures and loading operations.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset.h"
#include "Engine/GeometryGenerator.h"

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
    glm::vec3 emissiveFactor{0.f};  ///< linear, per the glTF spec -- not an sRGB-authored constant
};

/// stb-decoded, always tightly packed RGBA8 (4 bytes/pixel, no row padding). Entries not referenced
/// by the active scene remain empty; preserving the glTF image index avoids a second remapping
/// table.
struct GltfImage {
    uint32_t width = 0, height = 0; ///< Decoded dimensions in pixels.
    std::vector<std::byte> rgba8;   ///< Tightly packed RGBA8 texels.
};

/// One draw: a mesh (GltfScene::meshes index), the material it draws with, and its node's
/// world transform (glTF's full T*R*S chain composed through every ancestor -- no animation,
/// so this is a static snapshot of the file's authored pose).
struct GltfInstance {
    uint32_t meshIndex = 0;     ///< Index into `GltfScene::meshes`.
    uint32_t materialIndex = 0; ///< Index into `GltfScene::materials`.
    glm::mat4 world{1.f};       ///< Flattened object-to-world transform.
};

/// Decoded active scene with flattened mesh, material, image, and instance arrays.
struct GltfScene {
    std::vector<GeoData> meshes; ///< one per active-scene primitive, tangents authored-or-generated
    std::vector<GltfMaterial> materials; ///< Decoded materials referenced by active instances.
    std::vector<GltfImage> images;       ///< indexed like glTF; unreferenced entries stay empty
    std::vector<GltfInstance> instances; ///< node transforms flattened (no animation)
};

/// Loads a .glb (embedded buffers/images) or .gltf (+ external .bin and image files, resolved
/// relative to path's directory) via cgltf + stb_image. Only meshes, materials, and base-color,
/// normal, metallic-roughness, occlusion, or emissive images reachable from the active scene are
/// decoded. Every used primitive becomes GeoData;
/// primitives without a TANGENT attribute get one generated (per-triangle from UV deltas,
/// accumulated per-vertex, Gram-Schmidt orthogonalized against the normal, w from the bitangent
/// cross sign; degenerate/absent UVs fall back to cross(up, normal)). Node transforms are
/// flattened after an active-scene traversal via cgltf's ancestor-chain composition; animation is
/// not evaluated.
AssetResult<GltfScene> loadGltf(std::string_view path);

} // namespace lmx::engine
