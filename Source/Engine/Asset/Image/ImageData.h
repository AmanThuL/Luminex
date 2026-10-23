//----------------------------------------------------------------------------------------------------------------------
/// @file ImageData.h
/// @brief Declares the decoded image description shared by the asset loaders and the texture bake.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lmx::asset {

/// Dimensionality of a decoded image.
enum class ImageKind {
    Tex2D, ///< One face.
    Cube,  ///< Six faces in the order +X, -X, +Y, -Y, +Z, -Z.
};

/// One (face, mip level) range inside an ImageData payload.
struct ImageMip {
    uint64_t offset = 0;   ///< Byte offset of the level from the start of the payload.
    uint64_t size = 0;     ///< Byte size of the level.
    uint64_t rowPitch = 0; ///< Bytes between rows (block rows for compressed formats).
};

/// A decoded or baked image: `mips` holds one range of `payload` per (face, mip level), ordered
/// face-major then mip-major -- face0[mip0..mipLevels-1], face1[mip0..mipLevels-1], ... --
/// so mips.size() == mipLevels * faceCount, with 6 faces for Cube and 1 for Tex2D. The ranges
/// are offsets, so a copy of the image stays valid.
struct ImageData {
    uint32_t width = 0;                ///< Base-level width in pixels.
    uint32_t height = 0;               ///< Base-level height in pixels.
    uint32_t mipLevels = 1;            ///< Number of complete mip levels per face.
    ImageKind kind = ImageKind::Tex2D; ///< Image dimensionality.
    std::vector<std::byte> payload;    ///< Tightly packed, face-major then mip-major.
    std::vector<ImageMip> mips;        ///< Ranges of payload, one per (face, mip level).
};

} // namespace lmx::asset
