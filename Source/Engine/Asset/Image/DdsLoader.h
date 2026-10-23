//----------------------------------------------------------------------------------------------------------------------
/// @file DdsLoader.h
/// @brief Declares DDS image data and loading operations.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Asset.h"
#include "Engine/Asset/Image/ImageData.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lmx::asset {

/// A decoded DDS image: the ImageData contract (offsets into payload, face-major then mip-major)
/// plus whether the texels are BC1.
struct DdsImage : ImageData {
    bool bc1 = false; ///< false = RGBA8 (BGRA masks converted on load)
};

/// Decodes the legacy DDS formats covered by the loader fixtures: BC1 (DXT1) and uncompressed
/// A8R8G8B8 (swizzled to RGBA on load), 2D and cubemap, with or without a mip chain. Anything else
/// -- other FourCCs (DXT5, DX10 extended headers, ...), other uncompressed pixel layouts, a partial
/// cubemap (not all six faces), a bad magic -- is rejected with a message naming the file, the
/// offending header field, and its byte offset.
AssetResult<DdsImage> loadDds(std::string_view path);

} // namespace lmx::asset
