#include "Core/Math/Aabb.h"

#include <catch2/catch_test_macros.hpp>

using lmx::Aabb;
using lmx::center;
using lmx::distanceSquared;
using lmx::emptyAabb;
using lmx::expand;

//======================================================================================================================
TEST_CASE("emptyAabb then expand over five points matches the min/max accumulate loop", "[core]") {
    Aabb bounds = emptyAabb();
    expand(bounds, glm::vec3(1.0f, 2.0f, 3.0f));
    expand(bounds, glm::vec3(-2.5f, 0.5f, 4.0f));
    expand(bounds, glm::vec3(3.25f, -1.75f, -0.5f));
    expand(bounds, glm::vec3(0.0f, 5.0f, 2.0f));
    expand(bounds, glm::vec3(-4.0f, 3.0f, -6.5f));

    REQUIRE(bounds.minimum.x == -0x1p+2f);
    REQUIRE(bounds.minimum.y == -0x1.cp+0f);
    REQUIRE(bounds.minimum.z == -0x1.ap+2f);
    REQUIRE(bounds.maximum.x == 0x1.ap+1f);
    REQUIRE(bounds.maximum.y == 0x1.4p+2f);
    REQUIRE(bounds.maximum.z == 0x1p+2f);
}

//======================================================================================================================
TEST_CASE("center returns the midpoint of minimum and maximum", "[core]") {
    Aabb bounds = emptyAabb();
    expand(bounds, glm::vec3(1.0f, 2.0f, 3.0f));
    expand(bounds, glm::vec3(-2.5f, 0.5f, 4.0f));
    expand(bounds, glm::vec3(3.25f, -1.75f, -0.5f));
    expand(bounds, glm::vec3(0.0f, 5.0f, 2.0f));
    expand(bounds, glm::vec3(-4.0f, 3.0f, -6.5f));

    const glm::vec3 c = center(bounds);
    REQUIRE(c.x == -0x1.8p-2f);
    REQUIRE(c.y == 0x1.ap+0f);
    REQUIRE(c.z == -0x1.4p+0f);
}

//======================================================================================================================
TEST_CASE("distanceSquared matches boxDistanceSquared for inside/face/corner points", "[core]") {
    Aabb bounds = emptyAabb();
    expand(bounds, glm::vec3(1.0f, 2.0f, 3.0f));
    expand(bounds, glm::vec3(-2.5f, 0.5f, 4.0f));
    expand(bounds, glm::vec3(3.25f, -1.75f, -0.5f));
    expand(bounds, glm::vec3(0.0f, 5.0f, 2.0f));
    expand(bounds, glm::vec3(-4.0f, 3.0f, -6.5f));

    REQUIRE(distanceSquared(bounds, glm::vec3(0.0f, 0.0f, 0.0f)) == 0x0p+0f);
    REQUIRE(distanceSquared(bounds, glm::vec3(5.0f, 0.0f, 0.0f)) == 0x1.88p+1f);
    REQUIRE(distanceSquared(bounds, glm::vec3(10.0f, -10.0f, 20.0f)) == 0x1.71ap+8f);
}
