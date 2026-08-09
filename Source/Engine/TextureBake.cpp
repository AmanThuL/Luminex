#include "Engine/TextureBake.h"

#include "Core/Assert.h"
#include "Engine/Color.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace lmx::engine {

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
// Right-shift with a floor of 1 -- Engine/DdsLoader.h's mip-extent contract, repeated here so the
// baked chain's dimensions match what loadDds expects to find at each level.
uint32_t mipExtent(uint32_t base, uint32_t level) {
    const uint32_t extent = base >> level;
    return extent > 0 ? extent : 1;
}

//======================================================================================================================
// Escapes the two characters JSON requires ("\ and control chars); manifest sources are always
// repo-relative POSIX paths or basenames, so this only ever has to handle the quote and backslash
// in practice, but a stray control byte is escaped too rather than emitting invalid JSON.
std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    return out;
}

// Minimal self-contained SHA-256 (FIPS 180-4), processed in fixed 512-bit chunks with no data-
// dependent branching beyond the algorithm's own definition -- deterministic on every machine.
class Sha256 {
public:
    //==================================================================================================================
    void update(std::span<const std::byte> data) {
        for (const std::byte b : data) {
            m_buffer[m_bufferLen++] = static_cast<uint8_t>(b);
            m_bitLength += 8;
            if (m_bufferLen == 64) {
                processChunk(m_buffer.data());
                m_bufferLen = 0;
            }
        }
    }

    //==================================================================================================================
    std::array<uint8_t, 32> finish() {
        uint64_t bitLength = m_bitLength;
        m_buffer[m_bufferLen++] = 0x80;
        if (m_bufferLen > 56) {
            std::fill(m_buffer.begin() + m_bufferLen, m_buffer.end(), uint8_t{0});
            processChunk(m_buffer.data());
            m_bufferLen = 0;
        }
        std::fill(m_buffer.begin() + m_bufferLen, m_buffer.begin() + 56, uint8_t{0});
        for (int i = 0; i < 8; ++i) {
            m_buffer[56 + i] = static_cast<uint8_t>(bitLength >> (56 - 8 * i));
        }
        processChunk(m_buffer.data());

        std::array<uint8_t, 32> digest{};
        for (int i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>(m_h[i] >> 24);
            digest[i * 4 + 1] = static_cast<uint8_t>(m_h[i] >> 16);
            digest[i * 4 + 2] = static_cast<uint8_t>(m_h[i] >> 8);
            digest[i * 4 + 3] = static_cast<uint8_t>(m_h[i]);
        }
        return digest;
    }

private:
    //==================================================================================================================
    static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    //==================================================================================================================
    void processChunk(const uint8_t* chunk) {
        static constexpr uint32_t kK[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(chunk[i * 4]) << 24) |
                   (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(chunk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3];
        uint32_t e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + s1 + ch + kK[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        m_h[0] += a;
        m_h[1] += b;
        m_h[2] += c;
        m_h[3] += d;
        m_h[4] += e;
        m_h[5] += f;
        m_h[6] += g;
        m_h[7] += h;
    }

    std::array<uint32_t, 8> m_h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint8_t, 64> m_buffer{};
    size_t m_bufferLen = 0;
    uint64_t m_bitLength = 0;
};

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

//======================================================================================================================
std::string sha256Hex(std::span<const std::byte> bytes) {
    Sha256 hasher;
    hasher.update(bytes);
    const std::array<uint8_t, 32> digest = hasher.finish();
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string hex(64, '0');
    for (size_t i = 0; i < digest.size(); ++i) {
        hex[i * 2] = kHexDigits[digest[i] >> 4];
        hex[i * 2 + 1] = kHexDigits[digest[i] & 0xF];
    }
    return hex;
}

} // namespace lmx::engine
