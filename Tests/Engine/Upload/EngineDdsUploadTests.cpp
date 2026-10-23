#include <catch2/catch_test_macros.hpp>

#include "Engine/Asset/Image/ImageData.h"
#include "Engine/Upload/DdsUpload.h"

#include <cstddef>
#include <cstdint>

using namespace lmx;

//======================================================================================================================
TEST_CASE("toTextureMips points each level at its payload offset with its row pitch", "[engine]") {
    asset::ImageData image;
    image.width = 4;
    image.height = 4;
    image.mipLevels = 2;
    image.payload.resize(64 + 16);
    image.mips = {{.offset = 0, .size = 64, .rowPitch = 16},
                  {.offset = 64, .size = 16, .rowPitch = 8}};

    const auto mips = engine::toTextureMips(image);

    REQUIRE(mips.size() == 2);
    REQUIRE(mips[0].data == image.payload.data());
    REQUIRE(mips[0].bytesPerRow == 16);
    REQUIRE(mips[1].data == image.payload.data() + 64);
    REQUIRE(mips[1].bytesPerRow == 8);
}
