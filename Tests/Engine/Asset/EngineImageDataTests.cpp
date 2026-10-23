#include <catch2/catch_test_macros.hpp>

#include "Engine/Asset/Image/ImageData.h"
#include "Engine/Asset/Texture/TextureBake.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

using namespace lmx::asset;

//======================================================================================================================
TEST_CASE("a copied ImageData keeps valid mip ranges", "[asset]") {
    const std::array<uint8_t, 4 * 4 * 4> pixels{};
    auto original = std::make_unique<ImageData>(bakeMips(pixels, 4, 4, BakeMode::Linear));
    const ImageData copy = *original;
    original.reset();

    REQUIRE(copy.mips.size() == copy.mipLevels);
    uint64_t end = 0;
    for (const ImageMip& mip : copy.mips) {
        REQUIRE(mip.offset == end);
        end = mip.offset + mip.size;
        REQUIRE(end <= copy.payload.size());
    }
    REQUIRE(end == copy.payload.size());
}
