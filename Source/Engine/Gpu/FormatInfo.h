//----------------------------------------------------------------------------------------------------------------------
/// @file FormatInfo.h
/// @brief Declares renderer format names and uncompressed or block storage sizes.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <rojoRHI/RHI.h>

#include <cstdint>
#include <string_view>

namespace lmx::engine {

/// Returns bytes per texel for uncompressed formats, zero for Undefined; BC1 is invalid here.
uint32_t bytesPerTexel(rojoRHI::Format format);
/// Returns bytes per compression block, or bytes per texel for uncompressed formats.
uint32_t bytesPerBlock(rojoRHI::Format format);
/// Returns the square block width in texels: four for BC1, one otherwise.
uint32_t blockExtent(rojoRHI::Format format);
/// Returns whether the format has a depth aspect, with or without stencil.
bool isDepthFormat(rojoRHI::Format format);
/// Returns stable graph/capture format spellings; Undefined is printed as Unknown.
std::string_view formatName(rojoRHI::Format format);

} // namespace lmx::engine
