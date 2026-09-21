#include "Core/Math/Aabb.h"
#include "Core/Math/Sphere.h"

#include <catch2/catch_test_macros.hpp>

using lmx::Aabb;
using lmx::Sphere;

//======================================================================================================================
TEST_CASE("boundingSphere matches the vec4(center, length(max - center)) expression it replaces",
          "[core]") {
    // Captured from Source/Engine/Scene/Scene.cpp at 52945a6 (and the four
    // Source/Engine/Catalog/*Lab.cpp sites, which compute the same expression) via
    // std::format("{:a}", ...) on glm::vec4(center, glm::length(aabbMax - center)).
    {
        const Aabb box{glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(1.0f, 1.0f, 1.0f)};
        const Sphere sphere = lmx::boundingSphere(box);
        REQUIRE(sphere.center.x == 0x0p+0f);
        REQUIRE(sphere.center.y == 0x0p+0f);
        REQUIRE(sphere.center.z == 0x0p+0f);
        REQUIRE(sphere.radius == 0x1.bb67aep+0f);
    }
    {
        const Aabb box{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(2.0f, 4.0f, 6.0f)};
        const Sphere sphere = lmx::boundingSphere(box);
        REQUIRE(sphere.center.x == 0x1p+0f);
        REQUIRE(sphere.center.y == 0x1p+1f);
        REQUIRE(sphere.center.z == 0x1.8p+1f);
        REQUIRE(sphere.radius == 0x1.deeea2p+1f);
    }
    {
        const Aabb box{glm::vec3(-5.0f, 2.0f, -3.0f), glm::vec3(-1.0f, 10.0f, 7.0f)};
        const Sphere sphere = lmx::boundingSphere(box);
        REQUIRE(sphere.center.x == -0x1.8p+1f);
        REQUIRE(sphere.center.y == 0x1.8p+2f);
        REQUIRE(sphere.center.z == 0x1p+1f);
        REQUIRE(sphere.radius == 0x1.ad5336p+2f);
    }
}

//======================================================================================================================
TEST_CASE("toVec4 packs the sphere centre and radius with radius in w", "[core]") {
    const Sphere sphere{glm::vec3(1.0f, -2.0f, 3.0f), 4.5f};
    const glm::vec4 packed = lmx::toVec4(sphere);
    REQUIRE(packed.x == 1.0f);
    REQUIRE(packed.y == -2.0f);
    REQUIRE(packed.z == 3.0f);
    REQUIRE(packed.w == 4.5f);
}

//======================================================================================================================
TEST_CASE("intersects treats a shared boundary point as intersecting, not disjoint", "[core]") {
    const Aabb box{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(2.0f, 2.0f, 2.0f)};

    // Touching: the sphere's surface reaches exactly to the box's nearest face.
    const Sphere touching{glm::vec3(3.0f, 1.0f, 1.0f), 1.0f};
    REQUIRE(lmx::intersects(box, touching));

    // Disjoint: a gap remains between the sphere's surface and the box.
    const Sphere disjoint{glm::vec3(4.0f, 1.0f, 1.0f), 1.0f};
    REQUIRE_FALSE(lmx::intersects(box, disjoint));

    // Containing: the sphere fully encloses the box, and the box fully encloses the sphere.
    const Sphere enclosingBox{glm::vec3(1.0f, 1.0f, 1.0f), 10.0f};
    REQUIRE(lmx::intersects(box, enclosingBox));
    const Sphere insideBox{glm::vec3(1.0f, 1.0f, 1.0f), 0.1f};
    REQUIRE(lmx::intersects(box, insideBox));
}
