//----------------------------------------------------------------------------------------------------------------------
/// @file DdsLoader.h
/// @brief Declares DDS image data and loading operations.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset.h"
#include "RHI/RHI.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace lmx::engine {

/// A decoded DDS image, ready to hand to rhi::Device::createTexture: mips is a span of
/// createTexture-ready views into payload, one entry per (face, mip level), ordered face-major
/// then mip-major -- face0[mip0..mipLevels-1], face1[mip0..mipLevels-1], ... -- matching
/// rhi::Device::createTexture's contract (mips.size() == mipLevels * faceCount, 6 faces for
/// Cube, 1 for Tex2D).
struct DdsImage {
    uint32_t width = 0;     ///< Base-level width in pixels.
    uint32_t height = 0;    ///< Base-level height in pixels.
    uint32_t mipLevels = 1; ///< Number of complete mip levels in `payload`.
    rhi::TextureKind kind = rhi::TextureKind::Tex2D; ///< Uploaded texture dimensionality.
    bool bc1 = false;                  ///< false = RGBA8 (BGRA masks converted on load)
    std::vector<std::byte> payload;    ///< tightly packed, face-major then mip-major
    std::vector<rhi::TextureMip> mips; ///< views into payload, createTexture-ready
};

/// Decodes the legacy DDS formats covered by the loader fixtures: BC1 (DXT1) and uncompressed
/// A8R8G8B8 (swizzled to RGBA on load), 2D and cubemap, with or without a mip chain. Anything else
/// -- other FourCCs (DXT5, DX10 extended headers, ...), other uncompressed pixel layouts, a partial
/// cubemap (not all six faces), a bad magic -- is rejected with a message naming the file, the
/// offending header field, and its byte offset.
AssetResult<DdsImage> loadDds(std::string_view path);

/// Loads and uploads a DDS file in one step. srgb selects the _sRGB texture variant for color
/// data (albedo, skybox); pass false for data that must not be gamma-decoded (normal/data maps).
AssetResult<std::unique_ptr<rhi::Texture>>
createTextureFromDds(rhi::Device& device, std::string_view path, bool srgb, std::string_view label);

} // namespace lmx::engine
