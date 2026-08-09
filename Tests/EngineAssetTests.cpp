#include <catch2/catch_test_macros.hpp>

#include "Engine/DdsLoader.h"
#include "RHI/RHI.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace lmx::engine;
namespace rhi = lmx::rhi;

namespace {

// Writes bytes to a temporary file so loader tests exercise real file I/O.
class TempFile {
public:
    //==================================================================================================================
    TempFile(std::string_view suffix, std::string_view text) : m_path(uniquePath(suffix)) {
        std::ofstream out(m_path, std::ios::binary);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    //==================================================================================================================
    TempFile(std::string_view suffix, const std::vector<std::byte>& bytes)
        : m_path(uniquePath(suffix)) {
        std::ofstream out(m_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    //==================================================================================================================
    ~TempFile() { std::filesystem::remove(m_path); }

    //==================================================================================================================
    TempFile(const TempFile&) = delete;

    //==================================================================================================================
    TempFile& operator=(const TempFile&) = delete;

    //==================================================================================================================
    std::string string() const { return m_path.string(); }

private:
    //==================================================================================================================
    static std::filesystem::path uniquePath(std::string_view suffix) {
        static std::atomic<uint32_t> counter{0};
        return std::filesystem::temp_directory_path() /
               ("lmx-engine-test-" + std::to_string(counter++) + std::string(suffix));
    }
    std::filesystem::path m_path;
};

// Builds a minimal 128-byte DDS header (magic + DDS_HEADER, no DX10 extension) at the field
// offsets DdsLoader.cpp reads: magic@0, height@12, width@16, mipMapCount@28, pixel-format
// flags@80, fourCC@84, RGB bit count@88, R/G/B/A masks@92/96/100/104, caps2@112.
struct DdsHeaderFixture {
    uint32_t width = 4, height = 4, mipMapCount = 1;
    uint32_t pfFlags = 0x4; // DDPF_FOURCC
    std::array<char, 4> fourCC{'D', 'X', 'T', '1'};
    uint32_t rgbBitCount = 0, rMask = 0, gMask = 0, bMask = 0, aMask = 0;
    uint32_t caps2 = 0;
    bool badMagic = false;

    //==================================================================================================================
    static void putU32(std::vector<std::byte>& bytes, size_t offset, uint32_t value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    //==================================================================================================================
    std::vector<std::byte> header() const {
        std::vector<std::byte> bytes(128, std::byte{0});
        std::memcpy(bytes.data(), badMagic ? "BAD " : "DDS ", 4);
        putU32(bytes, 4, 124); // dwSize
        putU32(bytes, 12, height);
        putU32(bytes, 16, width);
        putU32(bytes, 28, mipMapCount);
        putU32(bytes, 76, 32); // DDS_PIXELFORMAT.dwSize
        putU32(bytes, 80, pfFlags);
        std::memcpy(bytes.data() + 84, fourCC.data(), 4);
        putU32(bytes, 88, rgbBitCount);
        putU32(bytes, 92, rMask);
        putU32(bytes, 96, gMask);
        putU32(bytes, 100, bMask);
        putU32(bytes, 104, aMask);
        putU32(bytes, 112, caps2);
        return bytes;
    }
};

//======================================================================================================================
uint32_t ceilDiv4(uint32_t x) {
    return (x + 3) / 4;
}

//======================================================================================================================
// Stamp each face-major, mip-major BC1 block with face*16+level so tests can verify layout.
std::vector<std::byte> bc1Payload(uint32_t width, uint32_t height, uint32_t mipLevels,
                                  uint32_t faceCount) {
    std::vector<std::byte> bytes;
    for (uint32_t face = 0; face < faceCount; ++face) {
        uint32_t w = width, h = height;
        for (uint32_t level = 0; level < mipLevels; ++level) {
            const size_t blockBytes = static_cast<size_t>(ceilDiv4(w)) * ceilDiv4(h) * 8;
            bytes.resize(bytes.size() + blockBytes,
                         std::byte{static_cast<unsigned char>(face * 16 + level)});
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
    }
    return bytes;
}

} // namespace

//======================================================================================================================
TEST_CASE("loadDds accepts a 4x4 single-mip BC1 2D texture", "[engine]") {
    DdsHeaderFixture fixture;
    fixture.width = 4;
    fixture.height = 4;
    fixture.mipMapCount = 1;
    std::vector<std::byte> bytes = fixture.header();
    const std::vector<std::byte> payload = bc1Payload(4, 4, 1, 1);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const TempFile file(".dds", bytes);

    const AssetResult<DdsImage> result = loadDds(file.string());
    REQUIRE(result.has_value());
    REQUIRE(result->width == 4);
    REQUIRE(result->height == 4);
    REQUIRE(result->mipLevels == 1);
    REQUIRE(result->kind == rhi::TextureKind::Tex2D);
    REQUIRE(result->bc1);
    REQUIRE(result->mips.size() == 1);
    REQUIRE(result->mips[0].bytesPerRow == 8);
    REQUIRE(result->payload.size() == 8);
}

//======================================================================================================================
TEST_CASE("loadDds accepts a 1x1 RGBA8 (A8R8G8B8) texture, swizzled to RGBA", "[engine]") {
    DdsHeaderFixture fixture;
    fixture.width = 1;
    fixture.height = 1;
    fixture.mipMapCount = 1;
    fixture.pfFlags = 0x41; // DDPF_ALPHAPIXELS | DDPF_RGB
    fixture.rgbBitCount = 32;
    fixture.rMask = 0x00FF0000;
    fixture.gMask = 0x0000FF00;
    fixture.bMask = 0x000000FF;
    fixture.aMask = 0xFF000000;
    std::vector<std::byte> bytes = fixture.header();
    // File byte order for A8R8G8B8 is B,G,R,A (little-endian dword 0xAARRGGBB): distinct values
    // per channel so a swizzle bug (e.g. leaving B/R swapped) shows up as a wrong byte, not a
    // coincidentally-right one.
    bytes.push_back(std::byte{0x33}); // B
    bytes.push_back(std::byte{0x22}); // G
    bytes.push_back(std::byte{0x11}); // R
    bytes.push_back(std::byte{0x44}); // A
    const TempFile file(".dds", bytes);

    const AssetResult<DdsImage> result = loadDds(file.string());
    REQUIRE(result.has_value());
    REQUIRE(result->width == 1);
    REQUIRE(result->height == 1);
    REQUIRE(result->mipLevels == 1);
    REQUIRE(result->kind == rhi::TextureKind::Tex2D);
    REQUIRE_FALSE(result->bc1);
    REQUIRE(result->mips.size() == 1);
    REQUIRE(result->mips[0].bytesPerRow == 4);
    REQUIRE(result->payload.size() == 4);
    REQUIRE(static_cast<unsigned char>(result->payload[0]) == 0x11); // R
    REQUIRE(static_cast<unsigned char>(result->payload[1]) == 0x22); // G
    REQUIRE(static_cast<unsigned char>(result->payload[2]) == 0x33); // B
    REQUIRE(static_cast<unsigned char>(result->payload[3]) == 0x44); // A
}

//======================================================================================================================
TEST_CASE("loadDds accepts an 8x8 BC1 cubemap with 3 mips, correctly face-major then mip-major",
          "[engine]") {
    DdsHeaderFixture fixture;
    fixture.width = 8;
    fixture.height = 8;
    fixture.mipMapCount = 3;
    fixture.caps2 = 0xFE00; // DDSCAPS2_CUBEMAP | all six face bits
    std::vector<std::byte> bytes = fixture.header();
    const std::vector<std::byte> payload = bc1Payload(8, 8, 3, 6);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const TempFile file(".dds", bytes);

    const AssetResult<DdsImage> result = loadDds(file.string());
    REQUIRE(result.has_value());
    REQUIRE(result->kind == rhi::TextureKind::Cube);
    REQUIRE(result->mipLevels == 3);
    REQUIRE(result->mips.size() == 18); // 6 faces * 3 mips

    // Levels are 8x8 (2x2 blocks -> 32 bytes, stride 16), 4x4 (1x1 blocks -> 8 bytes, stride 8),
    // 2x2 (1x1 blocks -> 8 bytes, stride 8).
    constexpr uint32_t kExpectedStride[3] = {16, 8, 8};
    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t level = 0; level < 3; ++level) {
            const rhi::TextureMip& mip = result->mips[face * 3 + level];
            REQUIRE(mip.bytesPerRow == kExpectedStride[level]);
            // Read through TextureMip to pin the face-major, mip-major span used by GPU upload.
            REQUIRE(*static_cast<const uint8_t*>(mip.data) ==
                    static_cast<uint8_t>(face * 16 + level));
        }
    }
    REQUIRE(result->payload.size() == 6 * (32 + 8 + 8));
}

//======================================================================================================================
TEST_CASE("loadDds rejects a bad magic", "[engine]") {
    DdsHeaderFixture fixture;
    fixture.badMagic = true;
    const TempFile file(".dds", fixture.header());
    const AssetResult<DdsImage> result = loadDds(file.string());
    REQUIRE_FALSE(result.has_value());
}

//======================================================================================================================
TEST_CASE("loadDds rejects fourCC DXT5 (unsupported compressed format)", "[engine]") {
    DdsHeaderFixture fixture;
    fixture.fourCC = {'D', 'X', 'T', '5'};
    std::vector<std::byte> bytes = fixture.header();
    const std::vector<std::byte> payload = bc1Payload(4, 4, 1, 1);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const TempFile file(".dds", bytes);
    const AssetResult<DdsImage> result = loadDds(file.string());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == AssetErrorCode::Unsupported);
}

//======================================================================================================================
TEST_CASE("loadDds BC1 stride uses the ceiling of width/4 and height/4, not truncation",
          "[engine]") {
    DdsHeaderFixture fixture;
    fixture.width = 6;
    fixture.height = 5;
    fixture.mipMapCount = 1;
    std::vector<std::byte> bytes = fixture.header();
    const std::vector<std::byte> payload = bc1Payload(6, 5, 1, 1);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const TempFile file(".dds", bytes);

    const AssetResult<DdsImage> result = loadDds(file.string());
    REQUIRE(result.has_value());
    REQUIRE(result->mips[0].bytesPerRow == 16); // ((6+3)/4)*8 = 2*8
    REQUIRE(result->payload.size() == 32);      // ((6+3)/4)*((5+3)/4)*8 = 2*2*8
}
