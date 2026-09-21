//----------------------------------------------------------------------------------------------------------------------
/// @file DdsLoader.h
/// @brief Declares DDS image data and loading operations.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Asset/Asset.h"
#include <rojoRHI/TextureDesc.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lmx::asset {

/// A decoded DDS image, ready to hand to rojoRHI::Device::createTexture: mips is a span of
/// createTexture-ready views into payload, one entry per (face, mip level), ordered face-major
/// then mip-major -- face0[mip0..mipLevels-1], face1[mip0..mipLevels-1], ... -- matching
/// rojoRHI::Device::createTexture's contract (mips.size() == mipLevels * faceCount, 6 faces for
/// Cube, 1 for Tex2D).
struct DdsImage {
    uint32_t width = 0;     ///< Base-level width in pixels.
    uint32_t height = 0;    ///< Base-level height in pixels.
    uint32_t mipLevels = 1; ///< Number of complete mip levels in `payload`.
    rojoRHI::TextureKind kind = rojoRHI::TextureKind::Tex2D; ///< Uploaded texture dimensionality.
    bool bc1 = false;                      ///< false = RGBA8 (BGRA masks converted on load)
    std::vector<std::byte> payload;        ///< tightly packed, face-major then mip-major
    std::vector<rojoRHI::TextureMip> mips; ///< views into payload, createTexture-ready
};

/// Decodes the legacy DDS formats covered by the loader fixtures: BC1 (DXT1) and uncompressed
/// A8R8G8B8 (swizzled to RGBA on load), 2D and cubemap, with or without a mip chain. Anything else
/// -- other FourCCs (DXT5, DX10 extended headers, ...), other uncompressed pixel layouts, a partial
/// cubemap (not all six faces), a bad magic -- is rejected with a message naming the file, the
/// offending header field, and its byte offset.
AssetResult<DdsImage> loadDds(std::string_view path);

} // namespace lmx::asset
