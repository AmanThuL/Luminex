//----------------------------------------------------------------------------------------------------------------------
/// @file AppSceneEnabledTests.cpp
/// @brief Tests authored flags, inherited masks, bound edits and measurement edit refusal.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Rendering/Temporal/TemporalEditorState.h"
#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Scene/SceneSession.h"
#include "Support/GraphTestSupport.h"
#include "Support/SceneDocumentTestSupport.h"
#include <catch2/catch_test_macros.hpp>

using namespace lmx;
using namespace lmx::app;
namespace {
//======================================================================================================================
engine::LoadedScene enabledFixture() {
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.document.rootNodes = {0};
    loaded.document.nodes = {{.name = "root", .children = {1, 2, 3}},
                             {.name = "asset"},
                             {.name = "lab", .enabled = false},
                             {.name = "other asset"}};
    loaded.binding.nodes.resize(4);
    loaded.binding.importedNodes = {
        {.assetRoot = 1, .sourceNode = 0, .name = "empty", .objects = {}},
        {.assetRoot = 1, .sourceNode = 1, .parent = 0, .name = "mesh", .objects = {0, 1}},
        {.assetRoot = 3, .sourceNode = 1, .name = "other mesh", .objects = {2}}};
    loaded.binding.objectNode = {1, 1, 3, engine::kGeneratedNode, engine::kGeneratedNode};
    loaded.binding.objectImportedNode = {1, 1, 2, engine::kGeneratedNode, engine::kGeneratedNode};
    loaded.binding.objectGeneratorNode = {engine::kGeneratedNode, engine::kGeneratedNode,
                                          engine::kGeneratedNode, 2, 2};
    loaded.binding.generatedObjectEnabled = {true, true, true, true, false};
    loaded.scene->objects.resize(5);
    loaded.scene->objects[3].enabled = false;
    loaded.scene->objects[4].enabled = false;
    const auto id = loaded.scene->addLight(engine::LocalLight{});
    REQUIRE(id);
    loaded.binding.lightGeneratorNode[engine::sceneLightKey(*id)] = 2;
    loaded.binding.generatedLightEnabled[engine::sceneLightKey(*id)] = true;
    auto light = *loaded.scene->light(*id);
    light.enabled = false;
    REQUIRE(loaded.scene->updateLight(*id, light));
    loaded.scene->lightLabPopulations.push_back({.documentNode = 2, .grid = {*id}});
    return loaded;
}
} // namespace

//======================================================================================================================
TEST_CASE("Session group and imported toggles preserve own flags and source identity",
          "[app][scene-enabled]") {
    auto loaded = enabledFixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    REQUIRE(session.setObjectEnabled(0, false));
    CHECK_FALSE(loaded.scene->objects[0].enabled);
    CHECK_FALSE(loaded.scene->objects[1].enabled);
    CHECK(loaded.scene->objects[2].enabled);
    REQUIRE(session.setNodeEnabled(0, false));
    CHECK(session.nodeEnabled(1));
    CHECK_FALSE(session.objectEnabled(0));
    REQUIRE(session.setNodeEnabled(0, true));
    CHECK_FALSE(loaded.scene->objects[0].enabled);
    CHECK(loaded.scene->objects[2].enabled);
    REQUIRE(session.setObjectEnabled(1, true));
    REQUIRE(session.setImportedNodeEnabled(0, false));
    CHECK(session.objectEnabled(0));
    CHECK_FALSE(loaded.scene->objects[0].enabled);
    REQUIRE(session.setImportedNodeEnabled(0, true));
    CHECK(loaded.scene->objects[0].enabled);
    CHECK(loaded.scene->objects[1].enabled);
    CHECK(session.editGeneration() == 6);
    REQUIRE(session.setImportedNodeEnabled(0, true));
    CHECK(session.editGeneration() == 6);
    TemporalEditorState temporal;
    syncSessionTemporalReset(temporal, session);
    CHECK(consumeCameraCut(temporal));
    syncSessionTemporalReset(temporal, session);
    CHECK_FALSE(consumeCameraCut(temporal));
}

//======================================================================================================================
TEST_CASE("Generated own flags survive masked activation and new pile identities",
          "[app][scene-enabled]") {
    auto loaded = enabledFixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    const auto grid = loaded.scene->localLights().front();
    CHECK(session.objectEnabled(3));
    CHECK_FALSE(session.objectEnabled(4));
    CHECK(session.localLightEnabled(grid));
    CHECK(session.isGenerated(EditorSubject::Object, 3, {}));
    CHECK_FALSE(session.isGenerated(EditorSubject::Object, 0, {}));
    REQUIRE(session.setLightLabPile(2));
    const auto pile = loaded.scene->lightLabPopulations.front().pile;
    REQUIRE(pile.size() == 2);
    CHECK(session.isGenerated(EditorSubject::LocalLight, 0, pile[0]));
    CHECK(session.localLightEnabled(pile[0]));
    CHECK_FALSE(loaded.scene->light(pile[0])->enabled);
    REQUIRE(session.setObjectEnabled(3, false));
    REQUIRE(session.setLocalLightEnabled(grid, false));
    REQUIRE(session.setLocalLightEnabled(pile[0], false));
    CHECK(session.editGeneration() == 0);
    REQUIRE(session.setNodeEnabled(2, true));
    CHECK_FALSE(loaded.scene->objects[3].enabled);
    CHECK_FALSE(loaded.scene->objects[4].enabled);
    CHECK_FALSE(loaded.scene->light(grid)->enabled);
    CHECK_FALSE(loaded.scene->light(pile[0])->enabled);
    CHECK(loaded.scene->light(pile[1])->enabled);
    CHECK(session.editGeneration() == 1);
    auto edited = *loaded.scene->light(pile[1]);
    edited.intensity = 9;
    REQUIRE(session.editLocalLight(pile[1], edited));
    session.editObject(3, {.position = {5, 0, 0}});
    CHECK(session.editGeneration() == 1);
    CHECK(loaded.document.nodes[2].enabled == false);
}

//======================================================================================================================
TEST_CASE("CLI group mask ends on a persisted group edit without rewriting child flags",
          "[app][scene-enabled]") {
    auto loaded = test::documentLightFixture();
    const auto group = *loaded.binding.localLightGroup;
    loaded.document.nodes[group].enabled = false;
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    REQUIRE(session.setLocalLightRig(true));
    CHECK(session.editGeneration() == 0);
    CHECK(loaded.scene->enabledLightCount() == 16);
    CHECK_FALSE(session.nodeEnabled(group));
    REQUIRE(session.setNodeEnabled(group, false));
    CHECK(loaded.scene->enabledLightCount() == 0);
    CHECK(session.editGeneration() == 1);
    REQUIRE(session.setNodeEnabled(group, true));
    CHECK(loaded.scene->enabledLightCount() == 16);
    CHECK(session.editGeneration() == 2);
}

//======================================================================================================================
TEST_CASE("Bound pose edits fan out and persistent mutations notify exactly on change",
          "[app][scene-enabled]") {
    auto loaded = enabledFixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    const DecomposedTransform pose{.position = {8, 2, 1}};
    session.editObject(0, pose);
    CHECK(loaded.scene->objects[1].position == pose.position);
    CHECK(loaded.scene->objects[2].position != pose.position);
    CHECK(session.editGeneration() == 1);
    session.editObject(1, pose);
    CHECK(session.editGeneration() == 1);
    session.resetObject(1);
    CHECK(loaded.scene->objects[0].position == glm::vec3(0));
    CHECK(session.editGeneration() == 2);
    loaded.binding.importedNodes[1].animated = true;
    session.editObject(0, pose);
    CHECK(session.editGeneration() == 2);
}

//======================================================================================================================
TEST_CASE("Measurement freezes session enablement through every toggle path",
          "[app][scene-enabled]") {
    auto loaded = test::documentLightFixture();
    const auto id = loaded.scene->localLights().front();
    const auto group = *loaded.binding.localLightGroup;
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    session.setMeasurementActive(true);
    CHECK_FALSE(session.setNodeEnabled(group, false));
    CHECK_FALSE(session.setLocalLightEnabled(id, false));
    CHECK_FALSE(session.setLocalLightRig(false));
    auto light = *loaded.scene->light(id);
    light.enabled = false;
    CHECK_FALSE(session.editLocalLight(id, light));
    CHECK(loaded.scene->light(id)->enabled);
    CHECK(session.editGeneration() == 0);
    CHECK_FALSE(session.consumeTemporalReset());
    session.setMeasurementActive(false);
    REQUIRE(session.setNodeEnabled(group, false));
    REQUIRE(session.setLocalLightEnabled(id, false));
    CHECK(session.editGeneration() == 2);
}

//======================================================================================================================
TEST_CASE("Visibility status distinguishes authored off from view rejection",
          "[app][scene-enabled]") {
    CHECK(visibilityStatusLabel(render::VisibilityState::Rejected,
                                render::VisibilityReason::AuthoredOff) == "Disabled");
    CHECK(visibilityStatusLabel(render::VisibilityState::Rejected,
                                render::VisibilityReason::Occluded) == "Culled: occluded");
    CHECK(visibilityStatusLabel(render::VisibilityState::Rejected,
                                render::VisibilityReason::None) == "Culled: frustum");
    CHECK(visibilityStatusLabel(render::VisibilityState::Visible,
                                render::VisibilityReason::CullingOff) == "Visible");
}

//======================================================================================================================
TEST_CASE("Persistent light edits and resets preserve masked own flags and notify dirty",
          "[app][scene-enabled]") {
    auto loaded = test::documentLightFixture();
    const auto id = loaded.scene->localLights().front();
    const auto group = *loaded.binding.localLightGroup;
    loaded.binding.nodes[0].directional = 0;
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    REQUIRE(session.setNodeEnabled(group, false));
    auto light = *loaded.scene->light(id);
    light.enabled = session.localLightEnabled(id);
    light.intensity += 3;
    REQUIRE(session.editLocalLight(id, light));
    CHECK(session.localLightEnabled(id));
    CHECK_FALSE(loaded.scene->light(id)->enabled);
    CHECK(session.editGeneration() == 2);
    REQUIRE(session.resetLocalLight(id));
    CHECK_FALSE(loaded.scene->light(id)->enabled);
    CHECK_FALSE(session.localLightChanged(id));
    CHECK(session.editGeneration() == 3);
    REQUIRE(session.resetLocalLight(id));
    CHECK(session.editGeneration() == 3);
    auto directional = loaded.scene->lights[0];
    directional.strength = {2, 3, 4};
    REQUIRE(session.editLight(0, directional));
    CHECK(session.editGeneration() == 4);
    REQUIRE(session.resetLight(0));
    CHECK(session.editGeneration() == 5);
    REQUIRE(session.resetLight(0));
    CHECK(session.editGeneration() == 5);
    REQUIRE(session.setNodeEnabled(0, false));
    session.setMeasurementActive(true);
    CHECK_FALSE(session.resetLight(0));
    CHECK_FALSE(loaded.scene->lights[0].enabled);
    CHECK(session.editGeneration() == 6);
}

//======================================================================================================================
TEST_CASE("Saved reset adoption preserves generation and generated session defaults",
          "[app][scene-enabled]") {
    auto loaded = enabledFixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    session.editObject(0, {.position = {4, 5, 6}});
    session.editObject(3, {.position = {9, 9, 9}});
    session.adoptDocumentResetBaseline();
    CHECK(session.editGeneration() == 1);
    CHECK_FALSE(session.objectChanged(0));
    CHECK(session.objectChanged(3));
    session.editObject(0, {.position = {1, 1, 1}});
    session.resetObject(1);
    CHECK(loaded.scene->objects[0].position == glm::vec3(4, 5, 6));
    CHECK(loaded.scene->objects[1].position == glm::vec3(4, 5, 6));
    CHECK(session.editGeneration() == 3);
}

//======================================================================================================================
TEST_CASE("Measurement refuses imported and generated edits without notification",
          "[app][scene-enabled]") {
    auto loaded = enabledFixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    session.setMeasurementActive(true);
    CHECK_FALSE(session.setImportedNodeEnabled(0, false));
    CHECK_FALSE(session.setObjectEnabled(0, false));
    CHECK_FALSE(session.setObjectEnabled(3, false));
    CHECK_FALSE(session.setLightLabPile(2));
    CHECK_FALSE(session.consumeTemporalReset());
    CHECK(session.editGeneration() == 0);
    CHECK(session.objectEnabled(0));
    CHECK(session.objectEnabled(3));
    session.setMeasurementActive(false);
    CHECK_FALSE(session.setImportedNodeEnabled(99, false));
    CHECK_FALSE(session.setObjectEnabled(99, false));
    CHECK_FALSE(session.setNodeEnabled(99, false));
    CHECK_FALSE(session.setLocalLightEnabled({}, false));
}

//======================================================================================================================
TEST_CASE("Instantiation captures generated own flags before an off ancestor masks them",
          "[app][scene-enabled]") {
    auto document = scenes::readCatalogDocument("light-lab");
    REQUIRE(document);
    document->nodes[0].enabled = false;
    const auto path =
        std::filesystem::current_path() / "SceneDocuments" / "generated-own-flags.scene.gltf";
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(asset::saveSceneDocument(*document, path));
    FakeDevice device;
    const engine::SceneGenerator generator =
        [](engine::Scene& scene, const asset::DocGenerator&,
           const engine::EnvironmentHook&) -> asset::AssetResult<void> {
        const auto mesh = scene.addMesh(engine::makeCube(), "lmx.test.enabled.cube");
        const auto material = scene.addMaterial({});
        for (bool enabled : {true, false}) {
            const auto index = scene.objects.size();
            scene.addObject({.mesh = mesh, .material = material});
            scene.setObjectEnabled(index, enabled);
            engine::LocalLight light;
            light.enabled = enabled;
            REQUIRE(scene.addLight(light));
        }
        return {};
    };
    auto loaded = engine::instantiateSceneDocument(device, *document, path,
                                                   [&](std::string_view) { return &generator; });
    REQUIRE(loaded);
    REQUIRE(loaded->scene->objects.size() == 2);
    REQUIRE_FALSE(loaded->scene->objects[0].enabled);
    REQUIRE_FALSE(loaded->scene->objects[1].enabled);
    REQUIRE(loaded->scene->enabledLightCount() == 0);
    SceneSession session;
    session.activate(*loaded, SceneActivationMotion::Reset);
    CHECK(session.objectEnabled(0));
    CHECK_FALSE(session.objectEnabled(1));
    REQUIRE(session.setNodeEnabled(0, true));
    CHECK(loaded->scene->objects[0].enabled);
    CHECK_FALSE(loaded->scene->objects[1].enabled);
    CHECK(loaded->scene->enabledLightCount() == 1);
}
