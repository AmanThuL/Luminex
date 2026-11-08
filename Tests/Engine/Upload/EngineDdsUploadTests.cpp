#include <catch2/catch_test_macros.hpp>

#include "Engine/Asset/Image/ImageData.h"
#include "Engine/Upload/DdsUpload.h"

#include <cstddef>
#include <cstdint>

using namespace lmx;

//======================================================================================================================
TEST_CASE("toTextureData points each level at its payload offset with its row pitch", "[engine]") {
    asset::ImageData image;
    image.width = 4;
    image.height = 4;
    image.mipLevels = 2;
    image.payload.resize(64 + 16);
    image.mips = {{.offset = 0, .size = 64, .rowPitch = 16},
                  {.offset = 64, .size = 16, .rowPitch = 8}};

    const auto mips = engine::toTextureData(image);

    REQUIRE(mips.size() == 2);
    REQUIRE(mips[0].bytes.data() == image.payload.data());
    REQUIRE(mips[0].bytes.size() == 64);
    REQUIRE(mips[0].bytesPerRow == 16);
    REQUIRE(mips[0].bytesPerImage == 0);
    REQUIRE(mips[1].bytes.data() == image.payload.data() + 64);
    REQUIRE(mips[1].bytes.size() == 16);
    REQUIRE(mips[1].bytesPerRow == 8);
    REQUIRE(mips[1].bytesPerImage == 0);
}
