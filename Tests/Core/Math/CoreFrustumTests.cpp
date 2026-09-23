//----------------------------------------------------------------------------------------------------------------------
/// @file CoreFrustumTests.cpp
/// @brief Pins frustum extraction and plane rejection to values from visibility classification.
//----------------------------------------------------------------------------------------------------------------------

#include "Core/Math/Aabb.h"
#include "Core/Math/Frustum.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {

// Default camera (origin, yaw 0, pitch 0, 60 degree vertical field of view, near 0.1) at aspect
// 16:9: its reversed infinite projection times its view, column by column.
const glm::mat4 kDefaultViewProjection{
    glm::vec4{0x1.f2d4a6p-1f, 0x0p+0f, 0x0p+0f, 0x0p+0f},
    glm::vec4{0x0p+0f, 0x1.bb67bp+0f, 0x0p+0f, 0x0p+0f},
    glm::vec4{0x0p+0f, 0x0p+0f, 0x0p+0f, -0x1p+0f},
    glm::vec4{0x0p+0f, 0x0p+0f, 0x1.99999ap-4f, 0x0p+0f},
};

// Planes that visibility extraction produced for that matrix, per guard distance.
const std::array<glm::vec4, 5> kPlanesGuard0{{
    {0x1.654aa8p-1f, 0x0p+0f, -0x1.6eb96ap-1f, 0x0p+0f},
    {-0x1.654aa8p-1f, 0x0p+0f, -0x1.6eb96ap-1f, 0x0p+0f},
    {0x0p+0f, 0x1.bb67bp-1f, -0x1p-1f, 0x0p+0f},
    {0x0p+0f, -0x1.bb67bp-1f, -0x1p-1f, 0x0p+0f},
    {0x0p+0f, 0x0p+0f, -0x1p+0f, -0x1.99999ap-4f},
}};
const std::array<glm::vec4, 5> kPlanesGuardCentimetre{{
    {0x1.654aa8p-1f, 0x0p+0f, -0x1.6eb96ap-1f, 0x1.47ae14p-7f},
    {-0x1.654aa8p-1f, 0x0p+0f, -0x1.6eb96ap-1f, 0x1.47ae14p-7f},
    {0x0p+0f, 0x1.bb67bp-1f, -0x1p-1f, 0x1.47ae14p-7f},
    {0x0p+0f, -0x1.bb67bp-1f, -0x1p-1f, 0x1.47ae14p-7f},
    {0x0p+0f, 0x0p+0f, -0x1p+0f, -0x1.70a3d8p-4f},
}};
const std::array<glm::vec4, 5> kPlanesGuardMillimetre{{
    {0x1.654aa8p-1f, 0x0p+0f, -0x1.6eb96ap-1f, 0x1.0624dep-10f},
    {-0x1.654aa8p-1f, 0x0p+0f, -0x1.6eb96ap-1f, 0x1.0624dep-10f},
    {0x0p+0f, 0x1.bb67bp-1f, -0x1p-1f, 0x1.0624dep-10f},
    {0x0p+0f, -0x1.bb67bp-1f, -0x1p-1f, 0x1.0624dep-10f},
    {0x0p+0f, 0x0p+0f, -0x1p+0f, -0x1.958106p-4f},
}};

//======================================================================================================================
void requirePlanes(const lmx::Frustum& frustum, const std::array<glm::vec4, 5>& expected) {
    REQUIRE(frustum.valid);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        INFO("plane " << index);
        REQUIRE(frustum.planes[index].x == expected[index].x);
        REQUIRE(frustum.planes[index].y == expected[index].y);
        REQUIRE(frustum.planes[index].z == expected[index].z);
        REQUIRE(frustum.planes[index].w == expected[index].w);
    }
}

enum Plane : std::size_t { Left, Right, Bottom, Top, Near };

} // namespace

//======================================================================================================================
TEST_CASE("extractFrustum reproduces the unguarded default camera planes", "[core]") {
    requirePlanes(lmx::extractFrustum(kDefaultViewProjection, 0.0f), kPlanesGuard0);
}

//======================================================================================================================
TEST_CASE("extractFrustum adds a centimetre guard to every plane distance", "[core]") {
    requirePlanes(lmx::extractFrustum(kDefaultViewProjection, 0.01f), kPlanesGuardCentimetre);
}

//======================================================================================================================
TEST_CASE("extractFrustum with the visibility guard reproduces visibility extraction", "[core]") {
    requirePlanes(lmx::extractFrustum(kDefaultViewProjection, 1e-3f), kPlanesGuardMillimetre);
}

//======================================================================================================================
TEST_CASE("extractFrustum marks a nonfinite or degenerate matrix invalid", "[core]") {
    glm::mat4 withNan = kDefaultViewProjection;
    withNan[1][2] = std::numeric_limits<float>::quiet_NaN();
    glm::mat4 withInfinity = kDefaultViewProjection;
    withInfinity[3][0] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(lmx::extractFrustum(withNan, 0.0f).valid);
    CHECK_FALSE(lmx::extractFrustum(withInfinity, 0.0f).valid);
    CHECK_FALSE(lmx::extractFrustum(glm::mat4{0.0f}, 0.0f).valid);
}

//======================================================================================================================
TEST_CASE("a default Frustum is invalid with zero planes", "[core]") {
    const lmx::Frustum frustum;
    REQUIRE_FALSE(frustum.valid);
    for (const auto& plane : frustum.planes)
        REQUIRE(plane == glm::vec4{0.0f});
}

//======================================================================================================================
TEST_CASE("planeRejects keeps a box inside every plane", "[core]") {
    const lmx::Aabb inside{{-0.5f, -0.5f, -5.5f}, {0.5f, 0.5f, -4.5f}};
    for (const auto& plane : kPlanesGuardMillimetre)
        REQUIRE_FALSE(lmx::planeRejects(plane, inside));
}

//======================================================================================================================
TEST_CASE("planeRejects keeps a box straddling each plane", "[core]") {
    const auto& planes = kPlanesGuardMillimetre;
    CHECK_FALSE(lmx::planeRejects(planes[Left], {{-6.0f, -0.5f, -5.5f}, {-4.5f, 0.5f, -4.5f}}));
    CHECK_FALSE(lmx::planeRejects(planes[Right], {{4.5f, -0.5f, -5.5f}, {6.0f, 0.5f, -4.5f}}));
    CHECK_FALSE(lmx::planeRejects(planes[Bottom], {{-0.5f, -3.5f, -5.5f}, {0.5f, -2.0f, -4.5f}}));
    CHECK_FALSE(lmx::planeRejects(planes[Top], {{-0.5f, 2.0f, -5.5f}, {0.5f, 3.5f, -4.5f}}));
    CHECK_FALSE(lmx::planeRejects(planes[Near], {{-0.05f, -0.05f, -0.5f}, {0.05f, 0.05f, 0.5f}}));
}

//======================================================================================================================
TEST_CASE("planeRejects rejects a box outside each plane", "[core]") {
    const auto& planes = kPlanesGuardMillimetre;
    CHECK(lmx::planeRejects(planes[Left], {{-20.0f, -0.5f, -5.5f}, {-15.0f, 0.5f, -4.5f}}));
    CHECK(lmx::planeRejects(planes[Right], {{15.0f, -0.5f, -5.5f}, {20.0f, 0.5f, -4.5f}}));
    CHECK(lmx::planeRejects(planes[Bottom], {{-0.5f, -12.0f, -5.5f}, {0.5f, -10.0f, -4.5f}}));
    CHECK(lmx::planeRejects(planes[Top], {{-0.5f, 10.0f, -5.5f}, {0.5f, 12.0f, -4.5f}}));
    CHECK(lmx::planeRejects(planes[Near], {{-0.05f, -0.05f, 1.0f}, {0.05f, 0.05f, 2.0f}}));
}

//======================================================================================================================
TEST_CASE("planeRejects keeps a box touching the plane and rejects the next float beyond",
          "[core]") {
    const auto& near = kPlanesGuard0[Near];
    const float touching = -0x1.99999ap-4f;
    const float beyond = -0x1.999998p-4f;
    REQUIRE(std::nextafter(touching, 0.0f) == beyond);
    CHECK_FALSE(lmx::planeRejects(near, {{-0.05f, -0.05f, touching}, {0.05f, 0.05f, touching}}));
    CHECK(lmx::planeRejects(near, {{-0.05f, -0.05f, beyond}, {0.05f, 0.05f, beyond}}));
}
