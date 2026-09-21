//----------------------------------------------------------------------------------------------------------------------
/// @file MaterialRecord.h
/// @brief Declares shared scene-owned material factors and texture identities.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Scene/SceneIds.h"
#include "Engine/Types/AlphaMode.h"

#include <glm/glm.hpp>

#include <optional>

namespace lmx::scene {

/// Scene-linear material factors with optional scene-owned texture identities.
struct MaterialRecord {
    std::optional<TextureId> diffuse;           ///< Null selects the renderer's white fallback.
    std::optional<TextureId> normalMap;         ///< Null disables normal mapping.
    std::optional<TextureId> metallicRoughness; ///< G roughness and B metallic; null is white.
    std::optional<TextureId> occlusion;         ///< Red-channel occlusion; null is white.
    std::optional<TextureId> emissiveMap;       ///< Linear emissive modulation; null is white.
    glm::vec4 albedo{1.0f};                     ///< Linear base color and coverage alpha.
    float roughness = 0.5f;                     ///< Perceptual roughness.
    float metallic = 0.0f;                      ///< Dielectric-to-metal mixture.
    float occlusionStrength = 1.0f;             ///< Occlusion contribution from zero to one.
    glm::vec3 emissive{0.0f};                   ///< Scene-linear emitted radiance.
    glm::mat4 uvTransform{1.0f};                ///< Material texture-coordinate transform.
    render::AlphaMode alphaMode = render::AlphaMode::Opaque; ///< Coverage mode.
    float alphaCutoff = 0.5f; ///< MASK threshold for texture alpha times factor alpha.
    bool doubleSided = false; ///< Two-sided masked rasterization and normals.
};

} // namespace lmx::scene
