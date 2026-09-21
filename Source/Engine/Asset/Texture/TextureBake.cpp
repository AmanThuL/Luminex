//----------------------------------------------------------------------------------------------------------------------
/// @file TextureBake.cpp
/// @brief Implements deterministic texture mip baking and DDS output.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Asset/Texture/TextureBake.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/IO/Json.h"
#include "Core/Math/Color.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace lmx::asset {

namespace {

//======================================================================================================================
void putU32(std::vector<std::byte>& bytes, size_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

// One source texel's contribution to a destination texel along a single axis.
struct Tap {
    uint32_t index = 0;
    float weight = 0.0f;
};

struct TapSet {
    std::array<Tap, 3> taps{};
    uint32_t count = 0;
};

//======================================================================================================================
// See TextureBake.h's bakeMips contract comment for the derivation: 2 taps at weight 1/2 for the
// ordinary case, 1 tap at weight 1 when the source axis cannot halve further, 3 taps at weight 1/3
// for the last destination index when the source extent is odd (the trailing unpaired texel folds
// in there).
TapSet axisTaps(uint32_t destIndex, uint32_t destExtent, uint32_t sourceExtent) {
    if (sourceExtent == 1) {
        return {{{{0, 1.0f}, {}, {}}}, 1};
    }
    const uint32_t base = destIndex * 2;
    const bool foldsLastTexel = (destIndex == destExtent - 1) && (sourceExtent % 2 == 1);
    if (foldsLastTexel) {
        constexpr float kThird = 1.0f / 3.0f;
        return {{{{base, kThird}, {base + 1, kThird}, {sourceExtent - 1, kThird}}}, 3};
    }
    return {{{{base, 0.5f}, {base + 1, 0.5f}, {}}}, 2};
}

//======================================================================================================================
float decodeColorChannel(uint8_t byte, BakeMode mode) {
    const float raw = static_cast<float>(byte) / 255.0f;
    return mode == BakeMode::Srgb ? srgbToLinear(raw) : raw;
}

//======================================================================================================================
uint8_t encodeColorChannel(float value, BakeMode mode) {
    const float encoded =
        mode == BakeMode::Srgb ? linearToSrgb(value) : std::clamp(value, 0.0f, 1.0f);
    return static_cast<uint8_t>(encoded * 255.0f + 0.5f);
}

//======================================================================================================================
float decodeRaw(uint8_t byte) {
    return static_cast<float>(byte) / 255.0f;
}

//======================================================================================================================
uint8_t encodeRaw(float value) {
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

//======================================================================================================================
// Encodes a tangent-space component already mapped into [0,1] (caller passes component*0.5+0.5),
// matching MaterialLab.cpp's encodeUnitToByte -- the same rounding convention, kept in step so a
// flat normal's zero component always lands on byte 128, never 127.
uint8_t encodeUnitToByte(float unitComponent) {
    return static_cast<uint8_t>(std::clamp(unitComponent, 0.0f, 1.0f) * 255.0f + 0.5f);
}

//======================================================================================================================
glm::vec3 decodeNormalTexel(const uint8_t* texel) {
    return {static_cast<float>(texel[0]) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(texel[1]) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(texel[2]) / 255.0f * 2.0f - 1.0f};
}

//======================================================================================================================
// Srgb and Linear differ only in the colour-channel transform; both filter alpha raw (Color.h's
// rule: alpha is never colour-space transformed). Accumulation order is y ascending then x
// ascending -- scanline order, fully deterministic.
std::vector<uint8_t> downsampleColor(std::span<const uint8_t> src, uint32_t srcW, uint32_t srcH,
                                     uint32_t dstW, uint32_t dstH, BakeMode mode) {
    std::vector<uint8_t> out(static_cast<size_t>(dstW) * dstH * 4);
    for (uint32_t dy = 0; dy < dstH; ++dy) {
        const TapSet yTaps = axisTaps(dy, dstH, srcH);
        for (uint32_t dx = 0; dx < dstW; ++dx) {
            const TapSet xTaps = axisTaps(dx, dstW, srcW);
            float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (uint32_t yi = 0; yi < yTaps.count; ++yi) {
                const Tap& yTap = yTaps.taps[yi];
                for (uint32_t xi = 0; xi < xTaps.count; ++xi) {
                    const Tap& xTap = xTaps.taps[xi];
                    const float weight = yTap.weight * xTap.weight;
                    const size_t offset = (static_cast<size_t>(yTap.index) * srcW + xTap.index) * 4;
                    sum[0] += weight * decodeColorChannel(src[offset + 0], mode);
                    sum[1] += weight * decodeColorChannel(src[offset + 1], mode);
                    sum[2] += weight * decodeColorChannel(src[offset + 2], mode);
                    sum[3] += weight * decodeRaw(src[offset + 3]);
                }
            }
            const size_t dstOffset = (static_cast<size_t>(dy) * dstW + dx) * 4;
            out[dstOffset + 0] = encodeColorChannel(sum[0], mode);
            out[dstOffset + 1] = encodeColorChannel(sum[1], mode);
            out[dstOffset + 2] = encodeColorChannel(sum[2], mode);
            out[dstOffset + 3] = encodeRaw(sum[3]);
        }
    }
    return out;
}

//======================================================================================================================
// NormalMap's extra step over downsampleColor: the weighted average vector is renormalized before
// encoding, every level, so a mip never drifts off the unit sphere. A near-zero average (opposing
// normals cancelling) falls back to flat +Z rather than producing a NaN from normalizing a
// zero-length vector.
std::vector<uint8_t> downsampleNormal(std::span<const uint8_t> src, uint32_t srcW, uint32_t srcH,
                                      uint32_t dstW, uint32_t dstH) {
    std::vector<uint8_t> out(static_cast<size_t>(dstW) * dstH * 4);
    for (uint32_t dy = 0; dy < dstH; ++dy) {
        const TapSet yTaps = axisTaps(dy, dstH, srcH);
        for (uint32_t dx = 0; dx < dstW; ++dx) {
            const TapSet xTaps = axisTaps(dx, dstW, srcW);
            glm::vec3 sum{0.0f};
            float alphaSum = 0.0f;
            for (uint32_t yi = 0; yi < yTaps.count; ++yi) {
                const Tap& yTap = yTaps.taps[yi];
                for (uint32_t xi = 0; xi < xTaps.count; ++xi) {
                    const Tap& xTap = xTaps.taps[xi];
                    const float weight = yTap.weight * xTap.weight;
                    const size_t offset = (static_cast<size_t>(yTap.index) * srcW + xTap.index) * 4;
                    sum += weight * decodeNormalTexel(&src[offset]);
                    alphaSum += weight * decodeRaw(src[offset + 3]);
                }
            }
            const glm::vec3 normal =
                glm::length(sum) < 1e-8f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::normalize(sum);
            const size_t dstOffset = (static_cast<size_t>(dy) * dstW + dx) * 4;
            out[dstOffset + 0] = encodeUnitToByte(normal.x * 0.5f + 0.5f);
            out[dstOffset + 1] = encodeUnitToByte(normal.y * 0.5f + 0.5f);
            out[dstOffset + 2] = encodeUnitToByte(normal.z * 0.5f + 0.5f);
            out[dstOffset + 3] = encodeRaw(alphaSum);
        }
    }
    return out;
}

//======================================================================================================================
// Right-shift with a floor of 1 -- Engine/Asset/Image/DdsLoader.h's mip-extent contract, repeated
// here so the baked chain's dimensions match what loadDds expects to find at each level.
uint32_t mipExtent(uint32_t base, uint32_t level) {
    const uint32_t extent = base >> level;
    return extent > 0 ? extent : 1;
}

} // namespace

//======================================================================================================================
std::string_view bakeFilterName(BakeMode mode) {
    switch (mode) {
    case BakeMode::Srgb:
        return "box-linear";
    case BakeMode::Linear:
        return "box-raw";
    case BakeMode::NormalMap:
        return "box-normal";
    }
    LMX_ASSERT(false, "bakeFilterName: unknown BakeMode");
    return "";
}

//======================================================================================================================
BakedMipChain bakeMips(std::span<const uint8_t> rgba8, uint32_t width, uint32_t height,
                       BakeMode mode) {
    LMX_ASSERT(width > 0 && height > 0, "bakeMips: width and height must be non-zero");
    LMX_ASSERT(rgba8.size() == static_cast<size_t>(width) * height * 4,
               "bakeMips: rgba8.size() must be width*height*4");

    uint32_t mipLevels = 1;
    for (uint32_t extent = std::max(width, height); extent > 1; extent >>= 1) {
        ++mipLevels;
    }

    std::vector<uint32_t> levelW(mipLevels), levelH(mipLevels);
    for (uint32_t level = 0; level < mipLevels; ++level) {
        levelW[level] = mipExtent(width, level);
        levelH[level] = mipExtent(height, level);
    }

    // Each level derives from the immediately preceding level's already-rounded bytes -- see the
    // header contract comment. Level 0 is the input, copied verbatim.
    std::vector<std::vector<uint8_t>> levels(mipLevels);
    levels[0].assign(rgba8.begin(), rgba8.end());
    for (uint32_t level = 1; level < mipLevels; ++level) {
        levels[level] =
            mode == BakeMode::NormalMap
                ? downsampleNormal(levels[level - 1], levelW[level - 1], levelH[level - 1],
                                   levelW[level], levelH[level])
                : downsampleColor(levels[level - 1], levelW[level - 1], levelH[level - 1],
                                  levelW[level], levelH[level], mode);
    }

    BakedMipChain result;
    result.width = width;
    result.height = height;
    result.mipLevels = mipLevels;

    size_t totalBytes = 0;
    for (const std::vector<uint8_t>& level : levels) {
        totalBytes += level.size();
    }
    result.payload.resize(totalBytes);
    result.mips.reserve(mipLevels);
    size_t offset = 0;
    for (uint32_t level = 0; level < mipLevels; ++level) {
        std::memcpy(result.payload.data() + offset, levels[level].data(), levels[level].size());
        result.mips.push_back(
            {result.payload.data() + offset, static_cast<uint64_t>(levelW[level]) * 4});
        offset += levels[level].size();
    }
    return result;
}

//======================================================================================================================
AssetResult<void> writeDds(std::string_view path, const BakedMipChain& image) {
    LMX_ASSERT(image.width > 0 && image.height > 0 && image.mipLevels >= 1,
               "writeDds: image must have non-zero extents and at least one mip level");
    LMX_ASSERT(image.mips.size() == image.mipLevels,
               "writeDds: mips.size() must equal mipLevels (Tex2D, one face)");

    constexpr size_t kHeaderSize = 128;
    std::vector<std::byte> header(kHeaderSize, std::byte{0});
    std::memcpy(header.data(), "DDS ", 4);
    putU32(header, 4, 124); // DDS_HEADER.dwSize
    constexpr uint32_t kFlagCaps = 0x1, kFlagHeight = 0x2, kFlagWidth = 0x4, kFlagPitch = 0x8,
                       kFlagPixelFormat = 0x1000, kFlagMipMapCount = 0x20000;
    putU32(header, 8,
           kFlagCaps | kFlagHeight | kFlagWidth | kFlagPitch | kFlagPixelFormat | kFlagMipMapCount);
    putU32(header, 12, image.height);
    putU32(header, 16, image.width);
    putU32(header, 20, image.width * 4); // level 0 row pitch, bytes
    putU32(header, 24, 0);               // depth
    putU32(header, 28, image.mipLevels);
    // dwReserved1 (bytes 32..75) stays zero.
    putU32(header, 76, 32);          // DDS_PIXELFORMAT.dwSize
    putU32(header, 80, 0x41);        // DDPF_ALPHAPIXELS | DDPF_RGB
    putU32(header, 84, 0);           // dwFourCC (unused for uncompressed)
    putU32(header, 88, 32);          // dwRGBBitCount
    putU32(header, 92, 0x00FF0000);  // R
    putU32(header, 96, 0x0000FF00);  // G
    putU32(header, 100, 0x000000FF); // B
    putU32(header, 104, 0xFF000000); // A
    constexpr uint32_t kCapsTexture = 0x1000, kCapsComplex = 0x8, kCapsMipmap = 0x400000;
    putU32(header, 108, kCapsTexture | (image.mipLevels > 1 ? (kCapsComplex | kCapsMipmap) : 0));
    putU32(header, 112, 0); // dwCaps2 -- not a cubemap
    // dwCaps3, dwCaps4, dwReserved2 (bytes 116..127) stay zero.

    std::ofstream out{std::string(path), std::ios::binary};
    if (!out) {
        return std::unexpected(
            AssetError{AssetErrorCode::Io, "writeDds: failed to open '" + std::string(path) + "'"});
    }
    out.write(reinterpret_cast<const char*>(header.data()),
              static_cast<std::streamsize>(header.size()));

    // File order for uncompressed A8R8G8B8 is B,G,R,A -- the exact inverse of loadDds's swizzle
    // back to R,G,B,A.
    std::vector<std::byte> filePayload(image.payload.size());
    for (size_t i = 0; i + 4 <= image.payload.size(); i += 4) {
        filePayload[i + 0] = image.payload[i + 2]; // B
        filePayload[i + 1] = image.payload[i + 1]; // G
        filePayload[i + 2] = image.payload[i + 0]; // R
        filePayload[i + 3] = image.payload[i + 3]; // A
    }
    out.write(reinterpret_cast<const char*>(filePayload.data()),
              static_cast<std::streamsize>(filePayload.size()));
    if (!out) {
        return std::unexpected(AssetError{AssetErrorCode::Io,
                                          "writeDds: failed to write '" + std::string(path) + "'"});
    }
    return {};
}

//======================================================================================================================
AssetResult<void> writeManifest(std::string_view path, std::string_view source,
                                std::string_view sourceSha256, BakeMode mode,
                                std::string_view toolVersion) {
    std::ofstream out{std::string(path), std::ios::binary};
    if (!out) {
        return std::unexpected(AssetError{AssetErrorCode::Io, "writeManifest: failed to open '" +
                                                                  std::string(path) + "'"});
    }
    // Literal key order, not a map -- byte-identical output on every run regardless of what an
    // unordered container might do.
    out << "{\"source\":\"" << jsonEscape(source) << "\",\"sourceSha256\":\"" << sourceSha256
        << "\",\"filter\":\"" << bakeFilterName(mode) << "\",\"toolVersion\":\""
        << jsonEscape(toolVersion) << "\"}\n";
    if (!out) {
        return std::unexpected(AssetError{AssetErrorCode::Io, "writeManifest: failed to write '" +
                                                                  std::string(path) + "'"});
    }
    return {};
}

} // namespace lmx::asset
