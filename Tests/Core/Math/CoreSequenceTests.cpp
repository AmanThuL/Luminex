#include "Core/Math/Sequence.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

//======================================================================================================================
TEST_CASE("radicalInverse matches the base-2 and base-3 digit expansion for indices 1 to 16",
          "[core]") {
    constexpr std::array<float, 16> base2 = {
        0x1p-1f,   0x1p-2f,   0x1.8p-1f, 0x1p-3f,   0x1.4p-1f, 0x1.8p-2f, 0x1.cp-1f, 0x1p-4f,
        0x1.2p-1f, 0x1.4p-2f, 0x1.ap-1f, 0x1.8p-3f, 0x1.6p-1f, 0x1.cp-2f, 0x1.ep-1f, 0x1p-5f,
    };
    constexpr std::array<float, 16> base3 = {
        0x1.555556p-2f, 0x1.555556p-1f, 0x1.c71c74p-4f, 0x1.c71c74p-2f,
        0x1.8e38e4p-1f, 0x1.c71c74p-3f, 0x1.1c71c8p-1f, 0x1.c71c74p-1f,
        0x1.2f684ep-5f, 0x1.7b426p-2f,  0x1.684bdap-1f, 0x1.2f684ep-3f,
        0x1.ed097ep-2f, 0x1.a12f68p-1f, 0x1.097b44p-2f, 0x1.2f684cp-1f,
    };
    for (uint32_t index = 1; index <= 16; ++index) {
        REQUIRE(lmx::radicalInverse(index, 2) == base2[index - 1]);
        REQUIRE(lmx::radicalInverse(index, 3) == base3[index - 1]);
    }
}

//======================================================================================================================
TEST_CASE("radicalInverseBase2 matches the digit expansion for indices 0 to 16", "[core]") {
    constexpr std::array<float, 17> expected = {
        0x0p+0f,   0x1p-1f,   0x1p-2f,   0x1.8p-1f, 0x1p-3f,   0x1.4p-1f,
        0x1.8p-2f, 0x1.cp-1f, 0x1p-4f,   0x1.2p-1f, 0x1.4p-2f, 0x1.ap-1f,
        0x1.8p-3f, 0x1.6p-1f, 0x1.cp-2f, 0x1.ep-1f, 0x1p-5f,
    };
    for (uint32_t index = 0; index <= 16; ++index) {
        REQUIRE(lmx::radicalInverseBase2(index) == expected[index]);
    }
}

//======================================================================================================================
TEST_CASE("radicalInverse base 2 and radicalInverseBase2 agree for indices 0 to 65,535", "[core]") {
    for (uint32_t index = 0; index <= 65535; ++index) {
        REQUIRE(lmx::radicalInverse(index, 2) == lmx::radicalInverseBase2(index));
    }
}

//======================================================================================================================
TEST_CASE("hammersley(3, 8) pairs the uniform coordinate with the base-2 radical inverse",
          "[core]") {
    const glm::vec2 point = lmx::hammersley(3, 8);
    REQUIRE(point.x == 0x1.8p-2f);
    REQUIRE(point.y == 0x1.8p-1f);
}
