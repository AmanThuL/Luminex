//----------------------------------------------------------------------------------------------------------------------
/// @file SceneSponzaCameraTourTests.cpp
/// @brief Tests the Sponza tour's corridor coverage, pacing and seamless playback.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Catalog/SponzaCameraTour.h"

#include "Engine/Scene/Scene.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

using namespace lmx;

//======================================================================================================================
TEST_CASE("Sponza tour authors one closed rail and preserves the camera lens",
          "[scene][sponza-tour]") {
    engine::Scene scene;
    scene.initialCamera.fovY = 0.9f;
    scene.initialCamera.nearZ = 0.07f;
    scene.initialCamera.farZ = 400.0f;
    engine::authorSponzaCameraTour(scene);
    const auto& keys = scene.animation.cameraTrack;
    REQUIRE(scene.animation.loop);
    REQUIRE(scene.animation.duration == engine::kSponzaCameraTourDuration);
    REQUIRE(keys.size() == 7201);
    REQUIRE(keys.front().time == 0.0);
    REQUIRE(keys.back().time == engine::kSponzaCameraTourDuration);
    REQUIRE(keys.front().position == keys.back().position);
    REQUIRE(keys.front().yaw == keys.back().yaw);
    REQUIRE(keys.front().pitch == keys.back().pitch);
    REQUIRE(scene.initialCamera.position == keys.front().position);
    REQUIRE(scene.initialCamera.yaw == keys.front().yaw);
    REQUIRE(scene.initialCamera.pitch == keys.front().pitch);
    REQUIRE(scene.initialCamera.fovY == 0.9f);
    REQUIRE(scene.initialCamera.nearZ == 0.07f);
    REQUIRE(scene.initialCamera.farZ == 400.0f);

    engine::Camera camera;
    scene.advanceAnimation(3.0 * engine::kSponzaCameraTourDuration + 0.25);
    scene.followCameraTrack(camera);
    const auto expected = asset::sampleCameraTrack(keys, 0.25);
    REQUIRE(glm::distance(camera.position, expected.position) < 1e-5f);
    REQUIRE(camera.yaw == Catch::Approx(expected.yaw).margin(1e-5f));
}

//======================================================================================================================
TEST_CASE("Sponza tour covers every corridor on both floors and changes level only in the atrium",
          "[scene][sponza-tour]") {
    engine::Scene scene;
    engine::authorSponzaCameraTour(scene);
    std::array<std::array<bool, 4>, 2> visited{};
    for (const auto& key : scene.animation.cameraTrack) {
        const auto p = key.position;
        REQUIRE(p.x >= -12.01f);
        REQUIRE(p.x <= 11.01f);
        REQUIRE(p.z >= -4.81f);
        REQUIRE(p.z <= 3.91f);
        REQUIRE(p.y >= 1.59f);
        REQUIRE(p.y <= 5.81f);
        if (p.y > 1.61f && p.y < 5.79f) {
            REQUIRE(std::abs(p.x) < 8.6f);
            REQUIRE(p.z == Catch::Approx(0.6f).margin(1e-4f));
        }
        for (size_t floor = 0; floor < 2; ++floor) {
            if (std::abs(p.y - (floor == 0 ? 1.6f : 5.8f)) > 0.01f)
                continue;
            visited[floor][0] = visited[floor][0] || (p.x < -11.8f && std::abs(p.z) < 1);
            visited[floor][1] = visited[floor][1] || (p.x > 10.8f && std::abs(p.z) < 1);
            visited[floor][2] = visited[floor][2] || (p.z < -4.6f && std::abs(p.x) < 1);
            visited[floor][3] = visited[floor][3] || (p.z > 3.7f && std::abs(p.x) < 1);
        }
    }
    for (const auto& floor : visited)
        for (bool corridor : floor)
            REQUIRE(corridor);
}

//======================================================================================================================
TEST_CASE("Sponza tour maintains walking pace and continuous pose through corners and the loop",
          "[scene][sponza-tour]") {
    engine::Scene scene;
    engine::authorSponzaCameraTour(scene);
    const auto& keys = scene.animation.cameraTrack;
    for (size_t i = 1; i < keys.size(); ++i) {
        CAPTURE(i);
        const float speed = glm::distance(keys[i].position, keys[i - 1].position) * 60.0f;
        REQUIRE(speed > 1.3f);
        REQUIRE(speed < 1.5f);
        REQUIRE(std::abs(keys[i].yaw - keys[i - 1].yaw) < 0.025f);
        REQUIRE(std::abs(keys[i].pitch - keys[i - 1].pitch) < 0.01f);
        const size_t next = i + 1 < keys.size() ? i + 1 : 1;
        const auto before = keys[i].position - keys[i - 1].position;
        const auto after = keys[next].position - keys[i].position;
        REQUIRE(glm::distance(before, after) * 3600.0f < 3.0f);
    }
}
