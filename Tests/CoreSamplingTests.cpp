#include "Core/Math/Sampling.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/constants.hpp>

using lmx::cubeAreaElement;
using lmx::cubeFaceDirection;
using lmx::cubeTexelSolidAngle;
using lmx::sampleGgxHalfVector;

//======================================================================================================================
TEST_CASE("cubeFaceDirection reprojects (0.25, -0.5) onto each of the six faces", "[core]") {
    REQUIRE(cubeFaceDirection(0, 0.25f, -0.5f) == glm::vec3(0x1p+0f, 0x1p-1f, -0x1p-2f));
    REQUIRE(cubeFaceDirection(1, 0.25f, -0.5f) == glm::vec3(-0x1p+0f, 0x1p-1f, 0x1p-2f));
    REQUIRE(cubeFaceDirection(2, 0.25f, -0.5f) == glm::vec3(0x1p-2f, 0x1p+0f, -0x1p-1f));
    REQUIRE(cubeFaceDirection(3, 0.25f, -0.5f) == glm::vec3(0x1p-2f, -0x1p+0f, 0x1p-1f));
    REQUIRE(cubeFaceDirection(4, 0.25f, -0.5f) == glm::vec3(0x1p-2f, 0x1p-1f, 0x1p+0f));
    REQUIRE(cubeFaceDirection(5, 0.25f, -0.5f) == glm::vec3(-0x1p-2f, 0x1p-1f, -0x1p+0f));
}

//======================================================================================================================
TEST_CASE("cubeTexelSolidAngle sums to 4 pi over a 16-square cube", "[core]") {
    float sum = 0.0f;
    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t x = 0; x < 16; ++x) {
                sum += cubeTexelSolidAngle(x, y, 16);
            }
        }
    }
    REQUIRE(sum == Catch::Approx(4.0f * glm::pi<float>()).margin(1e-4));
}

//======================================================================================================================
TEST_CASE("cubeTexelSolidAngle matches captured texel values", "[core]") {
    REQUIRE(cubeTexelSolidAngle(0, 0, 16) == 0x1.bf8ep-9f);
    REQUIRE(cubeTexelSolidAngle(7, 8, 16) == 0x1.f82498p-7f);
}

//======================================================================================================================
TEST_CASE("sampleGgxHalfVector's local overload matches captured half vectors", "[core]") {
    REQUIRE(sampleGgxHalfVector({0.25f, 0.5f}, 0.04f) ==
            glm::vec3(-0x1.e03bbcp-30f, 0x1.476c06p-5f, 0x1.ff9744p-1f));
    REQUIRE(sampleGgxHalfVector({0.25f, 0.5f}, 0.5f) ==
            glm::vec3(-0x1.4fd674p-26f, 0x1.c9f25ep-2f, 0x1.c9f25cp-1f));
}

//======================================================================================================================
TEST_CASE("sampleGgxHalfVector's normal overload matches captured half vectors", "[core]") {
    REQUIRE(sampleGgxHalfVector({0.25f, 0.5f}, 0.04f, glm::vec3(0.0f, 0.0f, 1.0f)) ==
            glm::vec3(0x1.476c06p-5f, 0x1.e03bbcp-30f, 0x1.ff9744p-1f));
    REQUIRE(sampleGgxHalfVector({0.25f, 0.5f}, 0.5f, glm::vec3(0.0f, 0.0f, 1.0f)) ==
            glm::vec3(0x1.c9f25ep-2f, 0x1.4fd674p-26f, 0x1.c9f25cp-1f));
    REQUIRE(sampleGgxHalfVector({0.25f, 0.5f}, 0.04f, glm::vec3(1.0f, 0.0f, 0.0f)) ==
            glm::vec3(0x1.ff9744p-1f, -0x1.e03bbcp-30f, 0x1.476c06p-5f));
    REQUIRE(sampleGgxHalfVector({0.25f, 0.5f}, 0.5f, glm::vec3(1.0f, 0.0f, 0.0f)) ==
            glm::vec3(0x1.c9f25cp-1f, -0x1.4fd674p-26f, 0x1.c9f25ep-2f));
}
