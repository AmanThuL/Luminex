//----------------------------------------------------------------------------------------------------------------------
/// @file IblUpload.cpp
/// @brief Implements GPU uploads for CPU-generated environment lighting.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Upload/IblUpload.h"

#include "Core/Assert.h"

#include <glm/gtc/packing.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lmx::engine::ibl {

namespace {

// One RGBA16Float texel; the cube upload below writes rows of these.
constexpr uint64_t kHalf4Stride = 8;
constexpr uint64_t kHalf2Stride = 4;
constexpr float kMaxHalf = 65504.0f;

//======================================================================================================================
// Uploads a cube chain as RGBA16Float. `chain` is mip-major (chain[0] is the base level); the
// staging buffers stay alive across createTexture because TextureMip only borrows their storage.
rojoRHI::Result<std::unique_ptr<rojoRHI::Texture>>
uploadCubeChain(rojoRHI::Device& device, std::span<const asset::ibl::CpuCubemap> chain,
                const std::string& label) {
    LMX_ASSERT(!chain.empty(), "uploadCube: a cube needs at least its base level");
    const uint32_t mipCount = static_cast<uint32_t>(chain.size());

    // Face-major, mip-major within a face -- the order createTexture documents for a cube.
    std::vector<std::vector<uint16_t>> staging;
    staging.reserve(size_t{asset::ibl::kCubeFaceCount} * mipCount);
    for (uint32_t face = 0; face < asset::ibl::kCubeFaceCount; ++face) {
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
            const std::vector<glm::vec4>& texels = chain[mip].faces[face];
            LMX_ASSERT(texels.size() == size_t{chain[mip].faceSize} * chain[mip].faceSize,
                       "uploadCube: a face does not hold faceSize * faceSize texels");
            std::vector<uint16_t> halves(texels.size() * 4);
            for (size_t texel = 0; texel < texels.size(); ++texel) {
                for (int channel = 0; channel < 4; ++channel) {
                    LMX_ASSERT(std::isfinite(texels[texel][channel]) &&
                                   std::abs(texels[texel][channel]) <= kMaxHalf,
                               "uploadCube: a component is not representable by binary16");
                    halves[texel * 4 + static_cast<size_t>(channel)] =
                        glm::packHalf1x16(texels[texel][channel]);
                }
            }
            staging.push_back(std::move(halves));
        }
    }

    std::vector<rojoRHI::TextureMip> mips;
    mips.reserve(staging.size());
    for (uint32_t face = 0; face < asset::ibl::kCubeFaceCount; ++face) {
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
            mips.push_back({.data = staging[size_t{face} * mipCount + mip].data(),
                            .bytesPerRow = uint64_t{chain[mip].faceSize} * kHalf4Stride});
        }
    }
    return device.createTexture({.width = chain[0].faceSize,
                                 .height = chain[0].faceSize,
                                 .format = rojoRHI::Format::RGBA16Float,
                                 .kind = rojoRHI::TextureKind::Cube,
                                 .mipLevels = mipCount,
                                 .sampled = true,
                                 .label = label},
                                mips);
}

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<rojoRHI::Texture>> uploadDfgLut(rojoRHI::Device& device,
                                                                const std::vector<glm::vec2>& lut,
                                                                uint32_t size,
                                                                const std::string& label) {
    std::vector<uint16_t> halves(lut.size() * 2);
    for (size_t texel = 0; texel < lut.size(); ++texel) {
        halves[texel * 2] = glm::packHalf1x16(lut[texel].x);
        halves[texel * 2 + 1] = glm::packHalf1x16(lut[texel].y);
    }
    const rojoRHI::TextureMip mip{.data = halves.data(),
                                  .bytesPerRow = uint64_t{size} * kHalf2Stride};
    const std::array<rojoRHI::TextureMip, 1> mips = {mip};
    return device.createTexture({.width = size,
                                 .height = size,
                                 .format = rojoRHI::Format::RG16Float,
                                 .mipLevels = 1,
                                 .sampled = true,
                                 .label = label},
                                mips);
}

} // namespace

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<rojoRHI::Texture>>
uploadCubemap(rojoRHI::Device& device, const asset::ibl::CpuCubemap& env, std::string_view label) {
    LMX_ASSERT(env.faceSize > 0, "uploadCubemap: the environment cube has no texels");
    return uploadCubeChain(device, std::span<const asset::ibl::CpuCubemap>(&env, 1),
                           std::string(label));
}

//======================================================================================================================
rojoRHI::Result<IblTextures> generate(rojoRHI::Device& device, const asset::ibl::CpuCubemap& env,
                                      std::string_view label, GenerationOptions options) {
    LMX_ASSERT(env.faceSize > 0, "ibl::generate: the environment cube has no texels");
    LMX_ASSERT(options.specularBaseFaceSize >= (1u << (asset::ibl::kSpecularMipCount - 1)),
               "ibl::generate: the specular base extent must hold every roughness mip");
    IblTextures textures;

    const asset::ibl::CpuCubemap& diffuseSource =
        options.irradianceSource != nullptr ? *options.irradianceSource : env;
    const asset::ibl::CpuCubemap irradiance =
        asset::ibl::computeIrradiance(diffuseSource, asset::ibl::kIrradianceFaceSize);
    const std::array<asset::ibl::CpuCubemap, 1> irradianceChain = {irradiance};
    auto irradianceTexture =
        uploadCubeChain(device, irradianceChain, std::string(label) + ".irradiance");
    if (!irradianceTexture) {
        return std::unexpected(std::move(irradianceTexture.error()));
    }
    textures.irradiance = std::move(*irradianceTexture);

    const std::vector<asset::ibl::CpuCubemap> prefiltered = asset::ibl::prefilterSpecular(
        env, options.specularBaseFaceSize, asset::ibl::kSpecularMipCount);
    auto prefilteredTexture =
        uploadCubeChain(device, prefiltered, std::string(label) + ".prefilteredEnv");
    if (!prefilteredTexture) {
        return std::unexpected(std::move(prefilteredTexture.error()));
    }
    textures.prefilteredEnv = std::move(*prefilteredTexture);

    // The DFG table depends on nothing but its own size, so every scene in a process shares one
    // integration of it.
    static const std::vector<glm::vec2> lut = asset::ibl::computeDfgLut(asset::ibl::kDfgLutSize);
    auto lutTexture =
        uploadDfgLut(device, lut, asset::ibl::kDfgLutSize, std::string(label) + ".dfgLut");
    if (!lutTexture) {
        return std::unexpected(std::move(lutTexture.error()));
    }
    textures.dfgLut = std::move(*lutTexture);

    return textures;
}

} // namespace lmx::engine::ibl
