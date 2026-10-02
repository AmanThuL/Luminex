//----------------------------------------------------------------------------------------------------------------------
/// @file SceneDocumentContent.h
/// @brief Declares immutable CPU geometry and images plus authored document mesh and material rows.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Model/GltfLoader.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lmx::asset {

/// Latest scene-document schema supporting saved geometry and material content.
inline constexpr uint32_t kSceneDocumentSchema = 2;

/// Immutable encoded PNG and its decoded level-zero pixels, without transfer conversion.
struct DocImage {
    std::string name;               ///< Authored image name, also used for saved PNG filenames.
    std::string sha256;             ///< Lowercase SHA-256 of file bytes.
    uint32_t width = 0, height = 0; ///< Decoded dimensions in pixels.
    bool mipmapped = true;          ///< Generate a full mip chain when instantiated.
    std::vector<std::byte> file;    ///< Original PNG bytes, retained for Save As.
    std::vector<std::byte> rgba8;   ///< Tightly packed four-byte pixels, without color conversion.
};

/// Shared immutable payload; copies of an editable document retain this allocation.
struct DocContent {
    std::vector<GeoData> geometries; ///< Unique accessor sets in first-mesh order.
    std::vector<DocImage> images;    ///< Images in source order.
    std::string geometrySha256;      ///< Lowercase SHA-256 of the external geometry buffer.
};

/// Authored glTF material whose texture indices address DocContent::images.
struct DocMaterial {
    std::string name;              ///< Authored display name.
    GltfMaterial values;           ///< Factors and source-image indices.
    float emissiveStrength = 1.0f; ///< Nonnegative KHR_materials_emissive_strength multiplier.
};

/// One document primitive referring to shared geometry and an authored material.
struct DocMesh {
    std::string name;      ///< Authored mesh name.
    uint32_t geometry = 0; ///< Index into DocContent::geometries.
    uint32_t material = 0; ///< Index into SceneDocument::materials.
};

/// Authored motion-vector behavior, independent of editor mobility.
enum class DocMotion {
    Rigid,  ///< The object's rigid transform defines its motion.
    Invalid ///< Motion history is invalid for this object.
};

} // namespace lmx::asset
