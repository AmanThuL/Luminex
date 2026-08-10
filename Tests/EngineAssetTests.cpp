#include <catch2/catch_test_macros.hpp>

#include "Engine/DdsLoader.h"
#include "Engine/TextureBake.h"
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

namespace {

//======================================================================================================================
// Fills a width x height RGBA8 buffer from a per-block callback -- every test below partitions
// its source image into 2x2 quadrants (or, for the fold-rule tests, uneven row/column groups) and
// stamps one uint8_t[4] colour or a formula per texel.
std::vector<uint8_t> makeQuadrantImage(uint32_t width, uint32_t height,
                                       const std::array<uint8_t, 4>& topLeft,
                                       const std::array<uint8_t, 4>& topRight,
                                       const std::array<uint8_t, 4>& bottomLeft,
                                       const std::array<uint8_t, 4>& bottomRight) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    const uint32_t halfW = width / 2, halfH = height / 2;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const std::array<uint8_t, 4>& c = y < halfH ? (x < halfW ? topLeft : topRight)
                                                        : (x < halfW ? bottomLeft : bottomRight);
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            std::memcpy(&pixels[offset], c.data(), 4);
        }
    }
    return pixels;
}

//======================================================================================================================
std::array<uint8_t, 4> pixelAt(const std::vector<std::byte>& payload, size_t offset) {
    return {static_cast<uint8_t>(payload[offset + 0]), static_cast<uint8_t>(payload[offset + 1]),
            static_cast<uint8_t>(payload[offset + 2]), static_cast<uint8_t>(payload[offset + 3])};
}

} // namespace

//======================================================================================================================
// White and black 2x2 quadrants arranged diagonally: filtering never mixes colours within a
// uniform quadrant, so level 1 (2x2) must equal the four quadrant colours exactly -- no sRGB math
// needed to check that level. Level 2 (1x1) averages white and black through the sRGB curve:
// linear mean (1+0+0+1)/4 = 0.5, and Engine/Color.h's linearToSrgb(0.5) is the same 0.735... value
// already pinned elsewhere in this codebase as byte 188 (e.g. Tests/GpuRendererTests.cpp's "the
// scene pass encodes its linear output to sRGB").
TEST_CASE("bakeMips --srgb filters a 4x4 diagonal image to exact level 1 and level 2 bytes",
          "[engine]") {
    constexpr std::array<uint8_t, 4> kWhite = {255, 255, 255, 255};
    constexpr std::array<uint8_t, 4> kBlack = {0, 0, 0, 255};
    const std::vector<uint8_t> pixels = makeQuadrantImage(4, 4, kWhite, kBlack, kBlack, kWhite);

    const BakedMipChain baked = bakeMips(pixels, 4, 4, BakeMode::Srgb);
    REQUIRE(baked.mipLevels == 3);
    REQUIRE(baked.payload.size() == (4 * 4 + 2 * 2 + 1 * 1) * 4);

    // Level 1, 2x2, offset 64 (level 0 is 4x4x4 = 64 bytes): top-left, top-right, bottom-left,
    // bottom-right in that row-major order.
    REQUIRE(pixelAt(baked.payload, 64) == kWhite);
    REQUIRE(pixelAt(baked.payload, 68) == kBlack);
    REQUIRE(pixelAt(baked.payload, 72) == kBlack);
    REQUIRE(pixelAt(baked.payload, 76) == kWhite);

    // Level 2, 1x1, offset 80 (level 1 is 2x2x4 = 16 bytes after level 1's own 64-byte offset).
    const std::array<uint8_t, 4> level2 = pixelAt(baked.payload, 80);
    REQUIRE(level2[0] == 188);
    REQUIRE(level2[1] == 188);
    REQUIRE(level2[2] == 188);
    REQUIRE(level2[3] == 255); // alpha is raw-averaged, never sRGB-transformed
}

//======================================================================================================================
// Same diagonal layout, --linear mode: no colour-space transform on any channel, so level 2 is a
// flat raw average -- (64+192+0+255)/4 = 127.75, rounds to 128 -- and unlike --srgb this applies
// identically to alpha too (every channel here is set to the same value per quadrant).
TEST_CASE("bakeMips --linear filters a 4x4 diagonal image to exact level 1 and level 2 bytes",
          "[engine]") {
    constexpr std::array<uint8_t, 4> kA = {64, 64, 64, 64};
    constexpr std::array<uint8_t, 4> kB = {192, 192, 192, 192};
    constexpr std::array<uint8_t, 4> kC = {0, 0, 0, 0};
    constexpr std::array<uint8_t, 4> kD = {255, 255, 255, 255};
    const std::vector<uint8_t> pixels = makeQuadrantImage(4, 4, kA, kB, kC, kD);

    const BakedMipChain baked = bakeMips(pixels, 4, 4, BakeMode::Linear);
    REQUIRE(pixelAt(baked.payload, 64) == kA);
    REQUIRE(pixelAt(baked.payload, 68) == kB);
    REQUIRE(pixelAt(baked.payload, 72) == kC);
    REQUIRE(pixelAt(baked.payload, 76) == kD);

    const std::array<uint8_t, 4> level2 = pixelAt(baked.payload, 80);
    REQUIRE(level2 == std::array<uint8_t, 4>{128, 128, 128, 128});
}

//======================================================================================================================
// +X and +Y tangent-space normals (RGB {255,128,128} and {128,255,128}) placed diagonally. A
// uniform quadrant's normal survives filtering exactly (decode, average four identical vectors,
// renormalize, re-encode round-trips for these axis-aligned values), so level 1 equals the source
// quadrants. Level 2 averages +X and +Y: the raw vector sum is (0.5,0.5,~0.0039) -- the residual Z
// comes from byte 128 not perfectly inverting encodeUnitToByte's zero-component target, and stays
// negligible after normalizing -- giving a unit vector at ~45 degrees in the XY plane:
// normalize(0.5,0.5,~0) ~= (0.7071,0.7071,~0), which encodes to (218,218,128).
TEST_CASE("bakeMips --normal-map filters a 4x4 diagonal image to exact level 1 and level 2 bytes",
          "[engine]") {
    constexpr std::array<uint8_t, 4> kPlusX = {255, 128, 128, 255};
    constexpr std::array<uint8_t, 4> kPlusY = {128, 255, 128, 255};
    const std::vector<uint8_t> pixels = makeQuadrantImage(4, 4, kPlusX, kPlusY, kPlusY, kPlusX);

    const BakedMipChain baked = bakeMips(pixels, 4, 4, BakeMode::NormalMap);
    REQUIRE(pixelAt(baked.payload, 64) == kPlusX);
    REQUIRE(pixelAt(baked.payload, 68) == kPlusY);
    REQUIRE(pixelAt(baked.payload, 72) == kPlusY);
    REQUIRE(pixelAt(baked.payload, 76) == kPlusX);

    const std::array<uint8_t, 4> level2 = pixelAt(baked.payload, 80);
    REQUIRE(level2 == std::array<uint8_t, 4>{218, 218, 128, 255});
}

//======================================================================================================================
// A 5-wide row exercises the odd-dimension fold rule from TextureBake.h's contract comment in one
// shot: destination width is floor(5/2) = 2. Texel 0 is the ordinary case, averaging source
// columns {0,1} = (0+100)/2 = 50. Texel 1 is the last destination index with an odd source width,
// so it folds the trailing unpaired column (index 4) in alongside its normal pair {2,3}, averaging
// all three at equal weight: (40+160+250)/3 = 150.
TEST_CASE("bakeMips folds the trailing texel of an odd source width into the last destination "
          "texel at equal weight",
          "[engine]") {
    const std::vector<uint8_t> pixels = {
        0,   0,   0,   255, // column 0
        100, 100, 100, 255, // column 1
        40,  40,  40,  255, // column 2
        160, 160, 160, 255, // column 3
        250, 250, 250, 255, // column 4 -- the trailing unpaired column
    };
    const BakedMipChain baked = bakeMips(pixels, 5, 1, BakeMode::Linear);
    REQUIRE(baked.width == 5);
    REQUIRE(baked.height == 1);
    REQUIRE(baked.mipLevels == 3); // 5->2->1

    // Level 1 is offset 20 (level 0 is 5*1*4 = 20 bytes), 2 texels.
    REQUIRE(pixelAt(baked.payload, 20) == std::array<uint8_t, 4>{50, 50, 50, 255});
    REQUIRE(pixelAt(baked.payload, 24) == std::array<uint8_t, 4>{150, 150, 150, 255});
}

//======================================================================================================================
// Both axes odd at once: a 3x3 image's single level-1 texel averages all 9 source texels at equal
// weight 1/9 (the fold rule applied separably on each axis, compounding at the one corner
// destination texel that is simultaneously the last index on both). Values 0,10,...,80 sum to 360;
// 360/9 = 40 exactly, so the expected byte has no rounding ambiguity to double-check.
TEST_CASE("bakeMips folds both axes together at a corner destination texel when width and height "
          "are both odd",
          "[engine]") {
    std::vector<uint8_t> pixels(3 * 3 * 4);
    for (int i = 0; i < 9; ++i) {
        const auto v = static_cast<uint8_t>(i * 10);
        pixels[i * 4 + 0] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = pixels[i * 4 + 3] = v;
    }
    const BakedMipChain baked = bakeMips(pixels, 3, 3, BakeMode::Linear);
    REQUIRE(baked.mipLevels == 2); // 3->1

    REQUIRE(pixelAt(baked.payload, 36) == std::array<uint8_t, 4>{40, 40, 40, 40});
}

//======================================================================================================================
// Determinism is the deliverable: baking the same input twice, including through the full
// writeDds + writeManifest file path, must produce byte-identical output on every run. A 9x7
// non-power-of-two image exercises the fold rule on both axes across a full chain, not
// just the hand-picked small cases above.
TEST_CASE("baking the same image twice produces byte-identical DDS and manifest output",
          "[engine]") {
    std::vector<uint8_t> pixels(9 * 7 * 4);
    for (size_t i = 0; i < pixels.size(); ++i) {
        // A fixed, non-uniform, non-symmetric pattern -- deterministic, not random.
        pixels[i] = static_cast<uint8_t>((i * 37 + 11) % 256);
    }

    const BakedMipChain first = bakeMips(pixels, 9, 7, BakeMode::Srgb);
    const BakedMipChain second = bakeMips(pixels, 9, 7, BakeMode::Srgb);
    // Compared as hashes, not element-by-element: a full-vector REQUIRE(a == b) would ask Catch2
    // to stringify every byte of a mismatch for the failure message, and a multi-hundred-byte
    // binary buffer containing arbitrary control bytes has, in practice, crashed that
    // stringification rather than reporting cleanly. Two independent bakes producing the same
    // hash is exactly as strong a determinism proof, and it stays diagnosable on failure.
    REQUIRE(sha256Hex(first.payload) == sha256Hex(second.payload));
    REQUIRE(first.mipLevels == second.mipLevels);

    const TempFile ddsA(".dds", std::vector<std::byte>{});
    const TempFile ddsB(".dds", std::vector<std::byte>{});
    REQUIRE(writeDds(ddsA.string(), first).has_value());
    REQUIRE(writeDds(ddsB.string(), second).has_value());

    std::ifstream fa(ddsA.string(), std::ios::binary);
    std::ifstream fb(ddsB.string(), std::ios::binary);
    const std::vector<char> bytesA((std::istreambuf_iterator<char>(fa)),
                                   std::istreambuf_iterator<char>());
    const std::vector<char> bytesB((std::istreambuf_iterator<char>(fb)),
                                   std::istreambuf_iterator<char>());
    REQUIRE(bytesA.size() == bytesB.size());
    REQUIRE(sha256Hex(std::as_bytes(std::span(bytesA))) ==
            sha256Hex(std::as_bytes(std::span(bytesB))));

    const std::string hashA = sha256Hex(std::as_bytes(std::span(pixels)));
    const std::string hashB = sha256Hex(std::as_bytes(std::span(pixels)));
    REQUIRE(hashA == hashB);
    const TempFile manifestA(".dds.json", std::vector<std::byte>{});
    const TempFile manifestB(".dds.json", std::vector<std::byte>{});
    REQUIRE(
        writeManifest(manifestA.string(), "fixture.png", hashA, BakeMode::Srgb, "1").has_value());
    REQUIRE(
        writeManifest(manifestB.string(), "fixture.png", hashB, BakeMode::Srgb, "1").has_value());
    std::ifstream ma(manifestA.string());
    std::ifstream mb(manifestB.string());
    const std::string manifestTextA((std::istreambuf_iterator<char>(ma)),
                                    std::istreambuf_iterator<char>());
    const std::string manifestTextB((std::istreambuf_iterator<char>(mb)),
                                    std::istreambuf_iterator<char>());
    REQUIRE(manifestTextA == manifestTextB);
}

//======================================================================================================================
// writeDds's output must be exactly what loadDds already parses (Engine/DdsLoader.h's contract):
// same dimensions, same mip count, and -- after loadDds's BGRA->RGBA swizzle undoes writeDds's
// RGBA->BGRA swizzle -- the identical payload bytes bakeMips produced.
TEST_CASE("writeDds then loadDds round-trips a baked chain's dimensions and payload", "[engine]") {
    std::vector<uint8_t> pixels(6 * 5 * 4);
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<uint8_t>((i * 53 + 7) % 256);
    }
    const BakedMipChain baked = bakeMips(pixels, 6, 5, BakeMode::Linear);

    const TempFile file(".dds", std::vector<std::byte>{});
    const AssetResult<void> written = writeDds(file.string(), baked);
    REQUIRE(written.has_value());

    const AssetResult<DdsImage> loaded = loadDds(file.string());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->width == baked.width);
    REQUIRE(loaded->height == baked.height);
    REQUIRE(loaded->mipLevels == baked.mipLevels);
    REQUIRE_FALSE(loaded->bc1);
    REQUIRE(loaded->kind == rhi::TextureKind::Tex2D);
    REQUIRE(loaded->payload.size() == baked.payload.size());
    REQUIRE(loaded->payload == baked.payload);
}

//======================================================================================================================
// sha256Hex against FIPS 180-4's published test vectors, so the manifest's source hash is
// verifiably standard SHA-256 -- the same digest Tools/bake_gltf_textures.py's hashlib call
// produces for the same bytes.
TEST_CASE("sha256Hex matches the published SHA-256 test vectors for the empty string and 'abc'",
          "[engine]") {
    REQUIRE(sha256Hex({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::array<char, 3> abc = {'a', 'b', 'c'};
    REQUIRE(sha256Hex(std::as_bytes(std::span(abc))) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

//======================================================================================================================
TEST_CASE("bakeFilterName returns the manifest's literal filter string for each mode", "[engine]") {
    REQUIRE(bakeFilterName(BakeMode::Srgb) == "box-linear");
    REQUIRE(bakeFilterName(BakeMode::Linear) == "box-raw");
    REQUIRE(bakeFilterName(BakeMode::NormalMap) == "box-normal");
}
