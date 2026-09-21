#include <catch2/catch_test_macros.hpp>

#include "Engine/Scene/Scene.h"
#include "Scenes/SceneLibrary.h"
#include <rojoRHI/RHI.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

//======================================================================================================================
TEST_CASE("San Miguel keeps masked content and a continuous comparison rail", "[gpu][san-miguel]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    lmx::scenes::SceneLibrary library(**device);
    const auto id = lmx::scenes::parseSceneId("san-miguel");
    REQUIRE(id.has_value());
    if (!library.entry(*id).available) {
        SKIP("Optional San Miguel absent; xmake setup --san-miguel installs it");
    }
    auto result = library.get(*id);
    INFO((result.has_value() ? "" : result.error().message));
    REQUIRE(result.has_value());
    const auto& scene = **result;
    REQUIRE_FALSE(scene.objects.empty());
    REQUIRE(std::ranges::any_of(scene.objects, [&scene](const auto& object) {
        const auto& material = scene.material(object.material);
        return material.alphaMode == lmx::engine::AlphaMode::Mask && material.doubleSided &&
               material.diffuse.has_value();
    }));
    REQUIRE(std::ranges::any_of(scene.objects, [&scene](const auto& object) {
        return scene.material(object.material).normalMap.has_value();
    }));
    uint64_t triangles = 0;
    std::vector<uint32_t> countedMeshes;
    for (const auto& object : scene.objects) {
        if (std::ranges::find(countedMeshes, object.mesh.slot) == countedMeshes.end()) {
            triangles += scene.tryMesh(object.mesh)->indexCount / 3;
            countedMeshes.push_back(object.mesh.slot);
        }
    }
    REQUIRE(triangles > 100000);
    REQUIRE(scene.animation.tracks.empty());
    REQUIRE(scene.animation.loop);
    REQUIRE(scene.animation.duration == 12.0);
    const auto& rail = scene.animation.cameraTrack;
    REQUIRE(rail.size() == 721);
    REQUIRE(glm::length(rail.front().position - rail.back().position) < 1e-5f);
    REQUIRE(glm::length(rail.front().position - scene.initialCamera.position) < 1e-5f);
    REQUIRE(glm::length(rail[rail.size() / 2].position - rail.front().position) > 1.0f);
    for (size_t i = 1; i < rail.size(); ++i) {
        REQUIRE(rail[i].time > rail[i - 1].time);
        REQUIRE(glm::length(rail[i].position - rail[i - 1].position) < 0.1f);
        REQUIRE(std::abs(rail[i].yaw - rail[i - 1].yaw) < 0.05f);
        REQUIRE(std::abs(rail[i].pitch - rail[i - 1].pitch) < 0.05f);
    }
}
