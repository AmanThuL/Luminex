//----------------------------------------------------------------------------------------------------------------------
/// @file SceneLightLabTests.cpp
/// @brief Tests LightLab's deterministic light/track generation, density scaling and catalog load.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/SceneSession.h"
#include "Scene/LightLab.h"
#include "Scene/SceneLibrary.h"

#include "Engine/Types/LocalLightMath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

using namespace lmx;
using Catch::Approx;

//======================================================================================================================
TEST_CASE("LightLab emitter orbits keep clearance from every material-field solid",
          "[scene][light-lab]") {
    for (uint32_t count : {64u, 66u, 187u, 256u, 1024u, 1090u, 4096u}) {
        const auto lights = scene::lightLabLights(count, 0);
        const auto tracks = scene::lightLabTracks(count, 0);
        for (size_t i = 0; i < lights.size(); ++i) {
            const float radius = i % 4 == 2 ? tracks[i / 4].radius : 0.0f;
            const glm::vec3 centre = lights[i].position;
            float clearance = 1000.0f;
            for (uint32_t column = 0; column < 8; ++column) {
                const float x = (float(column) - 3.5f) * 2.5f;
                // Distance from the whole horizontal orbit's conservative bounding box to the
                // pillar/sphere AABBs. This covers every continuous phase, not just frame samples.
                const glm::vec3 orbitExtent(radius, 0.0f, radius);
                const glm::vec3 pillarGap = glm::max(glm::abs(centre - glm::vec3(x, 1.6f, 3.5f)) -
                                                         orbitExtent - glm::vec3(0.6f, 1.6f, 0.6f),
                                                     glm::vec3(0.0f));
                const glm::vec3 sphereGap = glm::max(glm::abs(centre - glm::vec3(x, 0.9f, -3.5f)) -
                                                         orbitExtent - glm::vec3(0.9f),
                                                     glm::vec3(0.0f));
                clearance = std::min({clearance, glm::length(pillarGap), glm::length(sphereGap)});
            }
            CAPTURE(count, i, clearance);
            REQUIRE(clearance >= 0.25f - 1e-5f);
        }
    }
}

//======================================================================================================================
TEST_CASE("lightLabLights and lightLabTracks are identical across two builds",
          "[scene][light-lab]") {
    const auto firstLights = scene::lightLabLights(256, 0);
    const auto secondLights = scene::lightLabLights(256, 0);
    REQUIRE(firstLights.size() == secondLights.size());
    for (size_t i = 0; i < firstLights.size(); ++i) {
        const render::LocalLight& a = firstLights[i];
        const render::LocalLight& b = secondLights[i];
        REQUIRE(a.type == b.type);
        REQUIRE(a.position == b.position);
        REQUIRE(a.colour == b.colour);
        REQUIRE(a.intensity == b.intensity);
        REQUIRE(a.range == b.range);
        REQUIRE(a.direction == b.direction);
        REQUIRE(a.innerCone == b.innerCone);
        REQUIRE(a.outerCone == b.outerCone);
        const auto firstRow = render::makeLightRow(a);
        const auto secondRow = render::makeLightRow(b);
        REQUIRE(firstRow);
        REQUIRE(secondRow);
        REQUIRE(std::memcmp(&*firstRow, &*secondRow, sizeof(render::LightRow)) == 0);
    }

    // LightOrbitTrack has no padding between its 4-byte-aligned members, so a direct byte
    // comparison is safe and exercises the whole struct, including fields not listed above.
    const auto firstTracks = scene::lightLabTracks(256, 0);
    const auto secondTracks = scene::lightLabTracks(256, 0);
    REQUIRE(firstTracks.size() == secondTracks.size());
    REQUIRE(std::memcmp(firstTracks.data(), secondTracks.data(),
                        firstTracks.size() * sizeof(asset::LightOrbitTrack)) == 0);
}

//======================================================================================================================
TEST_CASE("lightLabLights returns exactly n plus pile lights with one in four spot",
          "[scene][light-lab]") {
    for (const uint32_t n : {64u, 256u, 1000u, 4096u}) {
        const auto lights = scene::lightLabLights(n, 0);
        REQUIRE(lights.size() == n);
        uint32_t spots = 0;
        for (const render::LocalLight& light : lights) {
            spots += light.type == render::LocalLightType::Spot;
        }
        REQUIRE(spots == n / 4);
    }

    const auto withPile = scene::lightLabLights(64, 10);
    REQUIRE(withPile.size() == 74);
}

//======================================================================================================================
TEST_CASE("lightLabTracks orbits exactly one in four grid lights, each pointing at a live light",
          "[scene][light-lab]") {
    for (const uint32_t n : {64u, 256u, 1000u, 4096u}) {
        const auto lights = scene::lightLabLights(n, 0);
        const auto tracks = scene::lightLabTracks(n, 0);
        REQUIRE(tracks.size() == n / 4);
        for (const asset::LightOrbitTrack& track : tracks) {
            REQUIRE(track.light < lights.size());
            REQUIRE(track.light % 4 == 2);
        }
    }
}

//======================================================================================================================
TEST_CASE("lightLabTracks periods divide the 12 s camera rail loop", "[scene][light-lab]") {
    const auto tracks = scene::lightLabTracks(256, 0);
    REQUIRE_FALSE(tracks.empty());
    for (const asset::LightOrbitTrack& track : tracks) {
        REQUIRE(track.period > 0.0f);
        const double periods = scene::kLightLabRailDuration / static_cast<double>(track.period);
        REQUIRE(Approx(periods - std::round(periods)).margin(1e-6) == 0.0);
    }
}

//======================================================================================================================
TEST_CASE("LightLab rail loops from an overview through a close pass", "[scene][light-lab]") {
    const auto rail = scene::lightLabCameraTrack();
    REQUIRE(rail.size() ==
            static_cast<size_t>(scene::kLightLabRailDuration * asset::kAnimationBakeRate) + 1);
    REQUIRE(rail.front().time == 0.0);
    REQUIRE(rail.back().time == scene::kLightLabRailDuration);
    REQUIRE(rail.front().position == rail.back().position);
    REQUIRE(rail.front().yaw == rail.back().yaw);
    REQUIRE(rail.front().pitch == rail.back().pitch);
    const auto midpoint = asset::sampleCameraTrack(rail, scene::kLightLabRailDuration * 0.5);
    REQUIRE(glm::distance(midpoint.position, scene::lightLabPilePosition()) <
            glm::distance(rail.front().position, scene::lightLabPilePosition()));
}

//======================================================================================================================
TEST_CASE("lightLabRange scales by 1/sqrt(n) between two populations", "[scene][light-lab]") {
    const float rangeSmall = scene::lightLabRange(64);
    const float rangeLarge = scene::lightLabRange(1024);
    const float ratio = rangeSmall / rangeLarge;
    const float expected = std::sqrt(1024.0f / 64.0f);
    REQUIRE(Approx(ratio).epsilon(1e-6) == expected);
    // The reference population reproduces the named reference range exactly.
    REQUIRE(scene::lightLabRange(scene::kLightLabReferenceLightCount) ==
            Approx(scene::kLightLabReferenceRange));
}

//======================================================================================================================
TEST_CASE("LightLab point and spot lights reach the floor at every benchmark population",
          "[scene][light-lab]") {
    for (const uint32_t n : {64u, 256u, 1024u, 4096u}) {
        CAPTURE(n);
        const auto lights = scene::lightLabLights(n, 0);
        for (const auto& light : lights) {
            glm::vec3 floorPoint(light.position.x, 0.0f, light.position.z);
            if (light.type == render::LocalLightType::Spot) {
                REQUIRE(light.direction.y < 0.0f);
                floorPoint =
                    light.position - light.direction * (light.position.y / light.direction.y);
            }
            REQUIRE(std::abs(floorPoint.x) < scene::kLightLabGridHalfExtent);
            REQUIRE(std::abs(floorPoint.z) < scene::kLightLabGridHalfExtent);
            REQUIRE(glm::distance(floorPoint, light.position) < light.range);
            const auto row = render::makeLightRow(light);
            REQUIRE(row);
            REQUIRE(render::lightReaches(*row, floorPoint));
            const auto radiance = render::computePunctualLight(
                *row, floorPoint, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(0.5f), glm::vec3(0.04f), 0.0f, 0.5f);
            REQUIRE(glm::all(glm::greaterThan(radiance, glm::vec3(0.0f))));
        }
    }
}

//======================================================================================================================
TEST_CASE("every lightLabLights light passes makeLightRow", "[scene][light-lab]") {
    for (const uint32_t n : {1u, 64u, 256u, 4096u}) {
        for (const render::LocalLight& light :
             scene::lightLabLights(n, std::min(32u, render::kMaxLocalLights - n))) {
            const auto row = render::makeLightRow(light);
            REQUIRE(row.has_value());
        }
    }
}

//======================================================================================================================
TEST_CASE("lightLabLights stacks every pile light at one shared position", "[scene][light-lab]") {
    const auto lights = scene::lightLabLights(64, 40);
    REQUIRE(lights.size() == 104);
    const glm::vec3 pile = scene::lightLabPilePosition();
    for (uint32_t i = 64; i < 104; ++i) {
        REQUIRE(lights[i].position == pile);
        REQUIRE(lights[i].type == render::LocalLightType::Point);
    }
}

//======================================================================================================================
TEST_CASE("LightLab loads from the catalog with its requested population",
          "[gpu][scene][light-lab]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    scene::SceneLibrary library(**device, 4096, 0, 128, 16);
    const auto id = scene::parseSceneId("light-lab");
    REQUIRE(id);
    REQUIRE(library.entry(*id).available);
    auto result = library.get(*id);
    REQUIRE(result);
    auto& loaded = **result;
    REQUIRE(loaded.name == "LightLab");
    REQUIRE(loaded.localLights().size() == 144);
    REQUIRE(loaded.lightLabGridCount == 128);
    const auto gridId = loaded.localLights().front();
    const auto originalPileId = loaded.localLights()[128];
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::PreserveLoadedMotion);
    REQUIRE(session.setLightLabPile(0));
    REQUIRE_FALSE(loaded.light(originalPileId));
    REQUIRE(session.setLightLabPile(140));
    REQUIRE(loaded.light(gridId));
    REQUIRE(loaded.localLights().size() == 268);
    const auto runtimePileId = loaded.localLights().back();
    const auto runtimePosition = loaded.light(runtimePileId)->position;
    loaded.animate(3.0);
    REQUIRE(loaded.light(runtimePileId)->position == runtimePosition);
    REQUIRE(loaded.animationLightId(0) == gridId);
    REQUIRE(loaded.animation.duration == scene::kLightLabRailDuration);
    REQUIRE(loaded.animation.loop);
    REQUIRE_FALSE(loaded.animation.cameraTrack.empty());
    (*device)->beginFrame();
    REQUIRE(loaded.prepareFrame((*device)->frameNumber()));
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("loadLightLabScene rejects out-of-range populations", "[gpu][scene][light-lab]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    REQUIRE_FALSE(scene::loadLightLabScene(**device, 0, 0));
    REQUIRE_FALSE(scene::loadLightLabScene(**device, render::kMaxLocalLights + 1, 0));
    REQUIRE_FALSE(scene::loadLightLabScene(**device, render::kMaxLocalLights, 1));
}

//======================================================================================================================
TEST_CASE("LightLab grid brightness stays comparable as ranges shrink", "[scene][light-lab]") {
    const auto reference = scene::lightLabLights(256, 0)[0];
    const auto response = [](const render::LocalLight& light) {
        const auto row = render::makeLightRow(light);
        REQUIRE(row);
        glm::vec3 floorPoint(light.position.x, 0.0f, light.position.z);
        if (light.type == render::LocalLightType::Spot)
            floorPoint = light.position - light.direction * (light.position.y / light.direction.y);
        return render::computePunctualLight(*row, floorPoint, {0, 1, 0}, {0, 1, 0}, glm::vec3(0.5f),
                                            glm::vec3(0.04f), 0, 0.5f);
    };
    const auto expected = response(reference);
    for (uint32_t n : {64u, 1024u, 4096u}) {
        const auto actual = response(scene::lightLabLights(n, 0)[0]);
        CAPTURE(n);
        for (uint32_t channel = 0; channel < 3; ++channel)
            REQUIRE(actual[channel] == Approx(expected[channel]).epsilon(1e-4));
    }
}
