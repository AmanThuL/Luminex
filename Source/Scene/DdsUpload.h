//----------------------------------------------------------------------------------------------------------------------
/// @file DdsUpload.h
/// @brief Declares DDS decoding and GPU texture upload.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Asset/Asset.h"
#include "RHI/RHI.h"

#include <memory>
#include <string_view>

namespace lmx::scene {

/// Loads and uploads a DDS file in one step. srgb selects the _sRGB texture variant for color
/// data (albedo, skybox); pass false for data that must not be gamma-decoded (normal/data maps).
asset::AssetResult<std::unique_ptr<rhi::Texture>>
createTextureFromDds(rhi::Device& device, std::string_view path, bool srgb, std::string_view label);

} // namespace lmx::scene
