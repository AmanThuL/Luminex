#include "App/Model/SceneSession.h"
#include "GraphTestSupport.h"

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
    const auto mesh = result.addMesh(render::makeCube(), "lmx.test.session");
    const auto material = result.addMaterial({});
    result.addObject({.name = "session object",
                      .position = {7.0f, 0.0f, 0.0f},
                      .mesh = mesh,
                      .material = material});
    // Model a loaded scene carrying an earlier accepted pose for the activation policy test.
    result.objects[0].previousModel = glm::mat4(1.0f);
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
    FakeDevice device;
    scene::Scene scene = makeSessionScene();
    scene.material(scene.objects[0].material).roughness = 0.25f;
    REQUIRE(scene.finalize(device));
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    session.advanceEditorFrame(true, false, false);
    device.frame = 1;
    REQUIRE(session.prepareFrame(device.frameNumber()));
    std::vector<render::DrawItem> items;
    const render::SceneView view = session.view(items, render::ShadowFilter::PCSS, true);
    REQUIRE(view.items.data() == items.data());
    REQUIRE(view.items.size() == 1);
    REQUIRE(view.items[0].instanceRow == scene.objects[0].id.slot);
    REQUIRE(view.items[0].mesh.firstIndex == scene.tryMesh(scene.objects[0].mesh)->firstIndex);
    REQUIRE(view.items[0].mesh.indexCount == scene.tryMesh(scene.objects[0].mesh)->indexCount);
    REQUIRE(view.shadowFilter == render::ShadowFilter::PCSS);
    REQUIRE(view.wireframe);
    render::InstanceRow instance;
    view.tables.instances->readback(&instance, sizeof(instance));
    REQUIRE(instance.model[3].x == Catch::Approx(1.0f));
    REQUIRE(instance.previousModel[3].x == 7.0f);
    scene.material(scene.objects[0].material).roughness = 0.75f;
    render::MaterialRow material;
    view.tables.materials->readback(&material, sizeof(material));
    REQUIRE(material.roughness == 0.25f);
    session.commitFrame();
    REQUIRE(scene.animationTime == 1.0 / asset::kAnimationBakeRate);
    REQUIRE(scene.objects[0].previousModel == scene.objects[0].modelMatrix());
    view.tables.instances->readback(&instance, sizeof(instance));
    REQUIRE(instance.previousModel[3].x == 7.0f);
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

//======================================================================================================================
TEST_CASE("SceneSession restores original static defaults after leaving and reactivating a scene",
          "[app][scene-session]") {
    auto first = makeSessionScene();
    first.animation.tracks.clear();
    first.lights[0].strength = {4.0f, 2.0f, 1.0f};
    auto second = makeSessionScene();
    SceneSession session;
    session.activate(first, SceneActivationMotion::Reset);
    session.editObject(0, {.position = {20.0f, 1.0f, 2.0f}});
    first.lights[0].strength = {10.0f, 10.0f, 10.0f};
    REQUIRE(session.objectChanged(0));
    REQUIRE(session.lightChanged(0));
    session.activate(second, SceneActivationMotion::Reset);
    session.activate(first, SceneActivationMotion::Reset);
    REQUIRE(first.objects[0].position.x == 20.0f);
    session.resetObject(0);
    session.resetLight(0);
    CHECK(first.objects[0].position == glm::vec3(7.0f, 0.0f, 0.0f));
    CHECK(first.objects[0].previousModel == first.objects[0].modelMatrix());
    CHECK(first.lights[0].strength == glm::vec3(4.0f, 2.0f, 1.0f));
    CHECK_FALSE(session.objectChanged(0));
    CHECK_FALSE(session.lightChanged(0));
}

//======================================================================================================================
TEST_CASE(
    "SceneSession resets one animated transform at current time without discarding other edits",
    "[app][scene-session]") {
    auto scene = makeSessionScene();
    scene.addObject({.name = "other",
                     .position = {9.0f, 0.0f, 0.0f},
                     .mesh = scene.objects[0].mesh,
                     .material = scene.objects[0].material});
    scene.animation.tracks.push_back({.objectIndex = 1,
                                      .keys = {{.time = 0.0, .translation = {2.0f, 0.0f, 0.0f}},
                                               {.time = 1.0, .translation = {4.0f, 0.0f, 0.0f}}}});
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    scene.animationTime = 0.5;
    session.editObject(0, {.position = {100.0f, 0.0f, 0.0f}});
    session.editObject(1, {.position = {200.0f, 0.0f, 0.0f}});
    scene.lights[1].strength = {6.0f, 5.0f, 4.0f};
    session.resetObject(0);
    CHECK(scene.animationTime == 0.5);
    CHECK(scene.objects[0].position.x == Catch::Approx(30.0f));
    CHECK(scene.objects[0].previousModel == scene.objects[0].modelMatrix());
    CHECK(scene.objects[1].position.x == 200.0f);
    CHECK(scene.lights[1].strength == glm::vec3(6.0f, 5.0f, 4.0f));
    CHECK_FALSE(session.objectChanged(0));
    CHECK(session.objectChanged(1));
}
