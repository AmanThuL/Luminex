#include "App/Model/SceneSession.h"
#include "GraphTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <cmath>
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
TEST_CASE("SceneSession::stepAnimation advances an orbit-tracked light by exactly one bake-rate "
          "step",
          "[app][scene-session]") {
    scene::Scene scene = makeSessionScene();
    scene.animation.tracks.clear();
    const auto lightId = scene.addLight({.type = render::LocalLightType::Point,
                                         .position = {0.0f, 0.0f, 0.0f},
                                         .colour = {1.0f, 1.0f, 1.0f},
                                         .intensity = 1.0f,
                                         .range = 5.0f});
    REQUIRE(lightId.has_value());
    // Period is exactly 4 bake-rate steps, so one stepAnimation() call is a quarter turn: a
    // hand-computable case that also pins the documented basis rule (sampleOrbit's doc comment).
    // axis +Y is nearly parallel to the deterministic basis's default reference (world up), so the
    // basis falls back to world +X: u = world +Z, v = world +X. At angle == pi/2, position ==
    // centre + radius * v, i.e. (radius, 0, 0).
    const asset::LightOrbitTrack track{.light = 0,
                                       .centre = {0.0f, 0.0f, 0.0f},
                                       .axis = {0.0f, 1.0f, 0.0f},
                                       .radius = 2.0f,
                                       .phase = 0.0f,
                                       .period =
                                           4.0f / static_cast<float>(asset::kAnimationBakeRate)};
    scene.animation.lightTracks.push_back(track);
    SceneSession session;
    session.activate(scene, SceneActivationMotion::PreserveLoadedMotion);

    session.stepAnimation();

    REQUIRE(scene.animationTime == 1.0 / asset::kAnimationBakeRate);
    const render::LocalLight* moved = scene.light(*lightId);
    REQUIRE(moved != nullptr);
    REQUIRE(std::abs(moved->position.x - 2.0f) < 1e-4f);
    REQUIRE(std::abs(moved->position.y) < 1e-4f);
    REQUIRE(std::abs(moved->position.z) < 1e-4f);
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

//======================================================================================================================
TEST_CASE("SceneSession local-light reset uses full identities and current orbit position",
          "[app][scene-session][local-light-editor]") {
    scene::Scene scene;
    render::LocalLight authored;
    authored.position = {1.0f, 2.0f, 3.0f};
    const auto id = scene.addLight(authored);
    REQUIRE(id);
    scene.animation.lightTracks.push_back({.light = 0,
                                           .centre = {0.0f, 2.0f, 0.0f},
                                           .axis = {0.0f, 1.0f, 0.0f},
                                           .radius = 3.0f,
                                           .phase = 0.0f,
                                           .period = 4.0f});
    SceneSession session;
    session.activate(scene, SceneActivationMotion::PreserveLoadedMotion);
    scene.animationTime = 1.0;
    scene.animate(scene.animationTime);
    REQUIRE_FALSE(session.localLightChanged(*id));
    auto edited = *scene.light(*id);
    edited.position = {90.0f, 0.0f, 0.0f};
    edited.intensity = 9.0f;
    edited.enabled = false;
    REQUIRE(session.editLocalLight(*id, edited));
    REQUIRE(session.localLightChanged(*id));
    REQUIRE(session.resetLocalLight(*id));
    REQUIRE(scene.light(*id)->position == asset::sampleOrbit(scene.animation.lightTracks[0], 1.0f));
    REQUIRE(scene.light(*id)->intensity == authored.intensity);
    REQUIRE(scene.light(*id)->enabled);
    REQUIRE(scene.removeLight(*id));
    const auto replacement = scene.addLight(edited);
    REQUIRE(replacement);
    REQUIRE_FALSE(session.localLightDefault(*id));
    REQUIRE_FALSE(session.resetLocalLight(*id));
    REQUIRE_FALSE(session.editLocalLight(*id, authored));
    REQUIRE(scene.light(*replacement)->intensity == 9.0f);
}

//======================================================================================================================
TEST_CASE("SceneSession pile edits preserve grid and unrelated identities and obey capacity",
          "[app][scene-session][local-light-editor]") {
    scene::Scene scene;
    scene.lightLabGridCount = 2;
    const auto grid0 = scene.addLight(render::LocalLight{});
    const auto grid1 = scene.addLight(render::LocalLight{});
    const auto authoredPile = scene.addLight(render::LocalLight{});
    REQUIRE(grid0);
    REQUIRE(grid1);
    REQUIRE(authoredPile);
    SceneSession session;
    session.activate(scene, SceneActivationMotion::PreserveLoadedMotion);
    REQUIRE(session.lightLabPileAvailable());
    REQUIRE(session.lightLabPileCount() == 1);
    REQUIRE(session.setLightLabPile(0));
    REQUIRE_FALSE(scene.light(*authoredPile));
    REQUIRE_FALSE(session.resetLocalLight(*authoredPile));
    REQUIRE(scene.light(*grid0));
    REQUIRE(scene.light(*grid1));
    const auto unrelated = scene.addLight(render::LocalLight{});
    REQUIRE(unrelated);
    REQUIRE(session.lightLabPileCapacity() == render::kMaxLocalLights - 3);
    REQUIRE_FALSE(session.setLightLabPile(render::kMaxLocalLights - 2));
    REQUIRE(scene.localLights().size() == 3);
    REQUIRE(session.setLightLabPile(140));
    REQUIRE(session.lightLabPileCount() == 140);
    REQUIRE(session.setLightLabPile(0));
    REQUIRE(scene.localLights().size() == 3);
    REQUIRE(scene.light(*unrelated));
}
