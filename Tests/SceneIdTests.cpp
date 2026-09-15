#include "Scene/Scene.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <type_traits>

using namespace lmx::scene;

static_assert(!std::is_convertible_v<InstanceId, MeshId>);
static_assert(!std::is_convertible_v<MeshId, MaterialId>);
static_assert(!std::is_convertible_v<MaterialId, TextureId>);

//======================================================================================================================
TEST_CASE("Scene handles reject foreign stores and stale instance generations",
          "[scene][scene-tables]") {
    Scene first;
    Scene second;
    const auto mesh = first.addMesh(lmx::render::makeCube(), "lmx.test.identity.mesh");
    const auto material = first.addMaterial({});
    const auto a = first.addObject({.mesh = mesh, .material = material});
    const auto b =
        first.addObject({.position = {2.0f, 0.0f, 0.0f}, .mesh = mesh, .material = material});
    REQUIRE(a != b);
    REQUIRE(first.tryObject(a));
    REQUIRE(first.tryMesh(mesh));
    REQUIRE(first.tryMaterial(material));
    REQUIRE_FALSE(second.tryObject(a));
    REQUIRE_FALSE(second.tryMesh(mesh));
    REQUIRE_FALSE(second.tryMaterial(material));
    REQUIRE_FALSE(first.tryObject({}));
    REQUIRE_FALSE(first.tryMesh({}));
    REQUIRE_FALSE(first.tryMaterial({}));
    REQUIRE_FALSE(first.tryTexture({}));
    first.removeObject(a);
    REQUIRE_FALSE(first.tryObject(a));
    REQUIRE(first.objects.front().id == b);
    REQUIRE(first.tryObject(b)->position.x == 2.0f);
    const auto replacement =
        first.addObject({.position = {5.0f, 0.0f, 0.0f}, .mesh = mesh, .material = material});
    REQUIRE(replacement.slot == a.slot);
    REQUIRE(replacement.generation != a.generation);
    REQUIRE_FALSE(first.tryObject(a));
    REQUIRE(first.tryObject(replacement)->previousModel[3].x == 5.0f);
}

//======================================================================================================================
TEST_CASE("Scene identity exhaustion retires a slot instead of resurrecting stale handles",
          "[scene][scene-tables]") {
    Scene scene;
    const auto mesh = scene.addMesh(lmx::render::makeCube(), "lmx.test.identity.exhaustion");
    const auto material = scene.addMaterial({});
    const auto oldest = scene.addObject({.mesh = mesh, .material = material});
    auto current = oldest;
    for (uint32_t generation = 1; generation < std::numeric_limits<uint16_t>::max(); ++generation) {
        REQUIRE(current.generation == generation);
        scene.removeObject(current);
        current = scene.addObject({.mesh = mesh, .material = material});
        REQUIRE_FALSE(scene.tryObject(oldest));
        if (current.slot != oldest.slot) {
            REQUIRE(generation == std::numeric_limits<uint16_t>::max() - 1);
            break;
        }
    }
    REQUIRE(current.slot != oldest.slot);
    REQUIRE(current.generation == 1);
}

//======================================================================================================================
TEST_CASE("Removing an instance preserves animation targets after dense editor indices shift",
          "[scene][scene-tables]") {
    Scene scene;
    const auto mesh = scene.addMesh(lmx::render::makeCube(), "lmx.test.identity.tracks");
    const auto material = scene.addMaterial({});
    const auto first = scene.addObject({.mesh = mesh, .material = material});
    const auto second = scene.addObject({.mesh = mesh, .material = material});
    scene.animation.tracks = {{.objectIndex = 0}, {.objectIndex = 1}};
    scene.animation.emissiveTracks = {{.objectIndex = 0}, {.objectIndex = 1}};
    scene.removeObject(first);
    REQUIRE(scene.objects.front().id == second);
    REQUIRE(scene.animation.tracks.size() == 1);
    REQUIRE(scene.animation.tracks.front().objectIndex == 0);
    REQUIRE(scene.animation.emissiveTracks.size() == 1);
    REQUIRE(scene.animation.emissiveTracks.front().objectIndex == 0);
}
