#include <catch2/catch_test_macros.hpp>

#include "App/Model/Scene/SceneSession.h"
#include "App/Model/Scene/SceneTree.h"
#include "App/Model/Scene/SceneTreeState.h"
#include "Scenes/SceneDocumentExport.h"
#include "Support/GraphTestSupport.h"
#include "Support/SceneDocumentFixtures.h"

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

//======================================================================================================================
TEST_CASE("Scene tree state rebuilds only when its key changes", "[app][scene-tree][cache]") {
    auto loaded = treeFixture();
    auto state = scenes::initialDocumentState(loaded);
    SceneTreeState tree;
    const auto inputs = [&](std::string_view filter, uint64_t edit) {
        return SceneTreeInputs{
            .loaded = loaded, .state = state, .filter = filter, .editGeneration = edit};
    };
    tree.view("a", inputs("", 0));
    tree.view("a", inputs("", 0));
    CHECK(tree.buildCount() == 1);
    tree.view("a", inputs("pillar", 0));
    CHECK(tree.buildCount() == 2);
    tree.view("a", inputs("pillar", 1));
    CHECK(tree.buildCount() == 3);
    tree.view("a", {.loaded = loaded,
                    .state = state,
                    .filter = "pillar",
                    .sceneGeneration = 1,
                    .editGeneration = 1});
    CHECK(tree.buildCount() == 4);
    tree.view("b", {.loaded = loaded,
                    .state = state,
                    .filter = "pillar",
                    .sceneGeneration = 1,
                    .editGeneration = 1});
    CHECK(tree.buildCount() == 5);
}

//======================================================================================================================
TEST_CASE("Scene tree state rebuilds for collapse changes but not repeated or root ones",
          "[app][scene-tree][cache]") {
    auto loaded = treeFixture();
    auto state = scenes::initialDocumentState(loaded);
    SceneTreeState tree;
    const SceneTreeInputs inputs{.loaded = loaded, .state = state};
    const size_t expanded = tree.view("a", inputs).rows.size();
    tree.setRootCollapsed("a", true);
    tree.view("a", inputs);
    CHECK(tree.buildCount() == 1);
    tree.setCollapsed("a", 3, true);
    CHECK(tree.view("a", inputs).rows.size() < expanded);
    CHECK(tree.buildCount() == 2);
    tree.setCollapsed("a", 3, true);
    tree.view("a", inputs);
    CHECK(tree.buildCount() == 2);
    tree.setCollapsed("a", 3, false);
    CHECK(tree.view("a", inputs).rows.size() == expanded);
}

//======================================================================================================================
TEST_CASE("Scene tree state notices enabled-flag changes without an edit generation",
          "[app][scene-tree][cache]") {
    auto loaded = treeFixture();
    auto state = scenes::initialDocumentState(loaded);
    SceneTreeState tree;
    const SceneTreeInputs inputs{.loaded = loaded, .state = state};
    tree.setCollapsed("a", 2, false);
    tree.view("a", inputs);
    loaded.scene->objects[0].enabled = false;
    const auto& view = tree.view("a", inputs);
    CHECK(tree.buildCount() == 2);
    const auto generated = std::ranges::find(view.rows, "Generated cube", &SceneTreeRow::label);
    REQUIRE(generated != view.rows.end());
    CHECK_FALSE(generated->effective);
    state.nodeEnabled[3] = false;
    tree.view("a", inputs);
    CHECK(tree.buildCount() == 3);
}

//======================================================================================================================
TEST_CASE("Scene tree state carries collapse choices across a rekey", "[app][scene-tree][cache]") {
    auto loaded = treeFixture();
    auto state = scenes::initialDocumentState(loaded);
    SceneTreeState tree;
    tree.setCollapsed("old", 3, true);
    tree.setRootCollapsed("old", true);
    tree.rekey("old", "new");
    CHECK(tree.collapsed("new", 3));
    CHECK(tree.rootCollapsed("new"));
    CHECK_FALSE(tree.collapsed("old", 3));
    const SceneTreeInputs inputs{.loaded = loaded, .state = state};
    tree.view("new", inputs);
    CHECK(tree.buildCount() == 1);
}

//======================================================================================================================
TEST_CASE("Scene tree visible rows hide descendants of a collapsed root outside search",
          "[app][scene-tree]") {
    auto loaded = treeFixture();
    auto state = scenes::initialDocumentState(loaded);
    const auto view = buildSceneTreeView(loaded, state, "", {});
    CHECK(sceneTreeVisibleRows(view.rows, false, "").size() == view.rows.size());
    CHECK(sceneTreeVisibleRows(view.rows, true, "").size() == 1);
    CHECK(sceneTreeVisibleRows(view.rows, true, "pillar").size() == view.rows.size());
    CHECK(sceneTreeVisibleRows({}, true, "").empty());
}

//======================================================================================================================
TEST_CASE("Document labels share one unnamed fallback and dirty marker", "[app][scene-tree]") {
    CHECK(documentNodeLabel("", 7) == "Node 7");
    CHECK(documentNodeLabel("Sun", 7) == "Sun");
    CHECK(documentTitle("Sponza", true) == "Sponza*");
    CHECK(documentTitle("Sponza", false) == "Sponza");
    auto loaded = treeFixture();
    loaded.document.nodes[1].name.clear();
    auto state = scenes::initialDocumentState(loaded);
    const auto rows = buildSceneTree(loaded, state, "", {});
    CHECK(std::ranges::any_of(rows, [](const auto& row) { return row.label == "Node 1"; }));
}

//======================================================================================================================
TEST_CASE("Generator groups default to collapsed and search retains their generated children",
          "[app][scene-tree]") {
    auto loaded = treeFixture();
    const auto state = scenes::initialDocumentState(loaded);
    SceneTreeState tree;
    const SceneTreeInputs inputs{.loaded = loaded, .state = state};
    const auto& compact = tree.view("fixture", inputs);
    CHECK(tree.collapsed("fixture", 2));
    CHECK_FALSE(tree.collapsed("fixture", 1));
    CHECK_FALSE(tree.collapsed("fixture", 3));
    CHECK(std::ranges::none_of(compact.rows, &SceneTreeRow::generated));
    CHECK(compact.totalCount == 9);
    const auto& filtered =
        tree.view("fixture", {.loaded = loaded, .state = state, .filter = "generated cube"});
    REQUIRE(filtered.rows.size() == 4);
    CHECK(filtered.rows[1].label == "Lab");
    CHECK(filtered.rows[2].label == "Generated");
    CHECK(filtered.rows[2].group);
    CHECK(filtered.rows.back().generated);
    CHECK(filtered.rows.back().node == 2);
    CHECK(filtered.rows.back().depth == filtered.rows[2].depth + 1);
    CHECK(tree.collapsed("fixture", 2));
    CHECK(std::ranges::none_of(tree.view("fixture", inputs).rows, &SceneTreeRow::generated));
    tree.setCollapsed("fixture", 2, false);
    CHECK(std::ranges::any_of(tree.view("fixture", inputs).rows, &SceneTreeRow::generated));
    tree.view("other", inputs);
    CHECK(std::ranges::any_of(tree.view("fixture", inputs).rows, &SceneTreeRow::generated));
    tree.rekey("fixture", "saved-as");
    CHECK_FALSE(tree.collapsed("saved-as", 2));
    CHECK(std::ranges::any_of(tree.view("saved-as", inputs).rows, &SceneTreeRow::generated));
}

//======================================================================================================================
TEST_CASE("An explicit generator expansion before first view survives default initialization",
          "[app][scene-tree]") {
    auto loaded = treeFixture();
    const auto state = scenes::initialDocumentState(loaded);
    SceneTreeState tree;
    tree.setCollapsed("fixture", 2, false);
    tree.setCollapsed("fixture", 3, true);
    const auto& rows = tree.view("fixture", {.loaded = loaded, .state = state}).rows;
    CHECK_FALSE(tree.collapsed("fixture", 2));
    CHECK(tree.collapsed("fixture", 3));
    CHECK(std::ranges::any_of(rows, &SceneTreeRow::generated));
    CHECK(std::ranges::none_of(rows, [](const auto& row) { return row.importedNode == 1; }));
}

//======================================================================================================================
TEST_CASE("Saved mesh rows select the bound object and retain exported pose edits",
          "[app][scene-tree]") {
    FakeDevice device;
    auto doc = test::contentDocument();
    doc.nodes[2].enabled = true;
    doc.nodes[2].mobility = asset::DocMobility::Movable;
    // Document node 2 maps to object 1, not the node's numeric slot.
    const auto path = std::filesystem::temp_directory_path() / "lmx-saved-mesh-tree.scene.gltf";
    REQUIRE(asset::saveSceneDocument(doc, path));
    auto result = scenes::loadSceneDocument(device, path);
    INFO((result ? "loaded" : result.error().message));
    REQUIRE(result);
    auto loaded = std::move(*result);
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::Reset);
    const auto rows = buildSceneTree(loaded, session.documentState(), "second cube", {}, &session);
    REQUIRE(rows.size() == 2);
    const auto& row = rows.back();
    REQUIRE(row.subject == EditorSubject::Object);
    CHECK(row.node == 2);
    REQUIRE(row.index == 1);
    CHECK_FALSE(row.generated);
    CHECK_FALSE(row.group);
    CHECK(row.importedNode == engine::kGeneratedNode);
    const EditorSelection selection{.sceneId = {"fixture"},
                                    .subject = row.subject,
                                    .index = row.index,
                                    .node = row.node,
                                    .importedNode = row.importedNode};
    CHECK(sceneTreeRowSelected(row, selection));
    CHECK_FALSE(sceneTreeSelectionHidden(rows, selection, "second cube"));
    CHECK(sceneTreeKeyboardTarget(rows, {}, true)->subject == EditorSubject::Group);
    CHECK(sceneTreeKeyboardTarget(std::span(rows).subspan(1), {}, true)->index == row.index);
    const glm::vec3 edited{9.f, 8.f, 7.f};
    const auto& object = loaded.scene->objects[row.index];
    REQUIRE(session.editObject(row.index, {edited, object.eulerDegrees, object.scale}));
    auto exported = scenes::exportSceneDocument(loaded, session.scene(), session.documentState());
    REQUIRE(exported);
    CHECK(exported->nodes[row.node].translation == edited);
    CHECK(scenes::documentDirty(loaded.document, *exported));
    REQUIRE(asset::saveSceneDocument(*exported, path));
    auto reopened = scenes::loadSceneDocument(device, path);
    REQUIRE(reopened);
    const auto persisted = std::ranges::find(reopened->binding.objectNode, row.node);
    REQUIRE(persisted != reopened->binding.objectNode.end());
    CHECK(reopened->scene->objects[persisted - reopened->binding.objectNode.begin()].position ==
          edited);
}
