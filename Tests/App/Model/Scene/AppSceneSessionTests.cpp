#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Scene/SceneSession.h"
#include "Support/GraphTestSupport.h"
#include "Support/SceneDocumentFixtures.h"
#include "Support/SceneDocumentTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <bit>
#include <cmath>
#include <vector>

namespace {

using lmx::app::SceneActivationMotion;
using lmx::app::SceneSession;
namespace asset = lmx::asset;
namespace render = lmx::render;
namespace engine = lmx::engine;

//======================================================================================================================
engine::Scene makeSessionScene() {
    engine::Scene result;
    const auto mesh = result.addMesh(engine::makeCube(), "lmx.test.session");
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
    engine::Scene first = makeSessionScene();
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

    engine::Scene second = makeSessionScene();
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
    engine::Scene scene = makeSessionScene();
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
    engine::Scene scene = makeSessionScene();
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
    engine::Scene scene = makeSessionScene();
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
    engine::Scene scene = makeSessionScene();
    scene.material(scene.objects[0].material).roughness = 0.25f;
    REQUIRE(scene.finalize(device));
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    session.advanceEditorFrame(true, false, false);
    device.frame = 1;
    REQUIRE(session.prepareFrame(device.frameNumber()));
    std::vector<engine::DrawItem> items;
    scene.look.shadowFilter = asset::ShadowFilter::PCSS;
    const render::SceneView view = session.view(items, true);
    REQUIRE(view.items.data() == items.data());
    REQUIRE(view.items.size() == 1);
    REQUIRE(view.items[0].instanceRow == scene.objects[0].id.slot);
    REQUIRE(view.items[0].mesh.firstIndex == scene.tryMesh(scene.objects[0].mesh)->firstIndex);
    REQUIRE(view.items[0].mesh.indexCount == scene.tryMesh(scene.objects[0].mesh)->indexCount);
    REQUIRE(view.shadowFilter == render::ShadowFilter::PCSS);
    REQUIRE(view.wireframe);
    engine::InstanceRow instance;
    view.tables.instances->readback(&instance, sizeof(instance));
    REQUIRE(instance.model[3].x == Catch::Approx(1.0f));
    REQUIRE(instance.previousModel[3].x == 7.0f);
    scene.material(scene.objects[0].material).roughness = 0.75f;
    engine::MaterialRow material;
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
    engine::Scene scene = makeSessionScene();
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
    engine::Scene scene = makeSessionScene();
    scene.animation.tracks.clear();
    const auto lightId = scene.addLight({.type = engine::LocalLightType::Point,
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
    const engine::LocalLight* moved = scene.light(*lightId);
    REQUIRE(moved != nullptr);
    REQUIRE(std::abs(moved->position.x - 2.0f) < 1e-4f);
    REQUIRE(std::abs(moved->position.y) < 1e-4f);
    REQUIRE(std::abs(moved->position.z) < 1e-4f);
}

//======================================================================================================================
TEST_CASE("SceneSession restores original static defaults after leaving and reactivating a scene",
          "[app][scene-session]") {
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>(makeSessionScene())};
    loaded.document = lmx::test::contentDocument();
    loaded.document.nodes[1].mobility = asset::DocMobility::Movable;
    loaded.objectMobility = {asset::DocMobility::Movable};
    loaded.binding.nodes.resize(loaded.document.nodes.size());
    loaded.binding.objectNode = {1};
    loaded.binding.objectImportedNode = {engine::kGeneratedNode};
    loaded.binding.objectGeneratorNode = {engine::kGeneratedNode};
    auto& first = *loaded.scene;
    first.animation.tracks.clear();
    first.lights[0].strength = {4.0f, 2.0f, 1.0f};
    auto second = makeSessionScene();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    REQUIRE(session.editObject(0, {.position = {20.0f, 1.0f, 2.0f}}));
    first.lights[0].strength = {10.0f, 10.0f, 10.0f};
    REQUIRE(session.objectChanged(0));
    REQUIRE(session.lightChanged(0));
    session.activate(second, SceneActivationMotion::Reset);
    session.activate(loaded, SceneActivationMotion::Reset);
    REQUIRE(first.objects[0].position.x == 20.0f);
    REQUIRE(session.resetObject(0));
    session.resetLight(0);
    CHECK(first.objects[0].position == glm::vec3(7.0f, 0.0f, 0.0f));
    CHECK(first.objects[0].previousModel == first.objects[0].modelMatrix());
    CHECK(first.lights[0].strength == glm::vec3(4.0f, 2.0f, 1.0f));
    CHECK_FALSE(session.objectChanged(0));
    CHECK_FALSE(session.lightChanged(0));
}

//======================================================================================================================
TEST_CASE("SceneSession refuses animated transform edits and exposes defaults at the current time",
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
    scene.animate(scene.animationTime);
    const auto first = scene.objects[0];
    const auto second = scene.objects[1];
    const auto generation = session.editGeneration();
    const auto edit = session.editObject(0, {.position = {100.0f, 0.0f, 0.0f}});
    REQUIRE_FALSE(edit);
    CHECK(edit.error().message == "Animation owns this transform");
    REQUIRE_FALSE(session.editObject(1, {.position = {200.0f, 0.0f, 0.0f}}));
    scene.lights[1].strength = {6.0f, 5.0f, 4.0f};
    const auto reset = session.resetObject(0);
    REQUIRE_FALSE(reset);
    CHECK(reset.error().message == "Animation owns this transform");
    CHECK(scene.animationTime == 0.5);
    CHECK(session.objectDefault(0).position.x == Catch::Approx(30.0f));
    CHECK(scene.objects[0].position == first.position);
    CHECK(scene.objects[0].previousModel == first.previousModel);
    CHECK(scene.objects[1].position == second.position);
    CHECK(scene.objects[1].previousModel == second.previousModel);
    CHECK(scene.lights[1].strength == glm::vec3(6.0f, 5.0f, 4.0f));
    CHECK_FALSE(session.objectChanged(0));
    CHECK_FALSE(session.objectChanged(1));
    CHECK(session.editGeneration() == generation);
}

//======================================================================================================================
TEST_CASE("SceneSession reset uses each asset clip's unwrapped phase and source hierarchy",
          "[app][scene-session][ux3]") {
    engine::Scene scene = makeSessionScene();
    scene.animation.tracks.clear();
    scene.animation.duration = 24.0;
    engine::AssetClipPlayback asset;
    asset.rootWorld = glm::translate(glm::mat4(1), glm::vec3(10, 0, 0));
    asset.instances = {scene.objects[0].id};
    asset.nodes.resize(2);
    asset.nodes[1].parent = 0;
    asset.nodes[1].animated = true;
    asset.nodes[1].instances = {0};
    asset.clips = {{.duration = 2.0,
                    .channels = {{.node = 0,
                                  .path = asset::GltfAnimationPath::Translation,
                                  .keys = {{.time = 0, .value = {0, 0, 0, 0}},
                                           {.time = 2, .value = {2, 0, 0, 0}}}}}},
                   {.duration = 3.5,
                    .channels = {{.node = 1,
                                  .path = asset::GltfAnimationPath::Translation,
                                  .keys = {{.time = 0, .value = {0, 0, 0, 0}},
                                           {.time = 3.5, .value = {3.5, 0, 0, 0}}}}}}};
    scene.assetAnimations.push_back(std::move(asset));
    SceneSession session;
    session.activate(scene, SceneActivationMotion::Reset);
    scene.animationTime = 0.5;
    scene.unwrappedAnimationTime = 24.5;
    scene.animate(scene.animationTime, scene.unwrappedAnimationTime);
    REQUIRE(scene.objects[0].position.x == Catch::Approx(10.5f));
    CHECK_FALSE(session.objectChanged(0));
    const auto before = scene.objects[0];
    const auto generation = session.editGeneration();
    const auto edit = session.editObject(0, {.position = {100, 0, 0}});
    REQUIRE_FALSE(edit);
    CHECK(edit.error().message == "Animation owns this transform");
    const auto reset = session.resetObject(0);
    REQUIRE_FALSE(reset);
    CHECK(reset.error().message == "Animation owns this transform");
    CHECK(session.objectDefault(0).position.x == Catch::Approx(10.5f));
    CHECK(scene.objects[0].position == before.position);
    CHECK(scene.objects[0].previousModel == before.previousModel);
    CHECK(session.editGeneration() == generation);
    CHECK(scene.objects[0].position.x == Catch::Approx(10.5f));
    CHECK_FALSE(session.objectChanged(0));
}

//======================================================================================================================
TEST_CASE("SceneSession local-light reset uses full identities and current orbit position",
          "[app][scene-session][local-light-editor]") {
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.document.nodes = {{.name = "Generated lights"}};
    loaded.document.rootNodes = {0};
    loaded.binding.nodes.resize(1);
    auto& scene = *loaded.scene;
    engine::LocalLight authored;
    authored.position = {1.0f, 2.0f, 3.0f};
    const auto id = scene.addLight(authored);
    REQUIRE(id);
    scene.animation.lightTracks.push_back({.light = 0,
                                           .centre = {0.0f, 2.0f, 0.0f},
                                           .axis = {0.0f, 1.0f, 0.0f},
                                           .radius = 3.0f,
                                           .phase = 0.0f,
                                           .period = 4.0f});
    loaded.binding.lightGeneratorNode[engine::sceneLightKey(*id)] = 0;
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
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
    engine::Scene scene;
    const auto grid0 = scene.addLight(engine::LocalLight{});
    const auto grid1 = scene.addLight(engine::LocalLight{});
    const auto authoredPile = scene.addLight(engine::LocalLight{});
    REQUIRE(grid0);
    REQUIRE(grid1);
    REQUIRE(authoredPile);
    scene.lightLabPopulations.push_back({.grid = {*grid0, *grid1}, .pile = {*authoredPile}});
    SceneSession session;
    session.activate(scene, SceneActivationMotion::PreserveLoadedMotion);
    REQUIRE(session.lightLabPileAvailable());
    REQUIRE(session.lightLabPileCount() == 1);
    REQUIRE(session.setLightLabPile(0));
    REQUIRE_FALSE(scene.light(*authoredPile));
    REQUIRE_FALSE(session.resetLocalLight(*authoredPile));
    REQUIRE(scene.light(*grid0));
    REQUIRE(scene.light(*grid1));
    const auto unrelated = scene.addLight(engine::LocalLight{});
    REQUIRE(unrelated);
    REQUIRE(session.lightLabPileCapacity() == engine::kMaxLocalLights - 3);
    REQUIRE_FALSE(session.setLightLabPile(engine::kMaxLocalLights - 2));
    REQUIRE(scene.localLights().size() == 3);
    REQUIRE(session.setLightLabPile(140));
    REQUIRE(session.lightLabPileCount() == 140);
    REQUIRE(session.setLightLabPile(0));
    REQUIRE(scene.localLights().size() == 3);
    REQUIRE(scene.light(*unrelated));
}

//======================================================================================================================
TEST_CASE("local light rig override is a no-op without a document group", "[app][scene-doc]") {
    lmx::engine::Scene scene;
    lmx::app::SceneSession session;
    session.activate(scene, lmx::app::SceneActivationMotion::Reset);
    REQUIRE_FALSE(session.localLightRigAvailable());
    REQUIRE(session.setLocalLightRig(true));
    REQUIRE(scene.localLights().empty());
}

//======================================================================================================================
TEST_CASE("CLI rig override preserves an authored-off child and off group state",
          "[app][scene-doc]") {
    auto loaded = lmx::test::documentLightFixture();
    const auto group = *loaded.binding.localLightGroup;
    loaded.document.nodes[group].enabled = false;
    const auto id = loaded.scene->rigLightIds().front();
    const auto child = loaded.binding.lightNode.at(lmx::engine::sceneLightKey(id));
    loaded.document.nodes[child].enabled = false;
    for (const auto lightId : loaded.scene->rigLightIds()) {
        auto light = *loaded.scene->light(lightId);
        light.enabled = false;
        REQUIRE(loaded.scene->updateLight(lightId, light));
    }
    lmx::app::SceneSession session;
    session.activate(loaded, lmx::app::SceneActivationMotion::Reset);
    REQUIRE(session.setLocalLightRig(true));
    REQUIRE(loaded.scene->enabledLightCount() == 15);
    REQUIRE_FALSE(loaded.scene->light(id)->enabled);
    REQUIRE_FALSE(session.documentState().nodeEnabled[group]);
    REQUIRE_FALSE(session.documentState().nodeEnabled[child]);
}

//======================================================================================================================
TEST_CASE("saved mesh enabled edits share document own flags and ancestor effective flags",
          "[app][scene-session][ux6-mesh-export]") {
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.document = lmx::test::contentDocument();
    loaded.document.nodes.push_back({.name = "Disabled parent", .children = {1}, .enabled = false});
    loaded.document.rootNodes = {0, 2, 3};
    loaded.binding.nodes.resize(4);
    loaded.binding.objectNode = {1, 2};
    loaded.binding.objectImportedNode.assign(2, engine::kGeneratedNode);
    loaded.binding.objectGeneratorNode.assign(2, engine::kGeneratedNode);
    const auto mesh = loaded.scene->addMesh(engine::makeCube(), "lmx.test.mesh.enabled");
    const auto material = loaded.scene->addMaterial({});
    for (uint32_t n = 1; n <= 2; ++n) {
        loaded.binding.nodes[n].objects = {n - 1};
        const auto& node = loaded.document.nodes[n];
        loaded.scene->addObject({.name = node.name,
                                 .position = node.translation,
                                 .scale = node.scale,
                                 .mesh = mesh,
                                 .material = material,
                                 .enabled = false});
    }
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    CHECK(session.objectEnabled(0));
    CHECK_FALSE(session.nodeEffectiveEnabled(1));
    CHECK_FALSE(session.scene().objects[0].enabled);
    auto generation = session.editGeneration();
    REQUIRE(session.setObjectEnabled(0, true));
    CHECK(session.editGeneration() == generation);
    REQUIRE(session.setObjectEnabled(0, false));
    CHECK_FALSE(session.nodeEnabled(1));
    CHECK_FALSE(session.objectEnabled(0));
    CHECK_FALSE(session.scene().objects[0].enabled);
    CHECK(session.editGeneration() == ++generation);
    REQUIRE(session.setObjectEnabled(0, true));
    CHECK(session.nodeEnabled(1));
    CHECK(session.objectEnabled(0));
    CHECK_FALSE(session.scene().objects[0].enabled);
    CHECK(session.editGeneration() == ++generation);
    REQUIRE(session.setNodeEnabled(3, true));
    CHECK(session.nodeEffectiveEnabled(1));
    CHECK(session.scene().objects[0].enabled);
    CHECK_FALSE(session.scene().objects[1].enabled);
    REQUIRE(session.setNodeEnabled(1, false));
    CHECK_FALSE(session.objectEnabled(0));
    CHECK_FALSE(session.scene().objects[0].enabled);
    generation = session.editGeneration();
    REQUIRE(session.setObjectEnabled(0, false));
    CHECK(session.editGeneration() == generation);
    session.setMeasurementActive(true);
    CHECK_FALSE(session.setObjectEnabled(0, true));
    CHECK_FALSE(session.setNodeEnabled(1, true));
    CHECK_FALSE(session.objectEnabled(0));
    CHECK_FALSE(session.scene().objects[0].enabled);
    CHECK(session.editGeneration() == generation);
}

//======================================================================================================================
TEST_CASE("SceneSession object pose locks refuse edits and resets without mutation",
          "[app][scene-session][ux6-pose-lock]") {
    using lmx::app::PoseLock;
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.document = lmx::test::contentDocument();
    loaded.document.nodes[2].mobility = asset::DocMobility::Movable;
    loaded.binding.nodes.resize(loaded.document.nodes.size());
    loaded.binding.objectNode = {1, 2};
    loaded.binding.objectImportedNode.assign(2, engine::kGeneratedNode);
    loaded.binding.objectGeneratorNode.assign(2, engine::kGeneratedNode);
    loaded.objectMobility = {asset::DocMobility::Static, asset::DocMobility::Movable};
    loaded.scene->objects.resize(2);
    for (size_t i = 0; i < 2; ++i) {
        loaded.binding.nodes[i + 1].objects = {i};
        loaded.scene->objects[i].position = {1, 2, 3};
        loaded.scene->objects[i].previousModel = glm::mat4(2.0f);
    }
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
    PoseLock expected = PoseLock::Static;
    SECTION("static") {}
    SECTION("generated takes precedence over its static mobility") {
        loaded.binding.objectGeneratorNode[0] = 1;
        expected = PoseLock::Generated;
    }
    SECTION("rigid animation takes precedence over its static mobility") {
        loaded.scene->animation.tracks.push_back({.objectIndex = 0});
        expected = PoseLock::Animated;
    }
    SECTION("imported animation ownership is retained without a rigid track") {
        loaded.binding.objectImportedNode[0] = 0;
        loaded.binding.importedNodes.push_back({.assetRoot = 1, .animated = true, .objects = {0}});
        expected = PoseLock::Animated;
    }
    SECTION("measurement takes precedence over generated and animated ownership") {
        session.setMeasurementActive(true);
        loaded.binding.objectGeneratorNode[0] = 1;
        loaded.scene->animation.tracks.push_back({.objectIndex = 0});
        expected = PoseLock::Measuring;
    }
    CHECK(session.objectPoseLock(0) == expected);
    const auto before = loaded.scene->objects[0];
    const auto generation = session.editGeneration();
    const auto refused = session.editObject(0, {.position = {9, 8, 7}});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message == lmx::app::poseLockReason(expected));
    const auto reset = session.resetObject(0);
    REQUIRE_FALSE(reset);
    CHECK(reset.error().message == lmx::app::poseLockReason(expected));
    CHECK(loaded.scene->objects[0].position == before.position);
    CHECK(loaded.scene->objects[0].eulerDegrees == before.eulerDegrees);
    CHECK(loaded.scene->objects[0].scale == before.scale);
    CHECK(loaded.scene->objects[0].previousModel == before.previousModel);
    CHECK(session.editGeneration() == generation);
    if (expected == PoseLock::Static) {
        REQUIRE(session.setObjectEnabled(0, false));
        CHECK_FALSE(session.objectEnabled(0));
        CHECK(session.objectPoseLock(0) == PoseLock::Static);
        CHECK(session.editGeneration() == generation + 1);
        CHECK(session.objectPoseLock(1) == PoseLock::None);
        CHECK(session.objectTransformPersistable(1));
        REQUIRE(session.editObject(1, {.position = {4, 5, 6}}));
        CHECK(loaded.scene->objects[1].position == glm::vec3(4, 5, 6));
        REQUIRE(session.resetObject(1));
        CHECK(loaded.scene->objects[1].position == glm::vec3(1, 2, 3));
    }
}

//======================================================================================================================
TEST_CASE("SceneSession pose locks have stable reasons and bounded invalid-subject handling",
          "[app][scene-session][ux6-pose-lock]") {
    using lmx::app::PoseLock;
    using lmx::app::poseLockReason;
    CHECK(poseLockReason(PoseLock::None).empty());
    CHECK(poseLockReason(PoseLock::Static) == "Static: mobility is authored in the scene file");
    CHECK(poseLockReason(PoseLock::Generated) == "Generated objects are placed by their generator");
    CHECK(poseLockReason(PoseLock::Animated) == "Animation owns this transform");
    CHECK(poseLockReason(PoseLock::Measuring) == "Pose edits are unavailable during measurement");
    SceneSession session;
    CHECK_FALSE(session.editObject(0, {}));
    CHECK_FALSE(session.resetObject(0));
    CHECK_FALSE(session.editLocalLight({}, {}));
    CHECK_FALSE(session.editLight(0, {}));
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.scene->objects.emplace_back();
    session.activate(loaded, SceneActivationMotion::Reset);
    CHECK(session.objectPoseLock(0) == PoseLock::Static);
    CHECK_FALSE(session.editObject(0, {}));
    CHECK_FALSE(session.editObject(99, {}));
    CHECK_FALSE(session.resetObject(99));
    CHECK_FALSE(session.resetLight(99));
    CHECK_FALSE(session.editLight(99, {}));
    CHECK_FALSE(session.objectTransformPersistable(99));
    CHECK(session.objectPoseLock(99) != PoseLock::None);
    CHECK(session.lightPoseLock(lmx::app::EditorSubject::LocalLight, 0, {}) != PoseLock::None);
    CHECK(session.editGeneration() == 0);
}

//======================================================================================================================
TEST_CASE("SceneSession light pose locks follow authored binding order and preserve non-pose edits",
          "[app][scene-session][ux6-pose-lock]") {
    using lmx::app::EditorSubject;
    using lmx::app::PoseLock;
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.document.nodes = {{.name = "Movable point", .light = 0},
                             {.name = "Static rim", .light = 1},
                             {.name = "Static spot", .light = 2},
                             {.name = "Movable key", .light = 3},
                             {.name = "Generator"}};
    loaded.document.nodes[0].mobility = asset::DocMobility::Movable;
    loaded.document.nodes[3].mobility = asset::DocMobility::Movable;
    loaded.binding.nodes.resize(5);
    loaded.document.rootNodes = {0, 1, 2, 3, 4};
    loaded.lightMobility = {asset::DocMobility::Movable, asset::DocMobility::Static,
                            asset::DocMobility::Static, asset::DocMobility::Movable};
    const auto point = loaded.scene->addLight({});
    const auto spot = loaded.scene->addLight({.type = engine::LocalLightType::Spot,
                                              .position = {-0.0f, 2, 3},
                                              .direction = {-0.0f, -2, -3},
                                              .innerCone = 0.1f,
                                              .outerCone = 0.7f});
    const auto generated = loaded.scene->addLight({});
    REQUIRE(point);
    REQUIRE(spot);
    REQUIRE(generated);
    loaded.binding.nodes[0].light = *point;
    loaded.binding.nodes[1].directional = 2;
    loaded.binding.nodes[2].light = *spot;
    loaded.binding.nodes[3].directional = 0;
    loaded.binding.lightNode[engine::sceneLightKey(*point)] = 0;
    loaded.binding.lightNode[engine::sceneLightKey(*spot)] = 2;
    loaded.binding.lightGeneratorNode[engine::sceneLightKey(*generated)] = 4;
    loaded.scene->lights[2].direction = {-0.0f, -2, -3};
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
    CHECK(session.lightPoseLock(EditorSubject::LocalLight, 999, *point) == PoseLock::None);
    CHECK(session.lightPoseLock(EditorSubject::LocalLight, 0, *spot) == PoseLock::Static);
    CHECK(session.lightPoseLock(EditorSubject::DirectionalLight, 2, {}) == PoseLock::Static);
    CHECK(session.lightPoseLock(EditorSubject::DirectionalLight, 0, {}) == PoseLock::None);
    CHECK(session.lightPoseLock(EditorSubject::LocalLight, 0, *generated) == PoseLock::None);
    const auto originalSpot = *loaded.scene->light(*spot);
    const auto originalRim = loaded.scene->lights[2];
    const auto generation = session.editGeneration();
    for (bool position : {false, true}) {
        auto changed = originalSpot;
        (position ? changed.position : changed.direction).x = 4;
        changed.intensity += 10;
        const auto refused = session.editLocalLight(*spot, changed);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().message == lmx::app::poseLockReason(PoseLock::Static));
        CHECK(loaded.scene->light(*spot)->position == originalSpot.position);
        CHECK(loaded.scene->light(*spot)->direction == originalSpot.direction);
        CHECK(loaded.scene->light(*spot)->intensity == originalSpot.intensity);
        CHECK(session.editGeneration() == generation);
    }
    auto changedRim = originalRim;
    changedRim.direction.x = 4;
    changedRim.strength *= 2;
    REQUIRE_FALSE(session.editLight(2, changedRim));
    CHECK(loaded.scene->lights[2].strength == originalRim.strength);
    CHECK(session.editGeneration() == generation);
    const auto sameBits = [](glm::vec3 a, glm::vec3 b) {
        for (int i = 0; i < 3; ++i)
            CHECK(std::bit_cast<uint32_t>(a[i]) == std::bit_cast<uint32_t>(b[i]));
    };
    for (int edit = 0; edit < 3; ++edit) {
        auto changed = *loaded.scene->light(*spot);
        changed.position.x = 0.0f;
        changed.direction.x = 0.0f;
        changed.intensity += 1;
        changed.colour = {0.25f, 0.5f, 0.75f};
        changed.range += 1;
        changed.innerCone = 0.1f;
        changed.outerCone = 0.7f;
        changed.enabled = !session.localLightEnabled(*spot);
        REQUIRE(session.editLocalLight(*spot, changed));
        sameBits(loaded.scene->light(*spot)->position, originalSpot.position);
        sameBits(loaded.scene->light(*spot)->direction, originalSpot.direction);
        CHECK(loaded.scene->light(*spot)->intensity == changed.intensity);
        CHECK(loaded.scene->light(*spot)->colour == changed.colour);
        CHECK(loaded.scene->light(*spot)->range == changed.range);
        CHECK(loaded.scene->light(*spot)->innerCone == changed.innerCone);
        CHECK(loaded.scene->light(*spot)->outerCone == changed.outerCone);
        CHECK(session.localLightEnabled(*spot) == changed.enabled);
        auto rim = loaded.scene->lights[2];
        rim.direction.x = 0.0f;
        rim.strength += glm::vec3(1);
        rim.enabled = !rim.enabled;
        REQUIRE(session.editLight(2, rim));
        sameBits(loaded.scene->lights[2].direction, originalRim.direction);
        CHECK(loaded.scene->lights[2].strength == rim.strength);
        CHECK(loaded.scene->lights[2].enabled == rim.enabled);
    }
    REQUIRE(session.setLocalLightEnabled(*spot, false));
    sameBits(loaded.scene->light(*spot)->direction, originalSpot.direction);
    auto moved = *loaded.scene->light(*point);
    moved.position.x = 6;
    REQUIRE(session.editLocalLight(*point, moved));
    auto key = loaded.scene->lights[0];
    key.direction = {1, 0, 0};
    REQUIRE(session.editLight(0, key));
    loaded.scene->animation.lightTracks = {{.light = 0}, {.light = 2}};
    CHECK(session.lightPoseLock(EditorSubject::LocalLight, 0, *point) == PoseLock::Animated);
    CHECK(session.lightPoseLock(EditorSubject::LocalLight, 0, *generated) == PoseLock::None);
    moved.position.x = 7;
    const auto beforeAnimated = session.editGeneration();
    const auto refusedAnimated = session.editLocalLight(*point, moved);
    REQUIRE_FALSE(refusedAnimated);
    CHECK(refusedAnimated.error().message == lmx::app::poseLockReason(PoseLock::Animated));
    CHECK(loaded.scene->light(*point)->position.x == 6);
    CHECK(session.editGeneration() == beforeAnimated);
    auto generatedEdit = *loaded.scene->light(*generated);
    generatedEdit.position.x = 8;
    REQUIRE(session.editLocalLight(*generated, generatedEdit));
    CHECK(session.editGeneration() == beforeAnimated);
    session.setMeasurementActive(true);
    for (const auto id : {*point, *spot, *generated}) {
        CHECK(session.lightPoseLock(EditorSubject::LocalLight, 0, id) == PoseLock::Measuring);
        auto changed = *loaded.scene->light(id);
        changed.position.x += 1;
        const auto refused = session.editLocalLight(id, changed);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().message == lmx::app::poseLockReason(PoseLock::Measuring));
    }
    CHECK(session.lightPoseLock(EditorSubject::DirectionalLight, 0, {}) == PoseLock::Measuring);
    key.direction = {0, 1, 0};
    const auto refused = session.editLight(0, key);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message == lmx::app::poseLockReason(PoseLock::Measuring));
    CHECK(session.editGeneration() == beforeAnimated);
    auto measuring = *loaded.scene->light(*spot);
    measuring.intensity += 1;
    REQUIRE(session.editLocalLight(*spot, measuring));
    sameBits(loaded.scene->light(*spot)->position, originalSpot.position);
    sameBits(loaded.scene->light(*spot)->direction, originalSpot.direction);
    CHECK(session.editGeneration() == beforeAnimated + 1);
}
