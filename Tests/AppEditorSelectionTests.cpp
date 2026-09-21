#include <catch2/catch_test_macros.hpp>

#include "App/Model/EditorSelection.h"
#include "Engine/Catalog/SceneLibrary.h"
#include "Engine/Scene/Scene.h"

#include <array>
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
constexpr size_t kRenderingCategoryCount = static_cast<size_t>(RenderingCategory::Count);
constexpr size_t kFirstLightRow = 1 + kRenderingCategoryCount;
constexpr size_t kFirstObjectRow = kFirstLightRow + 3;

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
        sceneSwitchOutcome(/*switchSucceeded=*/true, kSceneA, kSceneB, previous, previousFilter);

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
        sceneSwitchOutcome(/*switchSucceeded=*/false, kSceneA, kSceneB, previous, previousFilter);

    REQUIRE(outcome.selection.subject == previous.subject);
    REQUIRE(outcome.selection.sceneId == previous.sceneId);
    REQUIRE(outcome.selection.index == previous.index);
    REQUIRE(outcome.filter == previousFilter);
}

//======================================================================================================================
// Reselecting the scene that is already active must not reset the workspace: a combo box that
// fires on re-clicking the active row is safe to wire directly to this function without a caller
// guard, exactly because it is a documented no-op here.
TEST_CASE("selecting the already-active scene changes nothing", "[app]") {
    const EditorSelection previous{
        .sceneId = kSceneA, .subject = EditorSubject::Object, .index = 2};
    const std::string previousFilter = "crate";

    const SceneSwitchOutcome outcome =
        sceneSwitchOutcome(/*switchSucceeded=*/true, kSceneA, kSceneA, previous, previousFilter);

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

    const SceneSwitchOutcome toB = sceneSwitchOutcome(true, kSceneA, kSceneB, start, "filter");
    const SceneSwitchOutcome backToA =
        sceneSwitchOutcome(true, kSceneB, kSceneA, toB.selection, toB.filter);

    REQUIRE(toB.selection.subject == EditorSubject::Camera);
    REQUIRE(backToA.selection.subject == EditorSubject::Camera);
    REQUIRE(backToA.selection.sceneId == kSceneA);
    REQUIRE(backToA.filter.empty());
}

//======================================================================================================================
TEST_CASE("scene selection rows follow the spec's fixed group and item order", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate", "Barrel"});

    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "");

    REQUIRE(rows.size() == kFirstObjectRow + 2);
    REQUIRE(rows[0].subject == EditorSubject::Camera);
    REQUIRE(rows[0].displayLabel == "Editor Camera");
    REQUIRE(rows[0].group == EditorSelectionGroup::Workspace);
    REQUIRE(rows[1].subject == EditorSubject::Rendering);
    REQUIRE(rows[1].displayLabel == "Rendering");
    REQUIRE(rows[1].group == EditorSelectionGroup::Workspace);

    for (size_t i = 0; i < 3; ++i) {
        const EditorSelectionRow& row = rows[kFirstLightRow + i];
        REQUIRE(row.subject == EditorSubject::DirectionalLight);
        REQUIRE(row.index == i);
        REQUIRE(row.group == EditorSelectionGroup::DirectionalLights);
    }

    REQUIRE(rows[kFirstObjectRow].subject == EditorSubject::Object);
    REQUIRE(rows[kFirstObjectRow].index == 0);
    REQUIRE(rows[kFirstObjectRow].displayLabel == "Crate");
    REQUIRE(rows[kFirstObjectRow].group == EditorSelectionGroup::Objects);
    REQUIRE(rows[kFirstObjectRow + 1].subject == EditorSubject::Object);
    REQUIRE(rows[kFirstObjectRow + 1].index == 1);
    REQUIRE(rows[kFirstObjectRow + 1].displayLabel == "Barrel");
}

//======================================================================================================================
TEST_CASE("rendering topics have stable category identities and independent labels", "[app]") {
    const auto scene = sceneWithObjects({});
    const auto rows = buildSceneSelectionRows(scene, "rendering");
    constexpr std::array<std::string_view, kRenderingCategoryCount> labels{
        "Rendering", "Reconstruction", "Resolution", "Visibility", "Occlusion", "Submission",
        "Lighting",  "Exposure",       "Bloom",      "Shadows",    "Display",   "Scene tables"};
    REQUIRE(rows.size() == labels.size());
    for (size_t i = 0; i < labels.size(); ++i) {
        CAPTURE(i);
        REQUIRE(rows[i].subject == EditorSubject::Rendering);
        REQUIRE(rows[i].index == i);
        REQUIRE(rows[i].displayLabel == labels[i]);
        REQUIRE(renderingCategoryLabel(static_cast<RenderingCategory>(i)) == labels[i]);
        const EditorSelection selected{
            .sceneId = kSceneA, .subject = EditorSubject::Rendering, .index = i};
        REQUIRE(resolveSelection(selected, kSceneA, scene).index == i);
        REQUIRE_FALSE(selectionHiddenByFilter(scene, selected, labels[i]));
        REQUIRE_FALSE(selectionHiddenByFilter(scene, selected, "rEnDeR"));
    }
    REQUIRE(renderingCategoryLabel(RenderingCategory::Count) == "Unavailable");
}

//======================================================================================================================
TEST_CASE("rendering category searches do not invent selectable parent matches", "[app]") {
    const auto scene = sceneWithObjects({});
    const auto rows = buildSceneSelectionRows(scene, "oCcLuS");
    REQUIRE(rows.size() == 1);
    REQUIRE(rows.front().subject == EditorSubject::Rendering);
    REQUIRE(rows.front().index == static_cast<size_t>(RenderingCategory::Occlusion));
    const EditorSelection overview{.sceneId = kSceneA, .subject = EditorSubject::Rendering};
    const EditorSelection occlusion{.sceneId = kSceneA,
                                    .subject = EditorSubject::Rendering,
                                    .index = static_cast<size_t>(RenderingCategory::Occlusion)};
    REQUIRE(selectionHiddenByFilter(scene, overview, "occlus"));
    REQUIRE_FALSE(selectionHiddenByFilter(scene, occlusion, "occlus"));
    REQUIRE(selectionHiddenByFilter(scene, occlusion, "bloom"));
    REQUIRE(nextVisibleRow(rows, overview)->index == occlusion.index);
    REQUIRE(previousVisibleRow(rows, occlusion)->index == occlusion.index);
    REQUIRE(buildSceneSelectionRows(scene, "scene tables").size() == 1);
}

//======================================================================================================================
TEST_CASE("rendering navigation follows expanded topics and skips collapsed descendants", "[app]") {
    const auto scene = sceneWithObjects({});
    const auto rows = buildSceneSelectionRows(scene, "");
    const EditorSelection overview{.sceneId = kSceneA, .subject = EditorSubject::Rendering};
    const EditorSelection firstTopic{.sceneId = kSceneA,
                                     .subject = EditorSubject::Rendering,
                                     .index =
                                         static_cast<size_t>(RenderingCategory::Reconstruction)};
    const EditorSelection lastTopic{.sceneId = kSceneA,
                                    .subject = EditorSubject::Rendering,
                                    .index = static_cast<size_t>(RenderingCategory::SceneTables)};
    REQUIRE(nextVisibleRow(rows, overview)->index == firstTopic.index);
    REQUIRE(previousVisibleRow(rows, firstTopic)->index == 0);
    REQUIRE(previousVisibleRow(rows, firstTopic)->subject == EditorSubject::Rendering);
    REQUIRE(nextVisibleRow(rows, lastTopic)->subject == EditorSubject::DirectionalLight);
    const std::vector<EditorSelectionRow> collapsed{rows[0], rows[1], rows[kFirstLightRow]};
    REQUIRE(nextVisibleRow(collapsed, overview)->subject == EditorSubject::DirectionalLight);
    REQUIRE(previousVisibleRow(collapsed, overview)->subject == EditorSubject::Camera);
    REQUIRE(nextVisibleRow(collapsed, firstTopic)->subject == EditorSubject::Camera);
    REQUIRE(resolveSelection(firstTopic, kSceneA, scene).index == firstTopic.index);
}

//======================================================================================================================
TEST_CASE("rendering category validation and scene switches preserve selection policy", "[app]") {
    const auto scene = sceneWithObjects({});
    const EditorSelection invalid{
        .sceneId = kSceneA, .subject = EditorSubject::Rendering, .index = kRenderingCategoryCount};
    REQUIRE(resolveSelection(invalid, kSceneA, scene).subject == EditorSubject::None);
    const EditorSelection selected{.sceneId = kSceneA,
                                   .subject = EditorSubject::Rendering,
                                   .index = static_cast<size_t>(RenderingCategory::Occlusion)};
    const auto failed = sceneSwitchOutcome(false, kSceneA, kSceneB, selected, "Occlusion");
    REQUIRE(failed.selection.index == selected.index);
    REQUIRE(failed.selection.subject == EditorSubject::Rendering);
    REQUIRE(failed.filter == "Occlusion");
    const auto same = sceneSwitchOutcome(true, kSceneA, kSceneA, selected, "Occlusion");
    REQUIRE(same.selection.index == selected.index);
    REQUIRE(same.filter == "Occlusion");
    const auto switched = sceneSwitchOutcome(true, kSceneA, kSceneB, selected, "Occlusion");
    REQUIRE(switched.selection.subject == EditorSubject::Camera);
    REQUIRE(switched.selection.sceneId == kSceneB);
    REQUIRE(switched.filter.empty());
    REQUIRE(resolveSelection(selected, kSceneB, scene).subject == EditorSubject::None);
}

//======================================================================================================================
// Two objects sharing a display name still occupy distinct rows: index, not text, is identity.
TEST_CASE("duplicate object display names keep distinct row identity", "[app]") {
    const engine::Scene scene = sceneWithObjects({"Crate", "Crate"});

    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "crate");

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].displayLabel == "Crate [object 0]");
    REQUIRE(rows[1].displayLabel == "Crate [object 1]");
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

//======================================================================================================================
TEST_CASE("unnamed subjects and filtered selection keep explicit scene-local identity", "[app]") {
    const engine::Scene scene = sceneWithObjects({"", "Arch", "Arch"});
    const auto rows = buildSceneSelectionRows(scene, "");
    REQUIRE(rows[kFirstObjectRow].displayLabel == "Unnamed object [0]");
    REQUIRE(sceneObjectLabel(scene, 1) == "Arch [object 1]");
    REQUIRE(sceneObjectLabel(scene, 2) == "Arch [object 2]");
    const EditorSelection selection{
        .sceneId = kSceneA, .subject = EditorSubject::Object, .index = 2};
    REQUIRE(selectionHiddenByFilter(scene, selection, "Unnamed"));
    REQUIRE_FALSE(selectionHiddenByFilter(scene, selection, "ARCH"));
    REQUIRE_FALSE(selectionHiddenByFilter(scene, selection, ""));
    REQUIRE(buildSceneSelectionRows(scene, "object 2").front().index == 2);
    REQUIRE(selection.index == 2);
}

//======================================================================================================================
TEST_CASE("primitive qualifiers are compact but full source names remain searchable", "[app]") {
    engine::Scene scene = sceneWithObjects({"Sponza / arch", "Sponza / leaf", "Courtyard / arch"});
    scene.objects[0].sourceName = "Sponza";
    scene.objects[0].materialQualifier = "arch";
    scene.objects[1].sourceName = "Sponza";
    scene.objects[1].materialQualifier = "leaf";
    scene.objects[2].sourceName = "Courtyard";
    scene.objects[2].materialQualifier = "arch";
    const auto all = buildSceneSelectionRows(scene, "");
    REQUIRE(all[kFirstObjectRow].displayLabel == "arch [object 0]");
    REQUIRE(all[kFirstObjectRow].detailLabel == "Sponza / arch");
    REQUIRE(all[kFirstObjectRow + 1].displayLabel == "leaf");
    REQUIRE(all[kFirstObjectRow + 2].displayLabel == "arch [object 2]");
    REQUIRE(buildSceneSelectionRows(scene, "Sponza").size() == 2);
    REQUIRE(buildSceneSelectionRows(scene, "leaf").size() == 1);
    REQUIRE(sceneObjectLabel(scene, 2) == "Courtyard / arch");
    const EditorSelection selected{
        .sceneId = kSceneA, .subject = EditorSubject::Object, .index = 2};
    REQUIRE_FALSE(selectionHiddenByFilter(scene, selected, "object 2"));
    REQUIRE_FALSE(selectionHiddenByFilter(scene, selected, "Courtyard"));
    REQUIRE(selectionHiddenByFilter(scene, selected, "leaf"));
}

//======================================================================================================================
TEST_CASE("source hierarchy preserves distinct primitive identities and filtered parent groups",
          "[app]") {
    engine::Scene scene =
        sceneWithObjects({"Hall / arch", "Courtyard / stone", "Hall / leaf", "Lamp"});
    scene.objects[0].sourceName = "Hall";
    scene.objects[0].materialQualifier = "arch";
    scene.objects[1].sourceName = "Courtyard";
    scene.objects[1].materialQualifier = "stone";
    scene.objects[2].sourceName = "Hall";
    scene.objects[2].materialQualifier = "leaf";
    const auto groups = groupSceneObjectRows(buildSceneSelectionRows(scene, ""));
    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].sourceName == "Hall");
    REQUIRE(groups[0].rows.size() == 2);
    REQUIRE(groups[0].rows[0].index == 0);
    REQUIRE(groups[0].rows[1].index == 2);
    REQUIRE(groups[1].sourceName.empty());
    REQUIRE(groups[1].rows.size() == 2);
    REQUIRE(groups[1].rows[0].index == 1);
    REQUIRE(groups[1].rows[1].index == 3);

    const auto filtered = groupSceneObjectRows(buildSceneSelectionRows(scene, "leaf"));
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered[0].sourceName == "Hall");
    REQUIRE(filtered[0].rows.size() == 1);
    REQUIRE(filtered[0].rows[0].index == 2);
    REQUIRE(scene.objects[2].name == "Hall / leaf");
}

//======================================================================================================================
TEST_CASE("hierarchy navigation uses drawn leaves after a source group collapses", "[app]") {
    const auto scene = sceneWithObjects({"Arch", "Leaf", "Lamp"});
    const auto all = buildSceneSelectionRows(scene, "");
    // The panel submits only leaves whose ancestors are expanded; closing the first source group
    // removes Arch and Leaf from navigation without changing the selected scene-local identity.
    const std::vector<EditorSelectionRow> drawn{all[0], all[1], all[kFirstObjectRow + 2]};
    const EditorSelection rendering{.sceneId = kSceneA, .subject = EditorSubject::Rendering};
    REQUIRE(nextVisibleRow(drawn, rendering)->index == 2);
    REQUIRE(nextVisibleRow(drawn, rendering)->subject == EditorSubject::Object);
    const EditorSelection lamp{.sceneId = kSceneA, .subject = EditorSubject::Object, .index = 2};
    REQUIRE(previousVisibleRow(drawn, lamp)->subject == EditorSubject::Rendering);
    const EditorSelection collapsed{
        .sceneId = kSceneA, .subject = EditorSubject::Object, .index = 0};
    REQUIRE(nextVisibleRow(drawn, collapsed)->subject == EditorSubject::Camera);
    REQUIRE(collapsed.index == 0);
}

//======================================================================================================================
TEST_CASE("Local light selections retain full identity through slot reuse", "[app][selection]") {
    lmx::engine::Scene scene;
    const auto sceneId = *lmx::engine::parseSceneId("light-lab");
    const auto light = scene.addLight(lmx::engine::LocalLight{});
    REQUIRE(light);
    const lmx::app::EditorSelection selected{
        .sceneId = sceneId, .subject = lmx::app::EditorSubject::LocalLight, .lightId = *light};
    REQUIRE(lmx::app::resolveSelection(selected, sceneId, scene).subject ==
            lmx::app::EditorSubject::LocalLight);
    const auto rows = lmx::app::buildSceneSelectionRows(scene, "point");
    REQUIRE(rows.size() == 1);
    REQUIRE(rows.front().lightId == *light);
    REQUIRE_FALSE(lmx::app::selectionHiddenByFilter(scene, selected, "point"));
    REQUIRE(scene.removeLight(*light));
    REQUIRE(scene.addLight(lmx::engine::LocalLight{}));
    REQUIRE(lmx::app::resolveSelection(selected, sceneId, scene).subject ==
            lmx::app::EditorSubject::None);
}
