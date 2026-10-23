//----------------------------------------------------------------------------------------------------------------------
/// @file DdsUpload.cpp
/// @brief Implements DDS decoding and GPU texture upload.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Upload/DdsUpload.h"

#include "Engine/Asset/Image/DdsLoader.h"

#include <rojoRHI/RHI.h>

#include <span>
#include <utility>

namespace lmx::engine {

//======================================================================================================================
std::vector<rojoRHI::TextureData> toTextureData(const asset::ImageData& image) {
    std::vector<rojoRHI::TextureData> mips;
    mips.reserve(image.mips.size());
    for (const asset::ImageMip& mip : image.mips) {
        mips.push_back({.bytes = std::span(image.payload).subspan(mip.offset, mip.size),
                        .bytesPerRow = mip.rowPitch});
    }
    return mips;
}

//======================================================================================================================
asset::AssetResult<GpuTexture> createTextureFromDds(rojoRHI::Device& device, std::string_view path,
                                                    bool srgb, std::string_view label) {
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
        .type = image->kind == asset::ImageKind::Cube ? rojoRHI::TextureType::Cube
                                                      : rojoRHI::TextureType::Tex2D,
        .format = format,
        .extent = {image->width, image->height, 1},
        .mips = image->mipLevels,
        .layers = image->kind == asset::ImageKind::Cube ? 6u : 1u,
        .usage = rojoRHI::TextureUsage::Sampled,
        .label = label,
    };
    auto texture = GpuTexture::create(device, desc, toTextureData(*image));
    if (!texture) {
        return std::unexpected(asset::AssetError{asset::AssetErrorCode::UploadFailed,
                                                 std::move(texture.error().message)});
    }
    return std::move(*texture);
}

} // namespace lmx::engine
