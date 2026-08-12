#include <catch2/catch_test_macros.hpp>

#include "App/EditorSelection.h"
#include "Engine/Scene.h"
#include "Engine/SceneLibrary.h"

#include <optional>
#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::app;

namespace {

//======================================================================================================================
// A scene needs no device to exist as data: meshes/textures/materials stay empty, only the fields
// selection cares about (object names, light count) are filled.
engine::Scene sceneWithObjects(std::vector<std::string> objectNames) {
    engine::Scene scene;
    for (std::string& name : objectNames) {
        engine::SceneObject object;
        object.name = std::move(name);
        scene.objects.push_back(std::move(object));
    }
    return scene;
}

const engine::SceneId kSceneA{0};
const engine::SceneId kSceneB{1};

} // namespace

//======================================================================================================================
TEST_CASE("resolveSelection keeps a selection whose scene and index are still valid", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate", "Barrel"});

    const EditorSelection none{.sceneId = kSceneA, .subject = EditorSubject::None, .index = 0};
    const EditorSelection camera{.sceneId = kSceneA, .subject = EditorSubject::Camera, .index = 0};
    const EditorSelection rendering{
        .sceneId = kSceneA, .subject = EditorSubject::Rendering, .index = 0};
    const EditorSelection light2{
        .sceneId = kSceneA, .subject = EditorSubject::DirectionalLight, .index = 2};
    const EditorSelection object1{.sceneId = kSceneA, .subject = EditorSubject::Object, .index = 1};

    for (const EditorSelection& selection : {none, camera, rendering, light2, object1}) {
        const EditorSelection resolved = resolveSelection(selection, kSceneA, scene);
        REQUIRE(resolved.subject == selection.subject);
        REQUIRE(resolved.index == selection.index);
        REQUIRE(resolved.sceneId == kSceneA);
    }
}

//======================================================================================================================
TEST_CASE("resolveSelection heals a stale scene id to None on the active scene", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate"});
    const EditorSelection stale{.sceneId = kSceneB, .subject = EditorSubject::Object, .index = 0};

    const EditorSelection resolved = resolveSelection(stale, kSceneA, scene);

    REQUIRE(resolved.subject == EditorSubject::None);
    REQUIRE(resolved.sceneId == kSceneA);
    REQUIRE(resolved.index == 0);
}

//======================================================================================================================
TEST_CASE("resolveSelection heals an out-of-range light index to None", "[app]") {
    const engine::Scene scene = sceneWithObjects({});
    const EditorSelection outOfRange{
        .sceneId = kSceneA, .subject = EditorSubject::DirectionalLight, .index = 3};

    const EditorSelection resolved = resolveSelection(outOfRange, kSceneA, scene);

    REQUIRE(resolved.subject == EditorSubject::None);
    REQUIRE(resolved.sceneId == kSceneA);
}

//======================================================================================================================
TEST_CASE("resolveSelection heals an out-of-range object index to None", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate"});
    const EditorSelection outOfRange{
        .sceneId = kSceneA, .subject = EditorSubject::Object, .index = 1};

    const EditorSelection resolved = resolveSelection(outOfRange, kSceneA, scene);

    REQUIRE(resolved.subject == EditorSubject::None);
    REQUIRE(resolved.sceneId == kSceneA);
}

//======================================================================================================================
TEST_CASE("startup selects the scene's Camera", "[app]") {
    const EditorSelection selection = initialSelection(kSceneA);

    REQUIRE(selection.subject == EditorSubject::Camera);
    REQUIRE(selection.sceneId == kSceneA);
    REQUIRE(selection.index == 0);
}

//======================================================================================================================
TEST_CASE("a successful scene switch selects the new scene's Camera and clears the filter",
          "[app]") {
    const EditorSelection previous{
        .sceneId = kSceneA, .subject = EditorSubject::Object, .index = 2};
    const std::string previousFilter = "crate";

    const SceneSwitchOutcome outcome =
        sceneSwitchOutcome(/*switchSucceeded=*/true, kSceneB, previous, previousFilter);

    REQUIRE(outcome.selection.subject == EditorSubject::Camera);
    REQUIRE(outcome.selection.sceneId == kSceneB);
    REQUIRE(outcome.filter.empty());
}

//======================================================================================================================
TEST_CASE("a failed scene switch retains selection and filter exactly", "[app]") {
    const EditorSelection previous{
        .sceneId = kSceneA, .subject = EditorSubject::Object, .index = 2};
    const std::string previousFilter = "crate";

    const SceneSwitchOutcome outcome =
        sceneSwitchOutcome(/*switchSucceeded=*/false, kSceneB, previous, previousFilter);

    REQUIRE(outcome.selection.subject == previous.subject);
    REQUIRE(outcome.selection.sceneId == previous.sceneId);
    REQUIRE(outcome.selection.index == previous.index);
    REQUIRE(outcome.filter == previousFilter);
}

//======================================================================================================================
// A -> B -> A must not resurrect scene A's old object selection: each successful switch starts
// fresh at Camera, so nothing stale can survive a round trip.
TEST_CASE("repeated switching never restores a stale prior-scene selection", "[app]") {
    const EditorSelection start{.sceneId = kSceneA, .subject = EditorSubject::Object, .index = 4};

    const SceneSwitchOutcome toB = sceneSwitchOutcome(true, kSceneB, start, "filter");
    const SceneSwitchOutcome backToA = sceneSwitchOutcome(true, kSceneA, toB.selection, toB.filter);

    REQUIRE(toB.selection.subject == EditorSubject::Camera);
    REQUIRE(backToA.selection.subject == EditorSubject::Camera);
    REQUIRE(backToA.selection.sceneId == kSceneA);
    REQUIRE(backToA.filter.empty());
}

//======================================================================================================================
TEST_CASE("scene selection rows follow the spec's fixed group and item order", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate", "Barrel"});

    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "");

    REQUIRE(rows.size() == 2 + 3 + 2);
    REQUIRE(rows[0].subject == EditorSubject::Camera);
    REQUIRE(rows[0].displayLabel == "Editor Camera");
    REQUIRE(rows[0].group == EditorSelectionGroup::Workspace);
    REQUIRE(rows[1].subject == EditorSubject::Rendering);
    REQUIRE(rows[1].displayLabel == "Rendering");
    REQUIRE(rows[1].group == EditorSelectionGroup::Workspace);

    for (size_t i = 0; i < 3; ++i) {
        const EditorSelectionRow& row = rows[2 + i];
        REQUIRE(row.subject == EditorSubject::DirectionalLight);
        REQUIRE(row.index == i);
        REQUIRE(row.group == EditorSelectionGroup::DirectionalLights);
    }

    REQUIRE(rows[5].subject == EditorSubject::Object);
    REQUIRE(rows[5].index == 0);
    REQUIRE(rows[5].displayLabel == "Crate");
    REQUIRE(rows[5].group == EditorSelectionGroup::Objects);
    REQUIRE(rows[6].subject == EditorSubject::Object);
    REQUIRE(rows[6].index == 1);
    REQUIRE(rows[6].displayLabel == "Barrel");
}

//======================================================================================================================
// Two objects sharing a display name still occupy distinct rows: index, not text, is identity.
TEST_CASE("duplicate object display names keep distinct row identity", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate", "Crate"});

    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "crate");

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].displayLabel == "Crate");
    REQUIRE(rows[1].displayLabel == "Crate");
    REQUIRE(rows[0].index == 0);
    REQUIRE(rows[1].index == 1);
}

//======================================================================================================================
TEST_CASE("the scene filter matches display names case-insensitively", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate"});

    REQUIRE(buildSceneSelectionRows(scene, "CAM").size() == 1);
    REQUIRE(buildSceneSelectionRows(scene, "cam").size() == 1);
    REQUIRE(buildSceneSelectionRows(scene, "CrAtE").size() == 1);
}

//======================================================================================================================
TEST_CASE("a filter matching nothing returns an empty row list", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate"});

    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "nonexistent");

    REQUIRE(rows.empty());
}

//======================================================================================================================
// The filter only changes visibility: a filtered-out selection is still resolvable, and clearing
// the filter reveals its row again at the same identity.
TEST_CASE("a filtered-out selection is retained and revealed when the filter clears", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate", "Barrel"});
    const EditorSelection selection{
        .sceneId = kSceneA, .subject = EditorSubject::Object, .index = 0};

    const EditorSelection resolved = resolveSelection(selection, kSceneA, scene);
    REQUIRE(resolved.subject == EditorSubject::Object);

    const std::vector<EditorSelectionRow> filtered = buildSceneSelectionRows(scene, "barrel");
    bool foundWhileFiltered = false;
    for (const EditorSelectionRow& row : filtered) {
        if (row.subject == EditorSubject::Object && row.index == 0) {
            foundWhileFiltered = true;
        }
    }
    REQUIRE_FALSE(foundWhileFiltered);

    const std::vector<EditorSelectionRow> cleared = buildSceneSelectionRows(scene, "");
    bool foundAfterClear = false;
    for (const EditorSelectionRow& row : cleared) {
        if (row.subject == EditorSubject::Object && row.index == 0) {
            foundAfterClear = true;
        }
    }
    REQUIRE(foundAfterClear);
}

//======================================================================================================================
TEST_CASE("nextVisibleRow and previousVisibleRow walk visible rows in order", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate"});
    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "");
    const EditorSelection camera{.sceneId = kSceneA, .subject = EditorSubject::Camera, .index = 0};

    const std::optional<EditorSelectionRow> afterNext = nextVisibleRow(rows, camera);
    REQUIRE(afterNext.has_value());
    REQUIRE(afterNext->subject == EditorSubject::Rendering);

    const std::optional<EditorSelectionRow> afterPrevious = previousVisibleRow(rows, camera);
    REQUIRE(afterPrevious.has_value());
    REQUIRE(afterPrevious->subject == EditorSubject::Camera);
}

//======================================================================================================================
TEST_CASE("visible-row navigation clamps at the first and last row", "[app]") {
    const engine::Scene scene = sceneWithObjects({});
    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "");
    const EditorSelection first{.sceneId = kSceneA, .subject = EditorSubject::Camera, .index = 0};
    const EditorSelection last{
        .sceneId = kSceneA, .subject = EditorSubject::DirectionalLight, .index = 2};

    REQUIRE(previousVisibleRow(rows, first)->subject == EditorSubject::Camera);
    REQUIRE(nextVisibleRow(rows, last)->subject == EditorSubject::DirectionalLight);
    REQUIRE(nextVisibleRow(rows, last)->index == 2);
}

//======================================================================================================================
// A retained selection that the filter hid is not "found": both directions restart at the first
// visible row, matching the panel's documented policy rather than guessing a lost position.
TEST_CASE("navigation from a filtered-out selection starts at the first visible row", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate", "Barrel"});
    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "barrel");
    const EditorSelection filteredOut{
        .sceneId = kSceneA, .subject = EditorSubject::Object, .index = 0};

    const std::optional<EditorSelectionRow> next = nextVisibleRow(rows, filteredOut);
    const std::optional<EditorSelectionRow> previous = previousVisibleRow(rows, filteredOut);

    REQUIRE(next.has_value());
    REQUIRE(next->subject == EditorSubject::Object);
    REQUIRE(next->index == 1);
    REQUIRE(previous.has_value());
    REQUIRE(previous->subject == EditorSubject::Object);
    REQUIRE(previous->index == 1);
}

//======================================================================================================================
TEST_CASE("navigation over an empty row list finds nothing", "[app]") {
    const std::vector<EditorSelectionRow> empty;
    const EditorSelection selection{.sceneId = kSceneA, .subject = EditorSubject::None, .index = 0};

    REQUIRE_FALSE(nextVisibleRow(empty, selection).has_value());
    REQUIRE_FALSE(previousVisibleRow(empty, selection).has_value());
}
