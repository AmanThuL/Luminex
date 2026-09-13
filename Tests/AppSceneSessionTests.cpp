#include "App/Model/SceneSession.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <vector>

namespace {

using lmx::app::SceneActivationMotion;
using lmx::app::SceneSession;
namespace asset = lmx::asset;
namespace render = lmx::render;
namespace scene = lmx::scene;

//======================================================================================================================
scene::Scene makeSessionScene() {
    scene::Scene result;
    result.meshes.resize(1);
    result.materials.resize(1);
    result.objects.push_back({.name = "session object", .position = {7.0f, 0.0f, 0.0f}});
    result.initialCamera = {.position = {3.0f, 4.0f, 5.0f},
                            .yaw = 0.2f,
                            .pitch = -0.1f,
                            .fovY = 0.8f,
                            .nearZ = 0.25f,
                            .farZ = 500.0f};
    result.animation.duration = 1.0;
    result.animation.tracks.push_back(
        {.objectIndex = 0,
         .keys = {{.time = 0.0, .translation = {0.0f, 0.0f, 0.0f}},
                  {.time = 1.0, .translation = {60.0f, 0.0f, 0.0f}}}});
    result.animation.cameraTrack = {
        {.time = 0.0, .position = {0.0f, 1.0f, 2.0f}},
        {.time = 1.0, .position = {60.0f, 1.0f, 2.0f}, .yaw = 0.6f, .pitch = 0.3f}};
    return result;
}

} // namespace

//======================================================================================================================
TEST_CASE("SceneSession activation restores camera and preserves each caller's motion policy",
          "[app][scene-session]") {
    scene::Scene first = makeSessionScene();
    SceneSession session;
    REQUIRE(session.activeScene() == nullptr);
    session.activate(first, SceneActivationMotion::PreserveLoadedMotion);
    REQUIRE(session.activeScene() == &first);
    REQUIRE(&session.scene() == &first);
    REQUIRE(session.camera().position == first.initialCamera.position);
    REQUIRE(session.camera().yaw == first.initialCamera.yaw);
    REQUIRE(session.camera().pitch == first.initialCamera.pitch);
    REQUIRE(session.camera().fovY == first.initialCamera.fovY);
    REQUIRE(session.camera().nearZ == first.initialCamera.nearZ);
    REQUIRE(session.camera().farZ == first.initialCamera.farZ);
    REQUIRE(first.objects[0].previousModel == glm::mat4(1.0f));

    scene::Scene second = makeSessionScene();
    second.animationTime = 0.75;
    second.objects[0].position.x = 12.0f;
    second.initialCamera.position.x = -3.0f;
    session.camera().position.x = 99.0f;
    session.activate(second, SceneActivationMotion::Reset);
    REQUIRE(session.activeScene() == &second);
    REQUIRE(session.camera().position == second.initialCamera.position);
    REQUIRE(second.animationTime == 0.75);
    REQUIRE(second.objects[0].position.x == 12.0f);
    REQUIRE(second.objects[0].previousModel == second.objects[0].modelMatrix());
    REQUIRE(first.objects[0].previousModel == glm::mat4(1.0f));
}

//======================================================================================================================
TEST_CASE("SceneSession editor playback preserves paused follow and fly-camera override",
          "[app][scene-session]") {
    scene::Scene scene = makeSessionScene();
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    session.camera().fovY = 1.2f;
    session.camera().nearZ = 0.5f;
    session.camera().farZ = 1000.0f;

    session.advanceEditorFrame(true, true, false);
    REQUIRE(scene.animationTime == 1.0 / asset::kAnimationBakeRate);
    REQUIRE(scene.objects[0].position.x == Catch::Approx(1.0f));
    REQUIRE(session.camera().position.x == Catch::Approx(1.0f));
    REQUIRE(session.camera().fovY == 1.2f);
    REQUIRE(session.camera().nearZ == 0.5f);
    REQUIRE(session.camera().farZ == 1000.0f);

    session.camera().position.x = 99.0f;
    session.advanceEditorFrame(false, true, false);
    REQUIRE(scene.animationTime == 1.0 / asset::kAnimationBakeRate);
    REQUIRE(session.camera().position.x == Catch::Approx(1.0f));

    session.camera().position.x = 99.0f;
    session.advanceEditorFrame(true, true, true);
    REQUIRE(scene.animationTime == 2.0 / asset::kAnimationBakeRate);
    REQUIRE(scene.objects[0].position.x == Catch::Approx(2.0f));
    REQUIRE(session.camera().position.x == 99.0f);

    session.advanceEditorFrame(false, false, false);
    REQUIRE(session.camera().position.x == 99.0f);
    session.advanceEditorFrame(false, true, false);
    REQUIRE(session.camera().position.x == Catch::Approx(2.0f));
}

//======================================================================================================================
TEST_CASE("SceneSession screenshot keeps authored frame zero before fixed-step playback",
          "[app][scene-session]") {
    scene::Scene scene = makeSessionScene();
    SceneSession session;
    session.activate(scene, SceneActivationMotion::PreserveLoadedMotion);
    session.prepareScreenshotFrame(0);
    REQUIRE(scene.animationTime == 0.0);
    REQUIRE(scene.objects[0].position.x == 7.0f);
    REQUIRE(scene.objects[0].previousModel == glm::mat4(1.0f));
    REQUIRE(session.camera().position == scene.animation.cameraTrack.front().position);

    session.prepareScreenshotFrame(1);
    REQUIRE(scene.animationTime == 1.0 / asset::kAnimationBakeRate);
    REQUIRE(scene.objects[0].position.x == Catch::Approx(1.0f));
    REQUIRE(session.camera().position.x == Catch::Approx(1.0f));
    REQUIRE(scene.objects[0].previousModel == glm::mat4(1.0f));
}

//======================================================================================================================
TEST_CASE("SceneSession sequence samples absolute frames across the looping clip boundary",
          "[app][scene-session]") {
    scene::Scene scene = makeSessionScene();
    scene.animation.duration = 3.0 / asset::kAnimationBakeRate;
    scene.animation.tracks[0].keys.back().time = scene.animation.duration;
    scene.animation.tracks[0].keys.back().translation.x = 3.0f;
    scene.animation.cameraTrack.back().time = scene.animation.duration;
    scene.animation.cameraTrack.back().position.x = 3.0f;
    SceneSession session;
    session.activate(scene, SceneActivationMotion::PreserveLoadedMotion);

    session.prepareSequenceFrame(0);
    REQUIRE(scene.objects[0].position.x == 0.0f);
    session.prepareSequenceFrame(4);
    REQUIRE(scene.animationTime == 4.0 / asset::kAnimationBakeRate);
    REQUIRE(scene.objects[0].position.x == 3.0f);
    REQUIRE(session.camera().position.x == 3.0f);

    session.rewindAnimation();
    for (uint32_t frame = 0; frame <= 4; ++frame) {
        session.prepareScreenshotFrame(frame);
    }
    REQUIRE(scene.animationTime == Catch::Approx(1.0 / asset::kAnimationBakeRate));
    REQUIRE(scene.objects[0].position.x == Catch::Approx(1.0f));
    REQUIRE(session.camera().position.x == Catch::Approx(1.0f));
}

//======================================================================================================================
TEST_CASE("SceneSession view borrows item storage while commit and rewind preserve frame ownership",
          "[app][scene-session]") {
    scene::Scene scene = makeSessionScene();
    scene.materials[0].roughness = 0.25f;
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    session.advanceEditorFrame(true, false, false);
    std::vector<render::DrawItem> items;
    const render::SceneView view = session.view(items, render::ShadowFilter::PCSS, true);
    REQUIRE(view.items.data() == items.data());
    REQUIRE(view.items.size() == 1);
    REQUIRE(view.items[0].mesh == &scene.meshes[0]);
    REQUIRE(view.shadowFilter == render::ShadowFilter::PCSS);
    REQUIRE(view.wireframe);
    REQUIRE(view.items[0].model[3].x == Catch::Approx(1.0f));
    REQUIRE(view.items[0].previousModel[3].x == 7.0f);
    scene.materials[0].roughness = 0.75f;
    REQUIRE(view.items[0].material.roughness == 0.25f);

    session.commitFrame();
    REQUIRE(scene.animationTime == 1.0 / asset::kAnimationBakeRate);
    REQUIRE(scene.objects[0].previousModel == scene.objects[0].modelMatrix());
    REQUIRE(view.items[0].previousModel[3].x == 7.0f);
    session.rewindAnimation();
    REQUIRE(scene.animationTime == 0.0);
    REQUIRE(scene.objects[0].position.x == 0.0f);
    REQUIRE(scene.objects[0].previousModel == scene.objects[0].modelMatrix());
    scene.objects[0].position.x = 8.0f;
    session.resetMotion();
    REQUIRE(scene.objects[0].previousModel[3].x == 8.0f);
}

//======================================================================================================================
TEST_CASE("SceneSession playback includes camera and emissive tracks but leaves static clocks idle",
          "[app][scene-session]") {
    scene::Scene scene = makeSessionScene();
    scene.animation.tracks.clear();
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);

    SECTION("camera-only clips advance") {
        session.advanceEditorFrame(true, true, false);
        REQUIRE(scene.animationTime == 1.0 / asset::kAnimationBakeRate);
        REQUIRE(session.camera().position.x == Catch::Approx(1.0f));
    }
    SECTION("emissive-only clips advance") {
        scene.animation.cameraTrack.clear();
        scene.animation.emissiveTracks.push_back(
            {.objectIndex = 0,
             .keys = {{.time = 0.0, .strength = 2.0f},
                      {.time = 1.0 / asset::kAnimationBakeRate, .strength = 4.0f}}});
        session.advanceEditorFrame(true, false, false);
        REQUIRE(scene.animationTime == 1.0 / asset::kAnimationBakeRate);
        REQUIRE(scene.objects[0].emissiveStrength == 4.0f);
    }
    SECTION("static scenes retain implicit time but accept explicit time") {
        scene.animation.cameraTrack.clear();
        session.advanceEditorFrame(true, true, false);
        session.prepareScreenshotFrame(1);
        REQUIRE(scene.animationTime == 0.0);
        session.stepAnimation();
        REQUIRE(scene.animationTime == 1.0 / asset::kAnimationBakeRate);
        session.prepareSequenceFrame(120);
        REQUIRE(scene.animationTime == 2.0);
    }
}
