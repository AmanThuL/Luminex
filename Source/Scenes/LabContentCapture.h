//----------------------------------------------------------------------------------------------------------------------
/// @file LabContentCapture.h
/// @brief Captures finalized lab geometry, materials and animation as owned scene-document content.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Document/SceneDocument.h"
#include "Engine/Scene/Scene.h"

#include <span>

namespace lmx::scenes {

/// Generator-authored level-zero pixels in the scene texture slot order.
struct LabTexture {
    std::string name;               ///< Safe ASCII filename stem, unique ignoring case.
    uint32_t width = 0, height = 0; ///< Nonzero pixel dimensions.
    bool srgb = false;              ///< Color texture when true; linear data otherwise.
    bool mipmapped = false;         ///< Rebuild the generator's full mip chain when true.
    std::vector<std::byte> rgba8;   ///< Tightly packed, unconverted RGBA8 pixels.
};

/// Returns MaterialLab's actual ramp, normal and checker generators in texture append order.
std::vector<LabTexture> materialLabTextures();
/// Returns TemporalLab's actual floor checker generator in texture append order.
std::vector<LabTexture> temporalLabTextures();

/// Captures one bare catalog generator with no environment or imported content. The scene must
/// be finalized, prepared and GPU-idle; its live resource slots must be dense and unchanged.
/// Keeps resource order, skipping only the specified object indices. Replaces MaterialLab and
/// TemporalLab's generator or inserts LightLab's field before its retained light generator.
/// Preserves camera/asset/light references and groups directional roles under Lights. Exact
/// orientation refusal, unsupported material state, malformed input or PNG encoding fails.
/// Reads retired scene buffers without changing the scene; performs no filesystem writes.
asset::AssetResult<asset::SceneDocument> captureLabDocument(const asset::SceneDocument& current,
                                                            const engine::Scene& generated,
                                                            std::span<const LabTexture> textures,
                                                            std::span<const uint32_t> skipObjects);

} // namespace lmx::scenes
