//----------------------------------------------------------------------------------------------------------------------
/// @file Texture.h
/// @brief Declares texture creation placed at an explicit address by the caller.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/Result.h"
#include "NoApi/Types.h"

#include <cstdint>
#include <string_view>

namespace lmx::noapi {

/// Describes a texture's shape, format, and declared usage.
struct TextureDesc {
    TextureKind kind = TextureKind::Texture2D; ///< Dimensionality of the texture.
    Extent3D extent{};                         ///< Size of mip level zero, in texels.
    uint32_t mipCount = 1;                     ///< Number of mip levels; must be at least one.
    uint32_t arrayLayers = 1;                  ///< Array layers; six for a cube map.
    uint32_t sampleCount = 1;          ///< Samples per texel; one for non-multisampled textures.
    Format format = Format::Undefined; ///< Texel format; must not be `Format::Undefined`.
    TextureUsage usage =
        TextureUsage::None; ///< Every use the texture is allowed; must be non-empty.
    std::string_view label; ///< Debug label; must be non-empty.
};

/// Describes how a bindless table slot reinterprets a texture.
///
/// A view never allocates. It selects a subrange of an existing texture and optionally reinterprets
/// its format, which is the only place in this interface where a subresource range is named.
struct TextureViewDesc {
    Format format =
        Format::Undefined; ///< Reinterpreted format, or `Format::Undefined` to keep the texture's.
    uint32_t baseMipLevel = 0;         ///< First mip level the view exposes.
    uint32_t mipCount = kAllMipLevels; ///< Mip levels the view exposes, from `baseMipLevel`.
    uint32_t baseArrayLayer = 0;       ///< First array layer the view exposes.
    /// Array layers the view exposes, from `baseArrayLayer`.
    uint32_t arrayLayers = kAllArrayLayers;
    bool storage = false; ///< Whether the slot is written rather than sampled.
};

/// Identifies a texel region of one texture subresource for copy commands.
struct TextureRegion {
    uint32_t mipLevel = 0;   ///< Mip level the region addresses.
    uint32_t arrayLayer = 0; ///< Array layer or cube face the region addresses.
    Origin3D origin{};       ///< Region origin within the subresource, in texels.
    Extent3D extent{};       ///< Region size, in texels.
};

/// Describes how texel rows are laid out in linear memory for a copy.
struct MemoryImageLayout {
    uint64_t bytesPerRow = 0; ///< Distance between consecutive texel rows, in bytes.
    /// Distance between consecutive array slices, in bytes; zero for 2D.
    uint64_t bytesPerImage = 0;
};

/// Returns the byte size and address alignment a texture with `desc` requires.
///
/// The caller allocates that memory itself and passes the resulting address to `createTexture`.
/// Size and alignment are hardware-specific because textures are stored swizzled and may carry
/// compression metadata, which is the reason this query exists at all.
SizeAlign textureSizeAlign(const Device* device, const TextureDesc& desc);

/// Creates a texture whose storage begins at `placement`.
///
/// `placement` must be an address inside a `MemoryKind::Private` allocation, must satisfy the
/// alignment reported by `textureSizeAlign`, and must have at least the reported size available;
/// all three are caller contracts and assert. The texture does not own the memory and does not
/// keep it alive.
///
/// Fails with `ErrorCode::ResourceCreationFailed` when the device rejects the descriptor and with
/// `ErrorCode::InvalidDesc` when the format and usage combination is not supported.
Result<Texture*> createTexture(Device* device, const TextureDesc& desc, GpuAddress placement);

/// Destroys a texture created by `createTexture`.
///
/// Every submission referencing the texture must be retired, and every bindless slot holding a
/// view of it must be cleared. Both are caller contracts and assert.
void destroyTexture(Device* device, Texture* texture);

/// Returns the descriptor a texture was created with.
const TextureDesc& textureDesc(const Texture* texture);

} // namespace lmx::noapi
