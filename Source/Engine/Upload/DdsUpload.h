//----------------------------------------------------------------------------------------------------------------------
/// @file DdsUpload.h
/// @brief Declares DDS decoding and GPU texture upload.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Asset.h"
#include "Engine/Gpu/GpuTexture.h"
#include <rojoRHI/RHI.h>

#include <string_view>

namespace lmx::engine {

/// Loads and uploads a DDS file in one step. srgb selects the _sRGB texture variant for color
/// data (albedo, skybox); pass false for data that must not be gamma-decoded (normal/data maps).
asset::AssetResult<GpuTexture> createTextureFromDds(rojoRHI::Device& device, std::string_view path,
                                                    bool srgb, std::string_view label);

} // namespace lmx::engine
