#include <catch2/catch_test_macros.hpp>

#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"
#include "App/Model/Scene/InspectorSubject.h"
#include "Scenes/SceneLibrary.h"

using namespace lmx;
using namespace lmx::app;

namespace {

//======================================================================================================================
engine::LoadedScene inspectorFixture() {
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.document.rootNodes = {0};
    loaded.document.nodes = {
        {.name = "Root", .children = {1, 2, 3, 4}},
        {.name = "Asset"},
        {.name = "Lamp", .light = 0},
        {.name = "Lab", .generator = asset::DocGenerator{.name = "VisibilityLab"}},
        {.name = "Key", .light = 1}};
    loaded.binding.nodes.resize(5);
    loaded.binding.importedNodes = {
        {.assetRoot = 1, .sourceNode = 0, .name = "Empty"},
        {.assetRoot = 1, .sourceNode = 1, .parent = 0, .name = "Column", .objects = {0, 1}}};
    loaded.binding.objectNode = {1, 1, engine::kGeneratedNode};
    loaded.binding.objectImportedNode = {1, 1, engine::kGeneratedNode};
    loaded.binding.objectGeneratorNode = {engine::kGeneratedNode, engine::kGeneratedNode, 3};
    loaded.binding.generatedObjectEnabled = {true, true, true};
    loaded.scene->objects.resize(3);
    loaded.scene->objects[0].name = "Column stone";
    loaded.scene->objects[1].name = "Column trim";
    loaded.scene->objects[2].name = "Generated cube";
    const auto light = loaded.scene->addLight(engine::LocalLight{});
    REQUIRE(light);
    loaded.binding.nodes[2].light = *light;
    loaded.binding.lightNode[engine::sceneLightKey(*light)] = 2;
    loaded.binding.nodes[4].directional = 0;
    return loaded;
}

} // namespace

//======================================================================================================================
TEST_CASE("Inspector enabled routing preserves group, source and generated ownership",
          "[app][inspector-subject]") {
    auto loaded = inspectorFixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    const EditorSelection root{.sceneId = {"fixture"}, .subject = EditorSubject::Group, .node = 0};
    const EditorSelection emptySource{
        .sceneId = {"fixture"}, .subject = EditorSubject::Group, .node = 1, .importedNode = 0};
    const EditorSelection column{.sceneId = {"fixture"},
                                 .subject = EditorSubject::Object,
                                 .index = 0,
                                 .node = 1,
                                 .importedNode = 1};
    const EditorSelection generated{
        .sceneId = {"fixture"}, .subject = EditorSubject::Object, .index = 2, .node = 3};
    REQUIRE(inspectorEnabledState(session, root)->own);
    REQUIRE(setInspectorEnabled(session, root, false));
    CHECK_FALSE(loaded.scene->objects[0].enabled);
    CHECK_FALSE(loaded.scene->light(loaded.scene->localLights().front())->enabled);
    CHECK(session.objectEnabled(0));
    REQUIRE(setInspectorEnabled(session, root, true));
    REQUIRE(setInspectorEnabled(session, emptySource, false));
    CHECK_FALSE(loaded.scene->objects[0].enabled);
    CHECK(inspectorEnabledState(session, column)->own);
    CHECK_FALSE(inspectorEnabledState(session, column)->effective);
    REQUIRE(resetInspectorEnabled(session, emptySource));
    REQUIRE(setInspectorEnabled(session, column, false));
    CHECK_FALSE(loaded.scene->objects[0].enabled);
    CHECK_FALSE(loaded.scene->objects[1].enabled);
    REQUIRE(resetInspectorEnabled(session, column));
    CHECK(loaded.scene->objects[0].enabled);
    CHECK(loaded.scene->objects[1].enabled);
    const auto generation = session.editGeneration();
    REQUIRE(setInspectorEnabled(session, generated, false));
    CHECK_FALSE(inspectorEnabledState(session, generated)->own);
    CHECK(inspectorEnabledState(session, generated)->generatedBy == "VisibilityLab");
    CHECK(session.editGeneration() == generation);
    REQUIRE(resetInspectorEnabled(session, generated));
    CHECK(session.objectEnabled(2));
}

//======================================================================================================================
TEST_CASE("Inspector light headers route own flags and restore their authored values",
          "[app][inspector-subject]") {
    auto loaded = inspectorFixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    const auto id = loaded.scene->localLights().front();
    const EditorSelection local{
        .sceneId = {"fixture"}, .subject = EditorSubject::LocalLight, .lightId = id, .node = 2};
    const EditorSelection directional{
        .sceneId = {"fixture"}, .subject = EditorSubject::DirectionalLight, .index = 0, .node = 4};
    REQUIRE(setInspectorEnabled(session, local, false));
    REQUIRE(setInspectorEnabled(session, directional, false));
    CHECK_FALSE(inspectorEnabledState(session, local)->own);
    CHECK_FALSE(inspectorEnabledState(session, directional)->own);
    REQUIRE(resetInspectorEnabled(session, local));
    REQUIRE(resetInspectorEnabled(session, directional));
    CHECK(session.localLightEnabled(id));
    CHECK(session.nodeEnabled(4));
}

//======================================================================================================================
TEST_CASE("Inspector Reset follows the adopted document for imported source nodes",
          "[app][inspector-subject]") {
    for (const bool group : {false, true}) {
        auto loaded = inspectorFixture();
        SceneSession session;
        session.activate(loaded, SceneActivationMotion::Reset);
        const EditorSelection selection = group ? EditorSelection{.sceneId = {"fixture"},
                                                                  .subject = EditorSubject::Group,
                                                                  .node = 1,
                                                                  .importedNode = 0}
                                                : EditorSelection{.sceneId = {"fixture"},
                                                                  .subject = EditorSubject::Object,
                                                                  .index = 0,
                                                                  .node = 1,
                                                                  .importedNode = 1};
        const auto sourceIndex = selection.importedNode;
        const auto& source = loaded.binding.importedNodes[sourceIndex];
        CAPTURE(group, sourceIndex);

        // With no document override, the source's original own flag is the reset fallback.
        REQUIRE(inspectorEnabledState(session, selection)->baseline == source.enabled);
        REQUIRE(setInspectorEnabled(session, selection, false));
        loaded.document.nodes[source.assetRoot].overrides.push_back(
            {.node = source.sourceNode, .name = source.name, .enabled = false});
        session.adoptDocumentResetBaseline();

        const auto saved = inspectorEnabledState(session, selection);
        REQUIRE(saved);
        CHECK_FALSE(saved->own);
        CHECK_FALSE(saved->baseline);
        REQUIRE(setInspectorEnabled(session, selection, true));
        const auto changed = inspectorEnabledState(session, selection);
        REQUIRE(changed);
        CHECK(changed->own);
        CHECK_FALSE(changed->baseline);
        REQUIRE(resetInspectorEnabled(session, selection));
        CHECK_FALSE(inspectorEnabledState(session, selection)->own);
        CHECK_FALSE(loaded.scene->objects[0].enabled);
        CHECK_FALSE(loaded.scene->objects[1].enabled);
    }
}

//======================================================================================================================
TEST_CASE("Environment reset restores the complete saved look only", "[app][inspector-subject]") {
    auto loaded = inspectorFixture();
    loaded.document.look.exposure.ev = 2.0f;
    loaded.document.look.bloom.enabled = false;
    loaded.document.look.shadowFilter = asset::ShadowFilter::PCSS;
    loaded.scene->look = loaded.document.look;
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    EditorRenderSettings settings;
    settings.renderScale = 0.6f;
    auto edited = session.look();
    edited.exposure.ev = -1.0f;
    edited.bloom.enabled = true;
    edited.shadowFilter = asset::ShadowFilter::PCF;
    session.editLook(edited);
    REQUIRE(sceneLookChanged(session));
    resetSceneLook(session);
    CHECK(session.look() == loaded.document.look);
    CHECK_FALSE(sceneLookChanged(session));
    CHECK(settings.renderScale == 0.6f);
}

//======================================================================================================================
TEST_CASE("Saved mesh Enabled resets to its loaded false own flag",
          "[app][inspector-subject][mobility-display]") {
    auto loaded = inspectorFixture();
    loaded.document.nodes.push_back({.name = "Saved cube", .mesh = 0, .enabled = false});
    loaded.document.nodes[0].children.push_back(5);
    loaded.binding.nodes.resize(6);
    loaded.binding.nodes[5].objects = {3};
    loaded.binding.objectNode.push_back(5);
    loaded.binding.objectImportedNode.push_back(engine::kGeneratedNode);
    loaded.binding.objectGeneratorNode.push_back(engine::kGeneratedNode);
    loaded.binding.generatedObjectEnabled.push_back(true);
    loaded.scene->objects.emplace_back();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    const EditorSelection cube{
        .sceneId = {"fixture"}, .subject = EditorSubject::Object, .index = 3, .node = 5};
    REQUIRE(inspectorEnabledState(session, cube));
    CHECK(inspectorIsStatic(session, cube) == true);
    CHECK_FALSE(inspectorSubjectEdited(session, cube));
    CHECK_FALSE(inspectorEnabledState(session, cube)->baseline);
    CHECK_FALSE(inspectorEnabledState(session, cube)->own);
    REQUIRE(setInspectorEnabled(session, cube, true));
    CHECK(inspectorSubjectEdited(session, cube));
    REQUIRE(resetInspectorEnabled(session, cube));
    CHECK_FALSE(inspectorEnabledState(session, cube)->own);
    CHECK_FALSE(inspectorSubjectEdited(session, cube));
}

//======================================================================================================================
TEST_CASE("Inspector Static reports authored mobility independently of pose locks",
          "[app][inspector-subject][mobility-display]") {
    auto loaded = inspectorFixture();
    loaded.objectMobility = {asset::DocMobility::Movable, asset::DocMobility::Movable,
                             asset::DocMobility::Static};
    // Binding order is local node 2 then directional node 4, not scene-light order.
    loaded.lightMobility = {asset::DocMobility::Static, asset::DocMobility::Movable};
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    const EditorSelection object{.subject = EditorSubject::Object, .index = 0};
    const EditorSelection generated{.subject = EditorSubject::Object, .index = 2};
    const EditorSelection local{.subject = EditorSubject::LocalLight,
                                .lightId = loaded.scene->localLights().front()};
    const EditorSelection directional{.subject = EditorSubject::DirectionalLight, .index = 0};
    CHECK(inspectorIsStatic(session, object) == false);
    CHECK(inspectorIsStatic(session, local) == true);
    CHECK(inspectorIsStatic(session, directional) == false);
    CHECK_FALSE(inspectorIsStatic(session, generated).has_value());
    CHECK_FALSE(inspectorIsStatic(session, {.subject = EditorSubject::Group, .node = 0}));
    session.setMeasurementActive(true);
    CHECK(session.objectPoseLock(0) == PoseLock::Measuring);
    CHECK(inspectorIsStatic(session, object) == false);
    session.setMeasurementActive(false);
    // Animated source nodes take no mobility, so the Inspector shows no Static value for them.
    loaded.binding.importedNodes[1].animated = true;
    CHECK_FALSE(inspectorIsStatic(session, object).has_value());
    CHECK(session.objectPoseLock(0) == PoseLock::Animated);
    const auto id = loaded.scene->addLight(engine::LocalLight{});
    REQUIRE(id);
    loaded.binding.lightGeneratorNode[engine::sceneLightKey(*id)] = 3;
    CHECK_FALSE(inspectorIsStatic(session, {.subject = EditorSubject::LocalLight, .lightId = *id}));
}

//======================================================================================================================
TEST_CASE("Inherited light disablement does not mark saved light fields edited",
          "[app][inspector-subject][mobility-display]") {
    auto loaded = inspectorFixture();
    loaded.document.nodes[0].enabled = false;
    loaded.scene->lights[0].enabled = false;
    const auto id = loaded.scene->localLights().front();
    auto light = *loaded.scene->light(id);
    light.enabled = false;
    REQUIRE(loaded.scene->updateLight(id, light));
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    const EditorSelection directional{.subject = EditorSubject::DirectionalLight, .index = 0};
    const EditorSelection local{.subject = EditorSubject::LocalLight, .lightId = id};
    CHECK_FALSE(inspectorSubjectEdited(session, directional));
    CHECK_FALSE(inspectorSubjectEdited(session, local));
    REQUIRE(session.setNodeEnabled(0, true));
    CHECK_FALSE(inspectorSubjectEdited(session, directional));
    CHECK_FALSE(inspectorSubjectEdited(session, local));
    REQUIRE(setInspectorEnabled(session, directional, false));
    REQUIRE(setInspectorEnabled(session, local, false));
    CHECK(inspectorSubjectEdited(session, directional));
    CHECK(inspectorSubjectEdited(session, local));
}

//======================================================================================================================
TEST_CASE("Stopped TemporalLab animated source rows carry no edit mark or mobility",
          "[gpu][app][inspector-subject][mobility-display]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    scenes::SceneLibrary library(**device);
    const auto id = scenes::parseSceneId("temporal-lab");
    REQUIRE(id);
    REQUIRE(library.get(*id));
    SceneSession session;
    session.activate(*library.loaded(*id), SceneActivationMotion::Reset);
    const auto& binding = session.loadedScene()->binding;
    size_t animated = 0;
    for (size_t i = 0; i < binding.importedNodes.size(); ++i) {
        const auto& source = binding.importedNodes[i];
        if (!source.animated)
            continue;
        for (const auto object : source.objects) {
            CAPTURE(source.name, object);
            const EditorSelection selection{.sceneId = *id,
                                            .subject = EditorSubject::Object,
                                            .index = static_cast<uint32_t>(object),
                                            .node = source.assetRoot,
                                            .importedNode = static_cast<uint32_t>(i)};
            CHECK(session.objectPoseLock(object) == PoseLock::Animated);
            CHECK_FALSE(session.objectChanged(object));
            CHECK_FALSE(inspectorSubjectEdited(session, selection));
            CHECK_FALSE(inspectorIsStatic(session, selection));
            ++animated;
        }
    }
    CHECK(animated >= 2);
    (*device)->waitIdle();
}
