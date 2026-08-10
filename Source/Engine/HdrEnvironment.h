//----------------------------------------------------------------------------------------------------------------------
/// @file HdrEnvironment.h
/// @brief Declares Radiance HDR decoding and deterministic environment-map conversion.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset.h"
#include "Engine/Ibl.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace lmx::engine {

/// A decoded scene-linear equirectangular environment in top-to-bottom row-major order.
struct HdrEquirectangularImage {
    uint32_t width = 0;              ///< Image width in pixels.
    uint32_t height = 0;             ///< Image height in pixels.
    std::vector<glm::vec3> radiance; ///< Finite, non-negative scene-linear RGB radiance.
};

/// Decodes a Radiance HDR file into finite, non-negative scene-linear RGB values. Missing files,
/// non-HDR inputs, malformed dimensions, and invalid radiance values return an `AssetError` naming
/// `path`.
AssetResult<HdrEquirectangularImage> loadRadianceHdr(std::string_view path);

/// Converts a 2:1 equirectangular image to the project's cubemap face convention using
/// deterministic bilinear reconstruction. Positive `yawRadians` rotates the sampled +X direction
/// toward +Z; `radianceScale` is a non-negative scene-linear multiplier.
AssetResult<ibl::CpuCubemap> equirectangularToCubemap(const HdrEquirectangularImage& image,
                                                      uint32_t faceSize, float yawRadians = 0.0f,
                                                      float radianceScale = 1.0f);

} // namespace lmx::engine
