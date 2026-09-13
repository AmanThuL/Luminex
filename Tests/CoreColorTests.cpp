#include "Core/Color.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using lmx::srgbToLinear;

//======================================================================================================================
TEST_CASE("srgbToLinear decodes the exact IEC sRGB curve", "[core]") {
    REQUIRE(srgbToLinear(0.0f) == 0.0f);
    REQUIRE(srgbToLinear(1.0f) == Catch::Approx(1.0f).margin(1e-4));
    REQUIRE(srgbToLinear(0.7f) == Catch::Approx(0.4479f).margin(1e-3));
    REQUIRE(srgbToLinear(0.5f) == Catch::Approx(0.2140f).margin(1e-3));
}

//======================================================================================================================
TEST_CASE("srgbToLinear's vec3/vec4 overloads decode component-wise; vec4 keeps alpha", "[core]") {
    const glm::vec3 v3 = srgbToLinear(glm::vec3(0.7f, 0.5f, 1.0f));
    REQUIRE(v3.x == Catch::Approx(0.4479f).margin(1e-3));
    REQUIRE(v3.y == Catch::Approx(0.2140f).margin(1e-3));
    REQUIRE(v3.z == Catch::Approx(1.0f).margin(1e-4));

    const glm::vec4 v4 = srgbToLinear(glm::vec4(0.7f, 0.5f, 1.0f, 0.25f));
    REQUIRE(v4.x == Catch::Approx(0.4479f).margin(1e-3));
    REQUIRE(v4.y == Catch::Approx(0.2140f).margin(1e-3));
    REQUIRE(v4.z == Catch::Approx(1.0f).margin(1e-4));
    REQUIRE(v4.w == 0.25f); // alpha untouched
}
