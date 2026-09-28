#include <catch2/catch_test_macros.hpp>

#include "App/Model/Scene/SceneTree.h"

#include <algorithm>
#include <set>

using namespace lmx;
using namespace lmx::app;

namespace {

//======================================================================================================================
engine::LoadedScene treeFixture() {
    engine::LoadedScene loaded;
    loaded.scene = std::make_unique<engine::Scene>();
    loaded.document.name = "Fixture";
    loaded.document.rootNodes = {0, 1};
    loaded.document.nodes.resize(5);
    loaded.document.nodes[0].name = "Camera";
    loaded.document.nodes[0].camera = 0;
    loaded.document.nodes[1].name = "Lab";
    loaded.document.nodes[1].children = {2, 3, 4};
    loaded.document.nodes[2].name = "Generated";
    loaded.document.nodes[2].generator = asset::DocGenerator{.name = "VisibilityLab"};
    loaded.document.nodes[3].name = "Asset";
    loaded.document.nodes[3].asset = asset::DocAsset{.uri = "fixture.gltf", .sha256 = "hash"};
    loaded.document.nodes[4].name = "Off light";
    loaded.document.nodes[4].light = 0;
    loaded.document.lights.push_back({});
    loaded.binding.nodes.resize(5);
    loaded.binding.nodes[0].camera = true;
    loaded.binding.nodes[2].objects = {0};
    loaded.binding.nodes[3].objects = {1, 2};
    loaded.binding.importedNodes = {
        {.assetRoot = 3, .sourceNode = 0, .name = "Empty"},
        {.assetRoot = 3, .sourceNode = 1, .parent = 0, .name = "Pillar", .objects = {1, 2}}};
    loaded.binding.objectNode = {engine::kGeneratedNode, 3, 3};
    loaded.binding.objectImportedNode = {engine::kGeneratedNode, 1, 1};
    loaded.binding.objectGeneratorNode = {2, engine::kGeneratedNode, engine::kGeneratedNode};
    loaded.binding.generatedObjectEnabled = {true, true, true};
    loaded.scene->objects.resize(3);
    loaded.scene->objects[0].name = "Generated cube";
    loaded.scene->objects[1].name = "Pillar stone";
    loaded.scene->objects[2].name = "Pillar trim";
    loaded.binding.nodes[4].directional = 0;
    loaded.scene->lights[0].enabled = false;
    return loaded;
}

} // namespace

//======================================================================================================================
TEST_CASE("Scene tree preserves document order and imported source-node fanout",
          "[app][scene-tree]") {
    auto loaded = treeFixture();
    auto state = scenes::initialDocumentState(loaded);
    const auto rows = buildSceneTree(loaded, state, "", {});
    REQUIRE(rows.front().label == "Fixture");
    REQUIRE(rows[1].subject == EditorSubject::Camera);
    REQUIRE(rows[2].label == "Lab");
    REQUIRE(rows[3].label == "Generated");
    REQUIRE(rows[4].label == "Generated cube");
    REQUIRE(rows[4].generated);
    REQUIRE(rows[5].label == "Asset");
    REQUIRE(rows[6].label == "Empty");
    REQUIRE(rows[7].label.find("Pillar") != std::string::npos);
    REQUIRE(rows[7].subject == EditorSubject::Object);
    REQUIRE(rows[7].index == 1);
    REQUIRE(rows[7].importedNode == 1);
    REQUIRE(rows[8].label == "Off light");
}

//======================================================================================================================
TEST_CASE("Scene tree filtering retains ancestors and disabled subjects", "[app][scene-tree]") {
    auto loaded = treeFixture();
    auto state = scenes::initialDocumentState(loaded);
    state.nodeEnabled[4] = false;
    const auto filtered = buildSceneTree(loaded, state, "pillar", {});
    REQUIRE(filtered.size() == 5);
    REQUIRE(filtered[0].label == "Fixture");
    REQUIRE(filtered[1].label == "Lab");
    REQUIRE(filtered[2].label == "Asset");
    REQUIRE(filtered[3].label == "Empty");
    REQUIRE(filtered[4].subject == EditorSubject::Object);
    const auto all = buildSceneTree(loaded, state, "", {});
    REQUIRE(all[8].label == "Off light");
    REQUIRE_FALSE(all[8].enabled);
    REQUIRE(all.back().subject == EditorSubject::Environment);
    REQUIRE(sceneTreeSubjectCount(all) == 9);
}

//======================================================================================================================
TEST_CASE("Scene tree separates own and effective flags through imported ancestry",
          "[app][scene-tree]") {
    auto loaded = treeFixture();
    auto state = scenes::initialDocumentState(loaded);
    state.nodeEnabled[3] = false;
    state.importedEnabled[1] = false;
    const auto rows = buildSceneTree(loaded, state, "", {});
    REQUIRE_FALSE(rows[5].enabled);
    REQUIRE_FALSE(rows[5].effective);
    REQUIRE(rows[6].enabled);
    REQUIRE_FALSE(rows[6].effective);
    REQUIRE_FALSE(rows[7].enabled);
    REQUIRE_FALSE(rows[7].effective);
    REQUIRE(rows[7].label == "Pillar (2 primitives)");
}

//======================================================================================================================
TEST_CASE("Scene tree collapse hides children while search reveals matches", "[app][scene-tree]") {
    auto loaded = treeFixture();
    auto state = scenes::initialDocumentState(loaded);
    const std::set<uint32_t> collapsed{3};
    const auto compact = buildSceneTree(loaded, state, "", collapsed);
    REQUIRE(std::ranges::none_of(
        compact, [](const auto& row) { return row.label == "Pillar (2 primitives)"; }));
    const auto found = buildSceneTree(loaded, state, "pillar", collapsed);
    REQUIRE(found.back().label == "Pillar (2 primitives)");
}

//======================================================================================================================
TEST_CASE("Scene tree keyboard enters at the first visible row after a hidden selection",
          "[app][scene-tree]") {
    auto loaded = treeFixture();
    auto state = scenes::initialDocumentState(loaded);
    const auto view = buildSceneTreeView(loaded, state, "pillar", {});
    REQUIRE(view.matchedCount == 4);
    REQUIRE(view.totalCount == 9);
    const std::span<const SceneTreeRow> visible(view.rows.begin() + 1, view.rows.end());
    const EditorSelection absent{.sceneId = {"fixture"}, .subject = EditorSubject::None};
    const EditorSelection root{
        .sceneId = {"fixture"}, .subject = EditorSubject::Group, .node = engine::kGeneratedNode};
    const EditorSelection filteredOut{
        .sceneId = {"fixture"}, .subject = EditorSubject::LocalLight, .node = 4};
    for (const auto& selection : {absent, root, filteredOut}) {
        REQUIRE(sceneTreeKeyboardTarget(visible, selection, true)->label == "Lab");
        REQUIRE(sceneTreeKeyboardTarget(visible, selection, false)->label == "Lab");
    }
    const auto last = visible.back();
    const EditorSelection selectedLast{.sceneId = {"fixture"},
                                       .subject = last.subject,
                                       .index = last.index,
                                       .node = last.node,
                                       .importedNode = last.importedNode};
    REQUIRE(sceneTreeKeyboardTarget(visible, selectedLast, true)->label == last.label);
}

//======================================================================================================================
TEST_CASE("Scene root search reveals a match and restores the collapsed state",
          "[app][scene-tree]") {
    bool collapsed = true;
    REQUIRE_FALSE(sceneTreeRootOpen(collapsed, ""));
    REQUIRE(sceneTreeRootOpen(collapsed, "pillar"));
    collapsed = sceneTreeRootCollapsedAfterDraw(collapsed, true, "pillar");
    REQUIRE(collapsed);
    REQUIRE_FALSE(sceneTreeRootOpen(collapsed, ""));
    collapsed = sceneTreeRootCollapsedAfterDraw(collapsed, true, "");
    REQUIRE_FALSE(collapsed);
}

//======================================================================================================================
TEST_CASE("Inspector search warning follows the selected document row", "[app][scene-tree]") {
    auto loaded = treeFixture();
    loaded.document.nodes[4].name = "Key";
    const auto state = scenes::initialDocumentState(loaded);
    const EditorSelection key{
        .sceneId = {"fixture"}, .subject = EditorSubject::DirectionalLight, .index = 0, .node = 4};
    const EditorSelection pillar{.sceneId = {"fixture"},
                                 .subject = EditorSubject::Object,
                                 .index = 1,
                                 .node = 3,
                                 .importedNode = 1};
    const EditorSelection lab{.sceneId = {"fixture"}, .subject = EditorSubject::Group, .node = 1};

    const auto keyRows = buildSceneTree(loaded, state, "Key", {});
    CHECK(sceneTreeSelectionHidden(keyRows, pillar, "Key"));
    CHECK_FALSE(sceneTreeSelectionHidden(keyRows, key, "Key"));
    CHECK_FALSE(sceneTreeSelectionHidden(keyRows, lab, "Key"));
    CHECK_FALSE(sceneTreeSelectionHidden(
        keyRows, EditorSelection{.sceneId = {"fixture"}, .subject = EditorSubject::Environment},
        "Key"));

    const auto pillarRows = buildSceneTree(loaded, state, "Pillar", {});
    CHECK_FALSE(sceneTreeSelectionHidden(pillarRows, pillar, "Pillar"));
    CHECK_FALSE(sceneTreeSelectionHidden(pillarRows, key, ""));
}

//======================================================================================================================
TEST_CASE("Scene tree handles a large generated population in one build", "[app][scene-tree]") {
    auto loaded = treeFixture();
    loaded.scene->objects.resize(4099);
    loaded.binding.objectGeneratorNode.resize(4099, 2);
    loaded.binding.generatedObjectEnabled.resize(4099, true);
    for (size_t i = 3; i < loaded.scene->objects.size(); ++i)
        loaded.scene->objects[i].name = i % 2 == 0 ? "Repeated" : "Unique " + std::to_string(i);
    auto state = scenes::initialDocumentState(loaded);
    const auto view = buildSceneTreeView(loaded, state, "Repeated", {});
    REQUIRE(view.totalCount == 4105);
    REQUIRE(view.matchedCount > 2000);
    REQUIRE(view.rows.front().label == "Fixture");
    REQUIRE(view.rows.back().label.find("Repeated [object 4098]") != std::string::npos);
}
