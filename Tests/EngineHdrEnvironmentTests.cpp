#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Engine/HdrEnvironment.h"

#include <glm/gtc/constants.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

using Catch::Approx;
using namespace lmx::engine;

namespace {

class TempHdr {
public:
    //==================================================================================================================
    explicit TempHdr(const std::string& bytes) {
        static std::atomic<uint32_t> counter{0};
        m_path = std::filesystem::temp_directory_path() /
                 ("lmx-hdr-test-" + std::to_string(counter++) + ".hdr");
        std::ofstream output(m_path, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    //==================================================================================================================
    ~TempHdr() { std::filesystem::remove(m_path); }

    //==================================================================================================================
    TempHdr(const TempHdr&) = delete;
    //==================================================================================================================
    TempHdr& operator=(const TempHdr&) = delete;

    //==================================================================================================================
    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

} // namespace

//======================================================================================================================
TEST_CASE("Radiance HDR decoder preserves scene-linear RGB", "[engine][hdr]") {
    std::string bytes = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
    bytes.append({static_cast<char>(128), static_cast<char>(64), static_cast<char>(32),
                  static_cast<char>(129), static_cast<char>(64), static_cast<char>(128),
                  static_cast<char>(32), static_cast<char>(129)});
    const TempHdr file(bytes);

    const AssetResult<HdrEquirectangularImage> loaded = loadRadianceHdr(file.path().string());

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->width == 2);
    REQUIRE(loaded->height == 1);
    REQUIRE(loaded->radiance.size() == 2);
    CHECK(loaded->radiance[0].x == Approx(1.0f));
    CHECK(loaded->radiance[0].y == Approx(0.5f));
    CHECK(loaded->radiance[0].z == Approx(0.25f));
    CHECK(loaded->radiance[1].x == Approx(0.5f));
    CHECK(loaded->radiance[1].y == Approx(1.0f));
    CHECK(loaded->radiance[1].z == Approx(0.25f));
}

//======================================================================================================================
TEST_CASE("HDR conversion preserves a scaled constant environment", "[engine][hdr]") {
    HdrEquirectangularImage image{
        .width = 4, .height = 2, .radiance = std::vector<glm::vec3>(8, {2.0f, 3.0f, 4.0f})};

    const AssetResult<ibl::CpuCubemap> converted = equirectangularToCubemap(image, 3, 0.37f, 0.25f);

    REQUIRE(converted.has_value());
    for (const auto& face : converted->faces) {
        for (const glm::vec4& texel : face) {
            CHECK(texel.x == Approx(0.5f));
            CHECK(texel.y == Approx(0.75f));
            CHECK(texel.z == Approx(1.0f));
            CHECK(texel.w == Approx(1.0f));
        }
    }
}

//======================================================================================================================
TEST_CASE("HDR conversion has pinned seam and yaw orientation", "[engine][hdr]") {
    HdrEquirectangularImage image;
    image.width = 4;
    image.height = 2;
    for (uint32_t y = 0; y < image.height; ++y) {
        image.radiance.emplace_back(0.0f);
        image.radiance.emplace_back(1.0f);
        image.radiance.emplace_back(4.0f);
        image.radiance.emplace_back(9.0f);
    }

    const AssetResult<ibl::CpuCubemap> unrotated = equirectangularToCubemap(image, 1);
    const AssetResult<ibl::CpuCubemap> quarterTurn =
        equirectangularToCubemap(image, 1, glm::half_pi<float>());

    REQUIRE(unrotated.has_value());
    REQUIRE(quarterTurn.has_value());
    CHECK(unrotated->faces[0][0].x == Approx(2.5f)); // +X: center between columns 1 and 2
    CHECK(unrotated->faces[4][0].x == Approx(6.5f)); // +Z: center between columns 2 and 3
    CHECK(unrotated->faces[5][0].x == Approx(0.5f)); // -Z: center between columns 0 and 1
    CHECK(unrotated->faces[1][0].x == Approx(4.5f)); // -X: wrapped seam between columns 3 and 0
    CHECK(quarterTurn->faces[0][0].x == Approx(unrotated->faces[4][0].x));
}

//======================================================================================================================
TEST_CASE("HDR boundaries reject incomplete or invalid radiance", "[engine][hdr]") {
    HdrEquirectangularImage incomplete{.width = 2, .height = 1, .radiance = {{1.0f, 1.0f, 1.0f}}};
    CHECK_FALSE(equirectangularToCubemap(incomplete, 1).has_value());

    HdrEquirectangularImage nonFinite{
        .width = 2,
        .height = 1,
        .radiance = {{std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}}};
    CHECK_FALSE(equirectangularToCubemap(nonFinite, 1).has_value());

    HdrEquirectangularImage valid{
        .width = 2, .height = 1, .radiance = {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}}};
    CHECK_FALSE(equirectangularToCubemap(valid, 0).has_value());
    CHECK_FALSE(equirectangularToCubemap(valid, 1, 0.0f, -1.0f).has_value());
}

//======================================================================================================================
TEST_CASE("Radiance HDR loader distinguishes missing and non-HDR inputs", "[engine][hdr]") {
    const AssetResult<HdrEquirectangularImage> missing =
        loadRadianceHdr("/definitely/missing/luminex-environment.hdr");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == AssetErrorCode::NotFound);

    const TempHdr nonHdr("not an hdr image");
    const AssetResult<HdrEquirectangularImage> unsupported =
        loadRadianceHdr(nonHdr.path().string());
    REQUIRE_FALSE(unsupported.has_value());
    CHECK(unsupported.error().code == AssetErrorCode::Unsupported);
}
