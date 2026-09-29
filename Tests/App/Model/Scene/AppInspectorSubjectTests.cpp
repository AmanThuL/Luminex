#include <catch2/catch_test_macros.hpp>

#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"
#include "App/Model/Scene/InspectorSubject.h"

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
