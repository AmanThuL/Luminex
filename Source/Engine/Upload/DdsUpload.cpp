//----------------------------------------------------------------------------------------------------------------------
/// @file DdsUpload.cpp
/// @brief Implements DDS decoding and GPU texture upload.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Upload/DdsUpload.h"

#include "Engine/Asset/Image/DdsLoader.h"

#include <utility>

namespace lmx::engine {

//======================================================================================================================
asset::AssetResult<std::unique_ptr<rojoRHI::Texture>> createTextureFromDds(rojoRHI::Device& device,
                                                                           std::string_view path,
                                                                           bool srgb,
                                                                           std::string_view label) {
    asset::AssetResult<asset::DdsImage> image = asset::loadDds(path);
    if (!image) {
        return std::unexpected(image.error());
    }

    rojoRHI::Format format{};
    if (image->bc1) {
        format = srgb ? rojoRHI::Format::BC1Unorm_sRGB : rojoRHI::Format::BC1Unorm;
    } else {
        format = srgb ? rojoRHI::Format::RGBA8Unorm_sRGB : rojoRHI::Format::RGBA8Unorm;
    }

    const rojoRHI::TextureDesc desc{
        .width = image->width,
        .height = image->height,
        .format = format,
        .kind = image->kind,
        .mipLevels = image->mipLevels,
        .sampled = true,
        .label = label,
    };
    auto texture = device.createTexture(desc, image->mips);
    if (!texture) {
        return std::unexpected(asset::AssetError{asset::AssetErrorCode::UploadFailed,
                                                 std::move(texture.error().message)});
    }
    return std::move(*texture);
}

} // namespace lmx::engine
