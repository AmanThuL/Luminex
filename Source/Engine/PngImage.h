//----------------------------------------------------------------------------------------------------------------------
/// @file PngImage.h
/// @brief Declares deterministic SDR PNG encoding and pixel/text decoding.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace lmx::engine {

/// Latin-1 PNG tEXt entry; keyword is 1–79 printable bytes, text contains no NUL.
struct PngTextChunk {
    std::string keyword; ///< PNG keyword, with no leading, trailing, or repeated spaces.
    std::string text;    ///< Uncompressed Latin-1 text payload.
};

/// Owned decoded pixels, with straight alpha and top-down rows; no transfer conversion occurs.
struct PngImage {
    uint32_t width = 0;             ///< Image width in pixels.
    uint32_t height = 0;            ///< Image height in pixels.
    std::vector<uint8_t> rgba;      ///< Four bytes per pixel, encoded R, G, B and alpha.
    std::vector<PngTextChunk> text; ///< Uncompressed tEXt chunks in file order.
};

/// Writes nonempty, tightly packed 8-bit RGBA SDR pixels in top-down order, without premultiplying.
/// Emits IHDR, sRGB (perceptual), gAMA (45455), cHRM (sRGB/D65), tEXt, IDAT, IEND in that order.
/// Replaces the destination deterministically, with no timestamp; invalid input or I/O returns
/// an asset error. Pixels must already be sRGB encoded; this function performs no color transform.
AssetResult<void> writePng(const std::filesystem::path& path, std::span<const uint8_t> rgba,
                           uint32_t width, uint32_t height,
                           std::span<const PngTextChunk> text = {});

/// Decodes PNG pixels to owned 8-bit RGBA without color conversion and retains tEXt entries.
/// Rejects malformed chunks, CRC mismatches, decode failures, and inaccessible files.
AssetResult<PngImage> readPng(const std::filesystem::path& path);

} // namespace lmx::engine
