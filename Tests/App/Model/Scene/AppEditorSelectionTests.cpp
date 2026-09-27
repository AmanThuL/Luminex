#include <catch2/catch_test_macros.hpp>

#include "App/Model/Scene/EditorSelection.h"
#include "Engine/Scene/Scene.h"
#include "Scenes/SceneLibrary.h"

#include <algorithm>
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

const scenes::SceneId kSceneA{"sponza"};
const scenes::SceneId kSceneB{"damaged-helmet"};
constexpr size_t kRenderingCategoryCount = static_cast<size_t>(RenderingCategory::Count);
template <typename T>
constexpr bool hasRenderingSubject = requires { T::Rendering; };
constexpr size_t kFirstLightRow = 0;
constexpr size_t kFirstObjectRow = kFirstLightRow + 3;

} // namespace

//======================================================================================================================
TEST_CASE("resolveSelection keeps a selection whose scene and index are still valid",
          "[app][selection]") {
    const engine::Scene scene = sceneWithObjects({"Crate", "Barrel"});

    const EditorSelection none{.sceneId = kSceneA, .subject = EditorSubject::None, .index = 0};
    const EditorSelection camera{.sceneId = kSceneA, .subject = EditorSubject::Camera, .index = 0};
    const EditorSelection light2{
        .sceneId = kSceneA, .subject = EditorSubject::DirectionalLight, .index = 2};
    const EditorSelection object1{.sceneId = kSceneA, .subject = EditorSubject::Object, .index = 1};

    for (const EditorSelection& selection : {none, camera, light2, object1}) {
        const EditorSelection resolved = resolveSelection(selection, kSceneA, scene);
        REQUIRE(resolved.subject == selection.subject);
        REQUIRE(resolved.index == selection.index);
        REQUIRE(resolved.sceneId == kSceneA);
    }
}

//======================================================================================================================
TEST_CASE("resolveSelection heals a stale scene id to None on the active scene",
          "[app][selection]") {
    const engine::Scene scene = sceneWithObjects({"Crate"});
    const EditorSelection stale{.sceneId = kSceneB, .subject = EditorSubject::Object, .index = 0};

    const EditorSelection resolved = resolveSelection(stale, kSceneA, scene);

    REQUIRE(resolved.subject == EditorSubject::None);
    REQUIRE(resolved.sceneId == kSceneA);
    REQUIRE(resolved.index == 0);
}

//======================================================================================================================
TEST_CASE("resolveSelection heals an out-of-range light index to None", "[app][selection]") {
    const engine::Scene scene = sceneWithObjects({});
    const EditorSelection outOfRange{
        .sceneId = kSceneA, .subject = EditorSubject::DirectionalLight, .index = 3};

    const EditorSelection resolved = resolveSelection(outOfRange, kSceneA, scene);

    REQUIRE(resolved.subject == EditorSubject::None);
    REQUIRE(resolved.sceneId == kSceneA);
}

//======================================================================================================================
TEST_CASE("resolveSelection heals an out-of-range object index to None", "[app][selection]") {
    const engine::Scene scene = sceneWithObjects({"Crate"});
    const EditorSelection outOfRange{
        .sceneId = kSceneA, .subject = EditorSubject::Object, .index = 1};

    const EditorSelection resolved = resolveSelection(outOfRange, kSceneA, scene);

    REQUIRE(resolved.subject == EditorSubject::None);
    REQUIRE(resolved.sceneId == kSceneA);
}

//======================================================================================================================
TEST_CASE("startup selects the scene's Camera", "[app][selection]") {
    const EditorSelection selection = initialSelection(kSceneA);

    REQUIRE(selection.subject == EditorSubject::Camera);
    REQUIRE(selection.sceneId == kSceneA);
    REQUIRE(selection.index == 0);
}

//======================================================================================================================
TEST_CASE("a successful scene switch selects the new scene's Camera and clears the filter",
          "[app][selection]") {
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
TEST_CASE("a failed scene switch retains selection and filter exactly", "[app][selection]") {
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
TEST_CASE("selecting the already-active scene changes nothing", "[app][selection]") {
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
TEST_CASE("repeated switching never restores a stale prior-scene selection", "[app][selection]") {
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
TEST_CASE("scene selection rows follow the spec's fixed group and item order", "[app][selection]") {
    const engine::Scene scene = sceneWithObjects({"Crate", "Barrel"});

    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "");

    REQUIRE(rows.size() == kFirstObjectRow + 2);
    REQUIRE(std::all_of(rows.begin(), rows.end(), [](const auto& row) {
        return row.subject == EditorSubject::DirectionalLight ||
               row.subject == EditorSubject::LocalLight || row.subject == EditorSubject::Object;
    }));

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
TEST_CASE("local-light rows sit between directional lights and objects and filter by label",
          "[app][selection]") {
    auto scene = sceneWithObjects({"Crate", "Barrel"});
    const auto point = scene.addLight(engine::LocalLight{});
    engine::LocalLight spotLight;
    spotLight.type = engine::LocalLightType::Spot;
    spotLight.innerCone = 0.2f;
    spotLight.outerCone = 0.4f;
    const auto spot = scene.addLight(spotLight);
    REQUIRE(point);
    REQUIRE(spot);

    const auto rows = buildSceneSelectionRows(scene, "");
    REQUIRE(rows.size() == kFirstObjectRow + 2 + 2);
    for (size_t i = 0; i < 3; ++i)
        REQUIRE(rows[kFirstLightRow + i].subject == EditorSubject::DirectionalLight);
    REQUIRE(rows[3].subject == EditorSubject::LocalLight);
    REQUIRE(rows[3].group == EditorSelectionGroup::LocalLights);
    REQUIRE(rows[3].lightId == *point);
    REQUIRE(rows[4].subject == EditorSubject::LocalLight);
    REQUIRE(rows[4].lightId == *spot);
    REQUIRE(rows[5].subject == EditorSubject::Object);
    REQUIRE(rows[5].displayLabel == "Crate");
    REQUIRE(rows[6].subject == EditorSubject::Object);
    REQUIRE(rows[6].displayLabel == "Barrel");

    const std::string spotLabel = sceneLocalLightLabel(scene, *spot);
    const auto filtered = buildSceneSelectionRows(scene, spotLabel);
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered.front().subject == EditorSubject::LocalLight);
    REQUIRE(filtered.front().lightId == *spot);
    REQUIRE(filtered.front().displayLabel == spotLabel);
}

//======================================================================================================================
TEST_CASE("rendering topics have stable category identities and independent labels",
          "[app][selection]") {
    const auto scene = sceneWithObjects({});
    const auto rows = buildSceneSelectionRows(scene, "rendering");
    constexpr std::array<std::string_view, kRenderingCategoryCount> labels{
        "Rendering", "Reconstruction", "Resolution", "Visibility", "Occlusion", "Submission",
        "Lighting",  "Exposure",       "Bloom",      "Shadows",    "Display",   "Scene tables"};
    REQUIRE(rows.empty());
    for (size_t i = 0; i < labels.size(); ++i) {
        CAPTURE(i);
        REQUIRE(renderingCategoryLabel(static_cast<RenderingCategory>(i)) == labels[i]);
    }
    REQUIRE(renderingCategoryLabel(RenderingCategory::Count) == "Unavailable");
}

//======================================================================================================================
TEST_CASE("rendering category searches do not invent selectable parent matches",
          "[app][selection]") {
    const auto scene = sceneWithObjects({});
    const auto rows = buildSceneSelectionRows(scene, "oCcLuS");
    REQUIRE(rows.empty());
    const EditorSelection camera{.sceneId = kSceneA, .subject = EditorSubject::Camera};
    REQUIRE_FALSE(selectionHiddenByFilter(scene, camera, "occlus"));
    REQUIRE_FALSE(nextVisibleRow(rows, camera));
    REQUIRE_FALSE(previousVisibleRow(rows, camera));
    REQUIRE(buildSceneSelectionRows(scene, "scene tables").empty());
}

//======================================================================================================================
TEST_CASE("camera navigation enters only visible scene subjects", "[app][selection]") {
    const auto scene = sceneWithObjects({});
    const auto rows = buildSceneSelectionRows(scene, "");
    const EditorSelection camera{.sceneId = kSceneA, .subject = EditorSubject::Camera};
    REQUIRE(nextVisibleRow(rows, camera)->subject == EditorSubject::DirectionalLight);
    REQUIRE(previousVisibleRow(rows, camera)->subject == EditorSubject::DirectionalLight);
    const std::vector<EditorSelectionRow> collapsed{rows[2]};
    REQUIRE(nextVisibleRow(collapsed, camera)->index == 2);
    REQUIRE(previousVisibleRow(collapsed, camera)->index == 2);
}

//======================================================================================================================
TEST_CASE("camera scene switches preserve selection policy", "[app][selection]") {
    const auto scene = sceneWithObjects({});
    const EditorSelection selected{.sceneId = kSceneA, .subject = EditorSubject::Camera};
    const auto failed = sceneSwitchOutcome(false, kSceneA, kSceneB, selected, "Occlusion");
    REQUIRE(failed.selection.index == selected.index);
    REQUIRE(failed.selection.subject == EditorSubject::Camera);
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
TEST_CASE("duplicate object display names keep distinct row identity", "[app][selection]") {
    const engine::Scene scene = sceneWithObjects({"Crate", "Crate"});

    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "crate");

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].displayLabel == "Crate [object 0]");
    REQUIRE(rows[1].displayLabel == "Crate [object 1]");
    REQUIRE(rows[0].index == 0);
    REQUIRE(rows[1].index == 1);
}

//======================================================================================================================
TEST_CASE("the scene filter matches display names case-insensitively", "[app][selection]") {
    const engine::Scene scene = sceneWithObjects({"Crate"});

    REQUIRE(buildSceneSelectionRows(scene, "CAM").empty());
    REQUIRE(buildSceneSelectionRows(scene, "cam").empty());
    REQUIRE(buildSceneSelectionRows(scene, "CrAtE").size() == 1);
}

//======================================================================================================================
TEST_CASE("a filter matching nothing returns an empty row list", "[app][selection]") {
    const engine::Scene scene = sceneWithObjects({"Crate"});

    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "nonexistent");

    REQUIRE(rows.empty());
}

//======================================================================================================================
// The filter only changes visibility: a filtered-out selection is still resolvable, and clearing
// the filter reveals its row again at the same identity.
TEST_CASE("a filtered-out selection is retained and revealed when the filter clears",
          "[app][selection]") {
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
TEST_CASE("nextVisibleRow and previousVisibleRow walk visible rows in order", "[app][selection]") {
    const engine::Scene scene = sceneWithObjects({"Crate"});
    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "");
    const EditorSelection camera{.sceneId = kSceneA, .subject = EditorSubject::Camera, .index = 0};

    const std::optional<EditorSelectionRow> afterNext = nextVisibleRow(rows, camera);
    REQUIRE(afterNext.has_value());
    REQUIRE(afterNext->subject == EditorSubject::DirectionalLight);
    REQUIRE(afterNext->index == 0);

    const std::optional<EditorSelectionRow> afterPrevious = previousVisibleRow(rows, camera);
    REQUIRE(afterPrevious.has_value());
    REQUIRE(afterPrevious->subject == EditorSubject::DirectionalLight);
    REQUIRE(afterPrevious->index == 0);
}

//======================================================================================================================
TEST_CASE("visible-row navigation clamps at the first and last row", "[app][selection]") {
    const engine::Scene scene = sceneWithObjects({});
    const std::vector<EditorSelectionRow> rows = buildSceneSelectionRows(scene, "");
    const EditorSelection first{
        .sceneId = kSceneA, .subject = EditorSubject::DirectionalLight, .index = 0};
    const EditorSelection last{
        .sceneId = kSceneA, .subject = EditorSubject::DirectionalLight, .index = 2};

    REQUIRE(previousVisibleRow(rows, first)->subject == EditorSubject::DirectionalLight);
    REQUIRE(previousVisibleRow(rows, first)->index == 0);
    REQUIRE(nextVisibleRow(rows, last)->subject == EditorSubject::DirectionalLight);
    REQUIRE(nextVisibleRow(rows, last)->index == 2);
}

//======================================================================================================================
// A retained selection that the filter hid is not "found": both directions restart at the first
// visible row, matching the panel's documented policy rather than guessing a lost position.
TEST_CASE("navigation from a filtered-out selection starts at the first visible row",
          "[app][selection]") {
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
TEST_CASE("navigation over an empty row list finds nothing", "[app][selection]") {
    const std::vector<EditorSelectionRow> empty;
    const EditorSelection selection{.sceneId = kSceneA, .subject = EditorSubject::None, .index = 0};

    REQUIRE_FALSE(nextVisibleRow(empty, selection).has_value());
    REQUIRE_FALSE(previousVisibleRow(empty, selection).has_value());
}

//======================================================================================================================
TEST_CASE("unnamed subjects and filtered selection keep explicit scene-local identity",
          "[app][selection]") {
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
TEST_CASE("primitive qualifiers are compact but full source names remain searchable",
          "[app][selection]") {
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
          "[app][selection]") {
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
TEST_CASE("hierarchy navigation uses drawn leaves after a source group collapses",
          "[app][selection]") {
    const auto scene = sceneWithObjects({"Arch", "Leaf", "Lamp"});
    const auto all = buildSceneSelectionRows(scene, "");
    // The panel submits only leaves whose ancestors are expanded; closing the first source group
    // removes Arch and Leaf from navigation without changing the selected scene-local identity.
    const std::vector<EditorSelectionRow> drawn{all[0], all[1], all[kFirstObjectRow + 2]};
    const EditorSelection light{
        .sceneId = kSceneA, .subject = EditorSubject::DirectionalLight, .index = 1};
    REQUIRE(nextVisibleRow(drawn, light)->index == 2);
    REQUIRE(nextVisibleRow(drawn, light)->subject == EditorSubject::Object);
    const EditorSelection lamp{.sceneId = kSceneA, .subject = EditorSubject::Object, .index = 2};
    REQUIRE(previousVisibleRow(drawn, lamp)->subject == EditorSubject::DirectionalLight);
    REQUIRE(previousVisibleRow(drawn, lamp)->index == 1);
    const EditorSelection collapsed{
        .sceneId = kSceneA, .subject = EditorSubject::Object, .index = 0};
    REQUIRE(nextVisibleRow(drawn, collapsed)->subject == EditorSubject::DirectionalLight);
    REQUIRE(collapsed.index == 0);
}

//======================================================================================================================
TEST_CASE("Local light selections retain full identity through slot reuse", "[app][selection]") {
    lmx::engine::Scene scene;
    const auto sceneId = *lmx::scenes::parseSceneId("light-lab");
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

//======================================================================================================================
TEST_CASE("hierarchy counts include disabled local lights and count only scene subjects",
          "[app][selection]") {
    auto scene = sceneWithObjects({"Crate", "Camera", "Rendering"});
    engine::LocalLight disabled;
    disabled.enabled = false;
    REQUIRE(scene.addLight(disabled));
    REQUIRE(scene.addLight(engine::LocalLight{}));
    const size_t total =
        std::size(scene.lights) + scene.localLights().size() + scene.objects.size();
    REQUIRE(total == 8);
    REQUIRE(hierarchyTotal(scene) == buildSceneSelectionRows(scene, "").size());
    for (const auto filter : {"", "light", "POINT", "crate", "CAM", "rendering", "missing"}) {
        CAPTURE(filter);
        const auto count = hierarchyCount(scene, filter);
        REQUIRE(count.total == total);
        REQUIRE(count.total == buildSceneSelectionRows(scene, "").size());
        REQUIRE(count.shown == buildSceneSelectionRows(scene, filter).size());
        REQUIRE(count.shown <= count.total);
    }
    const EditorSelection camera{.sceneId = kSceneA, .subject = EditorSubject::Camera};
    REQUIRE_FALSE(selectionHiddenByFilter(scene, camera, "crate"));
}

//======================================================================================================================
TEST_CASE("catalog LightLab and Sponza hierarchy counts include every local light",
          "[gpu][selection]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    scenes::SceneLibrary library(**device);
    for (const auto name : {"light-lab", "sponza"}) {
        CAPTURE(name);
        const auto id = scenes::parseSceneId(name);
        REQUIRE(id);
        REQUIRE(library.entry(*id).available);
        const auto loaded = library.get(*id);
        REQUIRE(loaded);
        const auto& scene = **loaded;
        const auto rows = buildSceneSelectionRows(scene, "");
        const size_t total =
            std::size(scene.lights) + scene.localLights().size() + scene.objects.size();
        REQUIRE_FALSE(scene.localLights().empty());
        REQUIRE(rows.size() == total);
        REQUIRE(static_cast<size_t>(std::count_if(rows.begin(), rows.end(), [](const auto& row) {
                    return row.subject == EditorSubject::LocalLight;
                })) == scene.localLights().size());
        for (const auto filter : {"", "light", "POINT", "spot", "Sponza", "sphere", "missing"}) {
            CAPTURE(filter);
            const auto count = hierarchyCount(scene, filter);
            REQUIRE(count.total == total);
            REQUIRE(count.total == rows.size());
            REQUIRE(count.shown == buildSceneSelectionRows(scene, filter).size());
            REQUIRE(count.shown <= count.total);
        }
    }
    (*device)->waitIdle();
}

//======================================================================================================================
TEST_CASE("rendering panel is outside the selection subject model",
          "[app][selection][ux2-rendering]") {
    REQUIRE_FALSE(hasRenderingSubject<EditorSubject>);
}
