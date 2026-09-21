#include "App/Model/EditorPlayback.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

namespace {
using lmx::app::EditorPlayback;
using lmx::app::PlaybackState;
using lmx::app::SceneActivationMotion;
using lmx::app::SceneSession;
namespace asset = lmx::asset;
namespace render = lmx::render;
namespace engine = lmx::engine;

//======================================================================================================================
engine::Scene playbackScene() {
    engine::Scene result;
    const auto mesh = result.addMesh(engine::makeCube(), "lmx.test.playback.cube");
    const auto material = result.addMaterial({.emissive = {1, 2, 3}});
    for (uint32_t i = 0; i < 3; ++i)
        result.addObject({.position = {7.0f + i, 2, 3},
                          .eulerDegrees = {10, 20, 30},
                          .scale = {2, 3, 4},
                          .mesh = mesh,
                          .material = material,
                          .emissiveStrength = 2.75f});
    result.animation.duration = 1;
    result.animationTime = 0.25;
    result.animation.tracks.push_back(
        {.objectIndex = 0,
         .keys = {{.time = 0, .translation = {0, 0, 0}}, {.time = 1, .translation = {60, 0, 0}}}});
    result.animation.emissiveTracks.push_back(
        {.objectIndex = 1, .keys = {{.time = 0, .strength = 8}, {.time = 1, .strength = 16}}});
    result.animation.cameraTrack = {{.time = 0, .position = {0, 1, 2}},
                                    {.time = 1, .position = {60, 1, 2}, .yaw = 0.6f}};
    const auto light = result.addLight({.type = engine::LocalLightType::Point,
                                        .position = {0, 0, 0},
                                        .colour = {1, 1, 1},
                                        .intensity = 1,
                                        .range = 5});
    REQUIRE(light.has_value());
    result.animation.lightTracks.push_back(
        {.light = 0, .centre = {0, 0, 0}, .axis = {0, 0, 1}, .radius = 4, .phase = 0, .period = 4});
    result.initialCamera = {.position = {3, 4, 5},
                            .yaw = 0.2f,
                            .pitch = -0.1f,
                            .fovY = 0.8f,
                            .nearZ = 0.25f,
                            .farZ = 500};
    return result;
}

//======================================================================================================================
bool near3(const glm::vec3& a, const glm::vec3& b) {
    constexpr float kMargin = 1e-4f;
    return std::abs(a.x - b.x) <= kMargin && std::abs(a.y - b.y) <= kMargin &&
           std::abs(a.z - b.z) <= kMargin;
}

//======================================================================================================================
void requireCamera(const engine::Camera& actual, const engine::Camera& expected) {
    REQUIRE(actual.position == expected.position);
    REQUIRE(actual.yaw == expected.yaw);
    REQUIRE(actual.pitch == expected.pitch);
    REQUIRE(actual.fovY == expected.fovY);
    REQUIRE(actual.nearZ == expected.nearZ);
    REQUIRE(actual.farZ == expected.farZ);
    REQUIRE(actual.moveSpeed == expected.moveSpeed);
}
} // namespace

//======================================================================================================================
TEST_CASE("editor playback restores its first edit state across pause and resume",
          "[app][editor-playback]") {
    auto scene = playbackScene();
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    session.camera().position = {9, 8, 7};
    session.camera().moveSpeed = 12;
    const auto camera = session.camera();
    const auto pose = scene.objects[0];
    EditorPlayback playback;
    bool followRail = true;
    REQUIRE(playback.state() == PlaybackState::Stopped);
    REQUIRE_FALSE(playback.active());
    REQUIRE_FALSE(playback.pause());
    REQUIRE_FALSE(playback.stop(session, followRail));
    REQUIRE(playback.play(session, followRail));
    REQUIRE(playback.playing());
    REQUIRE(playback.active());
    REQUIRE_FALSE(playback.play(session, followRail));
    REQUIRE(scene.animationTime == 0.25);
    requireCamera(session.camera(), camera);
    REQUIRE(scene.objects[0].position == pose.position);

    session.advanceEditorFrame(playback.playing(), followRail, false);
    REQUIRE(scene.animationTime == Catch::Approx(0.25 + 1.0 / asset::kAnimationBakeRate));
    REQUIRE(scene.objects[0].position.x == Catch::Approx(16));
    REQUIRE(scene.objects[1].emissiveStrength == 8);
    REQUIRE(playback.pause());
    REQUIRE(playback.state() == PlaybackState::Paused);
    REQUIRE_FALSE(playback.pause());
    const auto pausedTime = scene.animationTime;
    session.advanceEditorFrame(playback.playing(), false, false);
    REQUIRE(scene.animationTime == pausedTime);
    followRail = false;
    session.camera().position = {100, 100, 100};
    session.camera().fovY = 1.2f;
    session.camera().nearZ = 0.5f;
    session.camera().farZ = 1000;
    session.camera().moveSpeed = 25;
    REQUIRE(playback.play(session, followRail));
    session.advanceEditorFrame(playback.playing(), followRail, false);
    scene.objects[0].emissiveStrength = 5;
    scene.objects[1].position = {20, 30, 40};
    scene.objects[2].position = {50, 60, 70};
    scene.lights[1].strength = {3, 4, 5};
    auto& material = scene.material(scene.objects[0].material);
    material.emissive = {9, 8, 7};
    material.roughness = 0.75f;
    REQUIRE(playback.stop(session, followRail));
    REQUIRE(playback.state() == PlaybackState::Stopped);
    REQUIRE_FALSE(playback.playing());
    REQUIRE_FALSE(playback.active());
    REQUIRE(followRail);
    REQUIRE(scene.animationTime == 0.25);
    requireCamera(session.camera(), camera);
    REQUIRE(scene.objects[0].position == pose.position);
    REQUIRE(scene.objects[0].eulerDegrees == pose.eulerDegrees);
    REQUIRE(scene.objects[0].scale == pose.scale);
    REQUIRE(scene.objects[0].emissiveStrength == 5);
    REQUIRE(scene.objects[1].emissiveStrength == 2.75f);
    REQUIRE(scene.objects[1].position == glm::vec3(20, 30, 40));
    REQUIRE(scene.objects[2].position == glm::vec3(50, 60, 70));
    REQUIRE(scene.lights[1].strength == glm::vec3(3, 4, 5));
    REQUIRE(material.emissive == glm::vec3(9, 8, 7));
    REQUIRE(material.roughness == 0.75f);
    for (const auto& object : scene.objects)
        REQUIRE(object.previousModel == object.modelMatrix());
    REQUIRE_FALSE(playback.stop(session, followRail));
}

//======================================================================================================================
TEST_CASE("editor playback restores only a tracked light's position on Stop, leaving every other "
          "edit and every untracked light alone",
          "[app][editor-playback]") {
    auto scene = playbackScene();
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    const auto trackedId = scene.localLights().front();
    const auto authoredPosition = scene.light(trackedId)->position;
    const auto untrackedId = scene.addLight({.type = engine::LocalLightType::Point,
                                             .position = {5, 6, 7},
                                             .colour = {0.1f, 0.2f, 0.3f},
                                             .intensity = 2.0f,
                                             .range = 8.0f});
    REQUIRE(untrackedId.has_value());
    EditorPlayback playback;
    bool followRail = false;

    REQUIRE(playback.play(session, followRail));
    // Playing does not itself move the light -- only sampled frames do.
    REQUIRE(near3(scene.light(trackedId)->position, authoredPosition));

    session.advanceEditorFrame(true, followRail, false);
    // A quarter turn (track period 4, one bake-rate step past animationTime 0.25) has moved it.
    REQUIRE_FALSE(near3(scene.light(trackedId)->position, authoredPosition));

    // Edits made during the preview: intensity on the tracked light, everything on the untracked
    // one. Stop owns only the animation-owned field -- the tracked light's position -- so both
    // sets of edits survive exactly like an object's untracked fields do.
    engine::LocalLight trackedEdit = *scene.light(trackedId);
    trackedEdit.intensity = 42.0f;
    trackedEdit.enabled = false;
    REQUIRE(scene.updateLight(trackedId, trackedEdit));
    const engine::LocalLight untrackedEdit{.type = engine::LocalLightType::Spot,
                                           .position = {50, 60, 70},
                                           .colour = {0.9f, 0.8f, 0.7f},
                                           .intensity = 99.0f,
                                           .range = 33.0f,
                                           .direction = {0.0f, -1.0f, 0.0f},
                                           .innerCone = 0.1f,
                                           .outerCone = 0.5f};
    REQUIRE(scene.updateLight(*untrackedId, untrackedEdit));

    session.advanceEditorFrame(true, followRail, false);
    REQUIRE_FALSE(scene.light(trackedId)->enabled);
    REQUIRE_FALSE(near3(scene.light(trackedId)->position, trackedEdit.position));
    REQUIRE(playback.stop(session, followRail));

    const engine::LocalLight* tracked = scene.light(trackedId);
    REQUIRE(tracked != nullptr);
    REQUIRE_FALSE(tracked->enabled);
    REQUIRE(near3(tracked->position, authoredPosition)); // only the animation-owned field reverts
    REQUIRE(tracked->intensity == 42.0f);                // the unrelated edit survives Stop

    const engine::LocalLight* untracked = scene.light(*untrackedId);
    REQUIRE(untracked != nullptr);
    REQUIRE(near3(untracked->position, untrackedEdit.position));
    REQUIRE(untracked->colour == untrackedEdit.colour);
    REQUIRE(untracked->intensity == untrackedEdit.intensity);
    REQUIRE(untracked->range == untrackedEdit.range);
}

//======================================================================================================================
TEST_CASE("editor stop never revives an orbit-tracked light removed during playback",
          "[app][editor-playback]") {
    auto scene = playbackScene();
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    const auto lightId = scene.localLights().front();
    EditorPlayback playback;
    bool followRail = false;

    REQUIRE(playback.play(session, followRail));
    session.advanceEditorFrame(true, followRail, false);
    REQUIRE(scene.removeLight(lightId));

    REQUIRE(playback.stop(session, followRail));
    REQUIRE(scene.light(lightId) == nullptr);
    REQUIRE(scene.localLights().empty());
}

//======================================================================================================================
TEST_CASE("editor step begins a restorable paused preview and follows the rail",
          "[app][editor-playback]") {
    auto scene = playbackScene();
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    const auto camera = session.camera();
    const auto pose = scene.objects[0].position;
    EditorPlayback playback;
    bool followRail = true;
    REQUIRE(playback.step(session, followRail));
    REQUIRE(playback.state() == PlaybackState::Paused);
    REQUIRE(scene.animationTime == Catch::Approx(0.25 + 1.0 / asset::kAnimationBakeRate));
    REQUIRE(session.camera().position.x == Catch::Approx(16));
    REQUIRE(playback.step(session, followRail));
    REQUIRE(session.camera().position.x == Catch::Approx(17));
    REQUIRE(playback.stop(session, followRail));
    REQUIRE(scene.animationTime == 0.25);
    REQUIRE(scene.objects[0].position == pose);
    requireCamera(session.camera(), camera);
    scene.objects[0].position = {80, 90, 100};
    scene.animationTime = 0.5;
    REQUIRE(playback.play(session, false));
    session.advanceEditorFrame(playback.playing(), false, false);
    REQUIRE(playback.stop(session, followRail));
    REQUIRE(scene.animationTime == 0.5);
    REQUIRE(scene.objects[0].position == glm::vec3(80, 90, 100));
    REQUIRE_FALSE(followRail);
}

//======================================================================================================================
TEST_CASE("editor stop restores stable identities without reviving removed instances",
          "[app][editor-playback]") {
    auto scene = playbackScene();
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    EditorPlayback playback;
    bool followRail = false;
    const auto removed = scene.objects[0].id;
    const auto emissive = scene.objects[1].id;
    const auto mesh = scene.objects[0].mesh;
    const auto material = scene.objects[0].material;
    REQUIRE(playback.play(session, followRail));
    session.advanceEditorFrame(true, false, false);
    scene.removeObject(removed);
    const auto replacement = scene.addObject(
        {.position = {100, 200, 300}, .mesh = mesh, .material = material, .emissiveStrength = 42});
    REQUIRE(replacement.slot == removed.slot);
    REQUIRE(replacement.generation != removed.generation);
    std::reverse(scene.objects.begin(), scene.objects.end());
    REQUIRE(playback.stop(session, followRail));
    REQUIRE(scene.tryObject(removed) == nullptr);
    REQUIRE(scene.tryObject(replacement)->position == glm::vec3(100, 200, 300));
    REQUIRE(scene.tryObject(replacement)->emissiveStrength == 42);
    REQUIRE(scene.tryObject(emissive)->emissiveStrength == 2.75f);
    REQUIRE(scene.objects.size() == 3);
}

//======================================================================================================================
TEST_CASE("editor playback discards a snapshot after the active scene changes",
          "[app][editor-playback]") {
    auto first = playbackScene();
    auto second = playbackScene();
    SceneSession session;
    session.activate(first, SceneActivationMotion::Reset);
    EditorPlayback playback;
    bool followRail = true;
    REQUIRE(playback.play(session, followRail));
    session.advanceEditorFrame(true, true, false);
    const auto firstTime = first.animationTime;
    session.activate(second, SceneActivationMotion::Reset);
    session.camera().position = {100, 200, 300};
    const auto secondCamera = session.camera();
    followRail = false;
    SECTION("Stop cannot restore another scene") {
        REQUIRE_FALSE(playback.stop(session, followRail));
        REQUIRE(playback.state() == PlaybackState::Stopped);
        REQUIRE_FALSE(followRail);
        REQUIRE(second.animationTime == 0.25);
        REQUIRE(second.objects[0].position.x == 7);
        requireCamera(session.camera(), secondCamera);
    }
    SECTION("Play captures the newly active scene") {
        REQUIRE(playback.play(session, followRail));
        session.advanceEditorFrame(true, true, false);
        REQUIRE(playback.stop(session, followRail));
        REQUIRE(second.animationTime == 0.25);
        requireCamera(session.camera(), secondCamera);
        REQUIRE_FALSE(followRail);
    }
    REQUIRE(first.animationTime == firstTime);
}

//======================================================================================================================
TEST_CASE("editor playback handles absent and empty scenes without implicit rewinds",
          "[app][editor-playback]") {
    SceneSession session;
    EditorPlayback playback;
    bool followRail = false;
    REQUIRE_FALSE(playback.play(session, followRail));
    REQUIRE_FALSE(playback.step(session, followRail));
    REQUIRE_FALSE(playback.stop(session, followRail));
    REQUIRE(playback.state() == PlaybackState::Stopped);
    engine::Scene scene;
    scene.animationTime = 3.5;
    session.activate(scene, SceneActivationMotion::Reset);
    REQUIRE(playback.play(session, followRail));
    session.advanceEditorFrame(playback.playing(), false, false);
    REQUIRE(scene.animationTime == 3.5);
    REQUIRE(playback.step(session, followRail));
    REQUIRE(scene.animationTime == Catch::Approx(3.5 + 1.0 / asset::kAnimationBakeRate));
    REQUIRE(playback.state() == PlaybackState::Paused);
    REQUIRE(playback.stop(session, followRail));
    REQUIRE(scene.animationTime == 3.5);
}
