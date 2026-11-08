#include <catch2/catch_test_macros.hpp>

#include "Engine/Gpu/FormatInfo.h"

#include <array>
#include <utility>

//======================================================================================================================
TEST_CASE("formatName keeps the graph dump spellings", "[engine]") {
    using rojoRHI::Format;
    for (const auto [format, name] : std::array{
             std::pair{Format::Undefined, "Unknown"}, std::pair{Format::BGRA8Unorm, "BGRA8Unorm"},
             std::pair{Format::RGBA8Unorm, "RGBA8Unorm"},
             std::pair{Format::RGBA16Float, "RGBA16Float"},
             std::pair{Format::RG16Float, "RG16Float"}, std::pair{Format::R8Unorm, "R8Unorm"},
             std::pair{Format::D32Float, "D32Float"}, std::pair{Format::R16Float, "R16Float"},
             std::pair{Format::R32Float, "R32Float"}}) {
        CHECK(lmx::engine::formatName(format) == name);
    }
}

//======================================================================================================================
TEST_CASE("bytesPerTexel covers every format the renderer creates", "[engine]") {
    using rojoRHI::Format;
    for (const auto [format, bytes] :
         std::array{std::pair{Format::Undefined, 0u}, std::pair{Format::BGRA8Unorm, 4u},
                    std::pair{Format::RGBA8Unorm, 4u}, std::pair{Format::RGBA8Unorm_sRGB, 4u},
                    std::pair{Format::RGBA16Float, 8u}, std::pair{Format::RG16Float, 4u},
                    std::pair{Format::R8Unorm, 1u}, std::pair{Format::R16Float, 2u},
                    std::pair{Format::R32Float, 4u}, std::pair{Format::D32Float, 4u},
                    std::pair{Format::D32FloatS8, 8u}, std::pair{Format::RGB10A2Unorm, 4u}}) {
        CHECK(lmx::engine::bytesPerTexel(format) == bytes);
        CHECK(lmx::engine::bytesPerBlock(format) == bytes);
        CHECK(lmx::engine::blockExtent(format) == 1);
    }
    for (const auto format : {Format::BC1Unorm, Format::BC1Unorm_sRGB}) {
        CHECK(lmx::engine::bytesPerBlock(format) == 8);
        CHECK(lmx::engine::blockExtent(format) == 4);
    }
    CHECK(lmx::engine::isDepthFormat(Format::D32Float));
    CHECK(lmx::engine::isDepthFormat(Format::D32FloatS8));
    CHECK_FALSE(lmx::engine::isDepthFormat(Format::R32Float));
}
