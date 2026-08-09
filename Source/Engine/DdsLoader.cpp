#include "Engine/DdsLoader.h"

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace lmx::engine {

namespace {

constexpr uint64_t kHeaderSize = 128; // magic(4) + DDS_HEADER(124), no DX10 extension

// Byte offsets include the four-byte legacy DDS magic prefix.
constexpr uint64_t kOffsetMagic = 0;
constexpr uint64_t kOffsetHeight = 12;
constexpr uint64_t kOffsetWidth = 16;
constexpr uint64_t kOffsetMipMapCount = 28;
constexpr uint64_t kOffsetPixelFormatFlags = 80;
constexpr uint64_t kOffsetFourCC = 84;
constexpr uint64_t kOffsetRGBBitCount = 88;
constexpr uint64_t kOffsetRBitMask = 92;
constexpr uint64_t kOffsetGBitMask = 96;
constexpr uint64_t kOffsetBBitMask = 100;
constexpr uint64_t kOffsetABitMask = 104;
constexpr uint64_t kOffsetCaps2 = 112;

constexpr uint32_t kPixelFormatFourCC = 0x4;
constexpr uint32_t kPixelFormatRGBA = 0x41;
constexpr uint32_t kCaps2CubemapBit = 0x200;
constexpr uint32_t kCaps2AllFacesMask = 0xFE00;
constexpr uint32_t kCubeFaceCount = 6;

// Little-endian A8R8G8B8 stores BGRA bytes; decode swizzles them to RGBA.
constexpr uint32_t kA8R8G8B8RMask = 0x00FF0000;
constexpr uint32_t kA8R8G8B8GMask = 0x0000FF00;
constexpr uint32_t kA8R8G8B8BMask = 0x000000FF;
constexpr uint32_t kA8R8G8B8AMask = 0xFF000000;

//======================================================================================================================
std::unexpected<AssetError> fail(std::string_view path, std::string message,
                                 AssetErrorCode code = AssetErrorCode::Malformed) {
    return std::unexpected(
        AssetError{code, "dds '" + std::string(path) + "': " + std::move(message)});
}

//======================================================================================================================
uint32_t readU32LE(const std::vector<std::byte>& bytes, uint64_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    // DDS and the target host are both little-endian.
    return value;
}

//======================================================================================================================
std::string toHex(uint32_t value) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        out.push_back(kDigits[(value >> shift) & 0xF]);
    }
    return out;
}

//======================================================================================================================
uint32_t ceilDiv4(uint32_t x) {
    return (x + 3) / 4;
}

//======================================================================================================================
uint32_t bc1BytesPerRow(uint32_t width) {
    return ceilDiv4(width) * 8;
}

//======================================================================================================================
uint64_t bc1LevelBytes(uint32_t width, uint32_t height) {
    return static_cast<uint64_t>(ceilDiv4(width)) * ceilDiv4(height) * 8;
}

//======================================================================================================================
// Level N of a chain halves the extent and never drops below one texel.
uint32_t mipExtent(uint32_t base, uint32_t level) {
    const uint32_t extent = base >> level;
    return extent > 0 ? extent : 1;
}

} // namespace

//======================================================================================================================
AssetResult<DdsImage> loadDds(std::string_view path) {
    std::ifstream fin{std::string(path), std::ios::binary};
    if (!fin) {
        return fail(path, "failed to open file", AssetErrorCode::NotFound);
    }
    std::vector<std::byte> bytes;
    fin.seekg(0, std::ios::end);
    const auto size = fin.tellg();
    if (size < 0) {
        return fail(path, "failed to determine file size", AssetErrorCode::Io);
    }
    bytes.resize(static_cast<size_t>(size));
    fin.seekg(0, std::ios::beg);
    fin.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!fin) {
        return fail(path, "failed to read file contents", AssetErrorCode::Io);
    }

    if (bytes.size() < kHeaderSize) {
        return fail(path, "file too small for a DDS header (need " + std::to_string(kHeaderSize) +
                              " bytes, got " + std::to_string(bytes.size()) + ") at offset 0");
    }
    if (std::memcmp(bytes.data() + kOffsetMagic, "DDS ", 4) != 0) {
        return fail(path, "bad magic at offset 0 (expected 'DDS ')");
    }

    const uint32_t height = readU32LE(bytes, kOffsetHeight);
    const uint32_t width = readU32LE(bytes, kOffsetWidth);
    if (width == 0 || height == 0) {
        return fail(path, "width/height must be non-zero (width @16 = " + std::to_string(width) +
                              ", height @12 = " + std::to_string(height) + ")");
    }
    // 0 (DDSD_MIPMAPCOUNT unset) and 1 both mean "just the base level".
    uint32_t mipMapCount = readU32LE(bytes, kOffsetMipMapCount);
    if (mipMapCount == 0) {
        mipMapCount = 1;
    }

    const uint32_t caps2 = readU32LE(bytes, kOffsetCaps2);
    const bool isCube = (caps2 & kCaps2CubemapBit) != 0;
    if (isCube && (caps2 & kCaps2AllFacesMask) != kCaps2AllFacesMask) {
        return fail(path, "partial cubemap at offset " + std::to_string(kOffsetCaps2) +
                              " (caps2 = " + toHex(caps2) +
                              "); all six faces are required (mask 0xfe00)");
    }
    const uint32_t faceCount = isCube ? kCubeFaceCount : 1;

    DdsImage image;
    image.width = width;
    image.height = height;
    image.mipLevels = mipMapCount;
    image.kind = isCube ? rhi::TextureKind::Cube : rhi::TextureKind::Tex2D;

    const uint32_t pfFlags = readU32LE(bytes, kOffsetPixelFormatFlags);
    char fourCC[5] = {};
    std::memcpy(fourCC, bytes.data() + kOffsetFourCC, 4);

    if ((pfFlags & kPixelFormatFourCC) != 0 && std::string_view(fourCC, 4) == "DXT1") {
        image.bc1 = true;
    } else if ((pfFlags & kPixelFormatRGBA) == kPixelFormatRGBA) {
        image.bc1 = false;
        const uint32_t rgbBitCount = readU32LE(bytes, kOffsetRGBBitCount);
        const uint32_t rMask = readU32LE(bytes, kOffsetRBitMask);
        const uint32_t gMask = readU32LE(bytes, kOffsetGBitMask);
        const uint32_t bMask = readU32LE(bytes, kOffsetBBitMask);
        const uint32_t aMask = readU32LE(bytes, kOffsetABitMask);
        if (rgbBitCount != 32 || rMask != kA8R8G8B8RMask || gMask != kA8R8G8B8GMask ||
            bMask != kA8R8G8B8BMask || aMask != kA8R8G8B8AMask) {
            return fail(path,
                        "unsupported uncompressed pixel format at offset " +
                            std::to_string(kOffsetRGBBitCount) +
                            " (only 32-bit A8R8G8B8 is accepted; got " +
                            std::to_string(rgbBitCount) + "-bit, R=" + toHex(rMask) + " G=" +
                            toHex(gMask) + " B=" + toHex(bMask) + " A=" + toHex(aMask) + ")",
                        AssetErrorCode::Unsupported);
        }
    } else {
        return fail(path,
                    "unsupported pixel format flags " + toHex(pfFlags) + " at offset " +
                        std::to_string(kOffsetPixelFormatFlags) + " (fourCC '" +
                        std::string(fourCC, 4) +
                        "'); only BC1 (DXT1) and uncompressed A8R8G8B8 are accepted",
                    AssetErrorCode::Unsupported);
    }

    // Every cube face shares the same mip-chain layout.
    std::vector<uint64_t> levelBytes(mipMapCount);
    std::vector<uint32_t> levelStride(mipMapCount);
    uint64_t perFaceBytes = 0;
    for (uint32_t level = 0; level < mipMapCount; ++level) {
        const uint32_t w = mipExtent(width, level);
        const uint32_t h = mipExtent(height, level);
        if (image.bc1) {
            levelStride[level] = bc1BytesPerRow(w);
            levelBytes[level] = bc1LevelBytes(w, h);
        } else {
            levelStride[level] = w * 4;
            levelBytes[level] = static_cast<uint64_t>(levelStride[level]) * h;
        }
        perFaceBytes += levelBytes[level];
    }
    const uint64_t totalPayloadBytes = perFaceBytes * faceCount;

    if (bytes.size() < kHeaderSize + totalPayloadBytes) {
        return fail(path, "truncated pixel data at offset " + std::to_string(kHeaderSize) +
                              " (need " + std::to_string(totalPayloadBytes) + " bytes for " +
                              std::to_string(faceCount) + " face(s) x " +
                              std::to_string(mipMapCount) + " mip(s), file has " +
                              std::to_string(bytes.size() - kHeaderSize) + ")");
    }

    std::vector<std::byte> payload(totalPayloadBytes);
    if (image.bc1) {
        std::memcpy(payload.data(), bytes.data() + kHeaderSize, totalPayloadBytes);
    } else {
        // Swap the R and B bytes of every pixel (A8R8G8B8's B,G,R,A file order -> R,G,B,A).
        const std::byte* src = bytes.data() + kHeaderSize;
        for (uint64_t i = 0; i + 4 <= totalPayloadBytes; i += 4) {
            payload[i + 0] = src[i + 2]; // R
            payload[i + 1] = src[i + 1]; // G
            payload[i + 2] = src[i + 0]; // B
            payload[i + 3] = src[i + 3]; // A
        }
    }

    std::vector<rhi::TextureMip> mips;
    mips.reserve(static_cast<size_t>(mipMapCount) * faceCount);
    uint64_t offset = 0;
    for (uint32_t face = 0; face < faceCount; ++face) {
        for (uint32_t level = 0; level < mipMapCount; ++level) {
            mips.push_back({payload.data() + offset, levelStride[level]});
            offset += levelBytes[level];
        }
    }

    image.payload = std::move(payload);
    image.mips = std::move(mips);
    return image;
}

//======================================================================================================================
AssetResult<std::unique_ptr<rhi::Texture>> createTextureFromDds(rhi::Device& device,
                                                                std::string_view path, bool srgb,
                                                                std::string_view label) {
    AssetResult<DdsImage> image = loadDds(path);
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
        return std::unexpected(
            AssetError{AssetErrorCode::UploadFailed, std::move(texture.error().message)});
    }
    return std::move(*texture);
}

} // namespace lmx::engine
