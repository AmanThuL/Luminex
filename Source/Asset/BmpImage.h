//----------------------------------------------------------------------------------------------------------------------
/// @file BmpImage.h
/// @brief Declares deterministic top-down BGRA bitmap writing.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace lmx::asset {

/// Writes BGRA8 pixels as a top-down 32-bit BMP; returns false on open or write failure.
bool writeBmp(const std::filesystem::path& path, const std::vector<uint8_t>& bgra, uint32_t width,
              uint32_t height);

} // namespace lmx::asset
