#include "Core/Math/Projection.h"
#include "Core/Math/Sphere.h"

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <array>

using lmx::OrthoFit;
using lmx::Sphere;

namespace {

/// Column-major, the order glm stores and indexes a mat4 in: element [c * 4 + r] is m[c][r].
using Matrix16 = std::array<float, 16>;

//======================================================================================================================
/// Requires every element of `actual` to equal `expected` exactly.
void requireExactly(const glm::mat4& actual, const Matrix16& expected) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CAPTURE(column, row);
            REQUIRE(actual[column][row] == expected[column * 4 + row]);
        }
    }
}

// Inputs shared by every orthographic case: a sphere off the origin, so the fit's translation is
// exercised as well as its extents.
constexpr Sphere kSphere{{1.0f, 2.0f, 3.0f}, 5.0f};

} // namespace

//======================================================================================================================
TEST_CASE("perspectiveReversedInfinite matches the camera projection it replaces", "[core]") {
    // Captured from Source/Engine/Types/Camera.cpp's Camera::projectionMatrix at 52945a6 via
    // std::format("{:a}", ...). The inputs are glm::radians(60.0f), glm::radians(35.0f) and
    // 16.0f / 9.0f, written as the hexfloats those expressions produce.
    SECTION("60 degrees, 16:9, near 0.1") {
        const glm::mat4 projection =
            lmx::perspectiveReversedInfinite(0x1.0c1524p+0f, 0x1.c71c72p+0f, 0.1f);
        requireExactly(projection, {0x1.f2d4a6p-1f, 0.0f, 0.0f, 0.0f, //
                                    0.0f, 0x1.bb67bp+0f, 0.0f, 0.0f,  //
                                    0.0f, 0.0f, 0.0f, -0x1p+0f,       //
                                    0.0f, 0.0f, 0x1.99999ap-4f, 0.0f});
    }
    SECTION("35 degrees, square, near 0.5") {
        const glm::mat4 projection = lmx::perspectiveReversedInfinite(0x1.38c354p-1f, 1.0f, 0.5f);
        requireExactly(projection, {0x1.95f6dp+1f, 0.0f, 0.0f, 0.0f, //
                                    0.0f, 0x1.95f6dp+1f, 0.0f, 0.0f, //
                                    0.0f, 0.0f, 0.0f, -0x1p+0f,      //
                                    0.0f, 0.0f, 0x1p-1f, 0.0f});
    }
}

//======================================================================================================================
TEST_CASE("perspectiveReversedInfinite maps the near plane to depth 1", "[core]") {
    const float nearZ = 0.5f;
    const glm::mat4 projection = lmx::perspectiveReversedInfinite(0x1.38c354p-1f, 1.0f, nearZ);
    const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, -nearZ, 1.0f);
    REQUIRE(clip.z / clip.w == 1.0f);
}

//======================================================================================================================
TEST_CASE("fitOrthoToSphere matches the shadow fit's view and projection", "[core]") {
    // Captured from Source/Render/ShadowStage.cpp's fitShadowOrtho at 52945a6 (its lightView and
    // lightProj, for a sphere centred at (1, 2, 3) with radius 5) via std::format("{:a}", ...).
    const OrthoFit fit = lmx::fitOrthoToSphere(kSphere, glm::vec3(-1.0f, -2.0f, -1.0f));
    requireExactly(fit.view, {0x1.6a09e6p-1f, -0x1.279a74p-1f, 0x1.a20bd6p-2f, 0.0f,  //
                              0.0f, 0x1.279a74p-1f, 0x1.a20bd6p-1f, 0.0f,             //
                              -0x1.6a09e6p-1f, -0x1.279a74p-1f, 0x1.a20bd6p-2f, 0.0f, //
                              0x1.6a09e4p+0f, 0x1.279a74p+0f, -0x1.a882f4p+3f, 0x1p+0f});
    requireExactly(fit.projection, {0x1.99999ap-3f, 0.0f, 0.0f, 0.0f, //
                                    0.0f, 0x1.99999ap-3f, 0.0f, 0.0f, //
                                    0.0f, 0.0f, 0x1.99999ap-4f, 0.0f, //
                                    -0.0f, -0.0f, 0x1.7ffffep+0f, 0x1p+0f});
}

//======================================================================================================================
TEST_CASE("fitOrthoToSphere falls back to another up vector when the direction is parallel to up",
          "[core]") {
    // Captured as above, for a light pointing straight down.
    const OrthoFit fit = lmx::fitOrthoToSphere(kSphere, glm::vec3(0.0f, -1.0f, 0.0f));
    requireExactly(fit.view, {-0x1p+0f, 0.0f, -0.0f, 0.0f, //
                              0.0f, 0.0f, 0x1p+0f, 0.0f,   //
                              0.0f, 0x1p+0f, -0.0f, 0.0f,  //
                              0x1p+0f, -0x1.8p+1f, -0x1.8p+3f, 0x1p+0f});
    requireExactly(fit.projection, {0x1.99999ap-3f, 0.0f, 0.0f, 0.0f, //
                                    0.0f, 0x1.99999ap-3f, 0.0f, 0.0f, //
                                    0.0f, 0.0f, 0x1.99999ap-4f, 0.0f, //
                                    -0.0f, -0.0f, 0x1.8p+0f, 0x1p+0f});
}
