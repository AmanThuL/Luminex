//----------------------------------------------------------------------------------------------------------------------
/// @file DdsUpload.cpp
/// @brief Implements DDS decoding and GPU texture upload.
//----------------------------------------------------------------------------------------------------------------------

#include "Scene/DdsUpload.h"

#include "Asset/DdsLoader.h"

#include <utility>

namespace lmx::scene {

//======================================================================================================================
asset::AssetResult<std::unique_ptr<rhi::Texture>> createTextureFromDds(rhi::Device& device,
                                                                       std::string_view path,
                                                                       bool srgb,
                                                                       std::string_view label) {
    asset::AssetResult<asset::DdsImage> image = asset::loadDds(path);
    if (!image) {
        return std::unexpected(image.error());
    }

    rhi::Format format{};
    if (image->bc1) {
        format = srgb ? rhi::Format::BC1Unorm_sRGB : rhi::Format::BC1Unorm;
    } else {
        format = srgb ? rhi::Format::RGBA8Unorm_sRGB : rhi::Format::RGBA8Unorm;
    }

    const rhi::TextureDesc desc{
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

} // namespace lmx::scene
