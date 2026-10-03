//----------------------------------------------------------------------------------------------------------------------
/// @file AppGizmoModelTests.cpp
/// @brief Tests gizmo permissions, projection, shared edits and reversible drag ownership.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/GizmoModel.h"

#include "Core/IO/File.h"
#include "Engine/Asset/Document/Orientation.h"
#include "Scenes/SceneDocumentExport.h"
#include "Support/SceneDocumentFixtures.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>

using namespace lmx;
using namespace lmx::app;

namespace {

//======================================================================================================================
engine::LoadedScene fixture() {
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.document = test::contentDocument();
    loaded.document.nodes[1].mobility = asset::DocMobility::Movable;
    loaded.document.nodes[2].enabled = true;
    loaded.binding.nodes.resize(5);
    loaded.binding.objectNode = {1, 2};
    loaded.binding.objectImportedNode.assign(2, engine::kGeneratedNode);
    loaded.binding.objectGeneratorNode.assign(2, engine::kGeneratedNode);
    loaded.objectMobility = {asset::DocMobility::Movable, asset::DocMobility::Static};
    loaded.scene->objects.resize(2);
    for (uint32_t i = 0; i < 2; ++i) {
        const auto& node = loaded.document.nodes[i + 1];
        auto& object = loaded.scene->objects[i];
        object.position = node.translation;
        object.eulerDegrees = asset::eulerDegreesForRotation(node.rotation);
        object.scale = node.scale;
        loaded.binding.nodes[i + 1].objects = {i};
    }
    loaded.document.lights = {{.name = "Point", .type = asset::DocLightType::Point, .range = 10},
                              {.name = "Spot",
                               .type = asset::DocLightType::Spot,
                               .range = 10,
                               .innerCone = 0.1f,
                               .outerCone = 0.6f}};
    for (uint32_t i = 0; i < 2; ++i) {
        const auto id = loaded.scene->addLight(
            {.type = i ? engine::LocalLightType::Spot : engine::LocalLightType::Point,
             .position = {1, 2, 3},
             .range = 10,
             .innerCone = 0.1f,
             .outerCone = 0.6f});
        REQUIRE(id);
        loaded.document.nodes.push_back({.name = i ? "Spot" : "Point",
                                         .translation = {1, 2, 3},
                                         .light = i,
                                         .mobility = asset::DocMobility::Movable});
        loaded.document.rootNodes.push_back(i + 3);
        loaded.binding.nodes[i + 3].light = *id;
        loaded.binding.lightNode[engine::sceneLightKey(*id)] = i + 3;
        loaded.lightMobility.push_back(asset::DocMobility::Movable);
    }
    return loaded;
}

//======================================================================================================================
EditorSelection objectSelection(size_t index = 0) {
    return {.sceneId = {"gizmo-fixture"}, .subject = EditorSubject::Object, .index = index};
}

//======================================================================================================================
EditorSelection lightSelection(const engine::LoadedScene& loaded, uint32_t node = 4) {
    return {.sceneId = {"gizmo-fixture"},
            .subject = EditorSubject::LocalLight,
            .lightId = *loaded.binding.nodes[node].light};
}

//======================================================================================================================
DecomposedTransform pose(const engine::SceneObject& object) {
    return {object.position, object.eulerDegrees, object.scale};
}

//======================================================================================================================
void sameBits(glm::vec3 actual, glm::vec3 expected) {
    for (int i = 0; i < 3; ++i)
        CHECK(std::bit_cast<uint32_t>(actual[i]) == std::bit_cast<uint32_t>(expected[i]));
}

//======================================================================================================================
void samePose(const engine::SceneObject& actual, const DecomposedTransform& expected) {
    sameBits(actual.position, expected.position);
    sameBits(actual.eulerDegrees, expected.eulerDegrees);
    sameBits(actual.scale, expected.scale);
}

//======================================================================================================================
asset::SceneDocument exported(const engine::LoadedScene& loaded, const SceneSession& session) {
    auto result = scenes::exportSceneDocument(loaded, *loaded.scene, session.documentState());
    INFO((result ? "exported" : result.error().message));
    REQUIRE(result);
    return *result;
}

//======================================================================================================================
TEST_CASE("gizmo defaults and every selection kind expose only their supported operations",
          "[app][gizmo]") {
    CHECK(GizmoState{}.tool == GizmoTool::Move);
    CHECK(GizmoState{}.space == GizmoSpace::World);
    SceneSession session;
    CHECK_FALSE(gizmoSubject(session, objectSelection(), true));
    auto loaded = fixture();
    session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
    auto selection = objectSelection();
    for (const auto kind :
         {EditorSubject::None, EditorSubject::Camera, EditorSubject::DirectionalLight,
          EditorSubject::Environment, EditorSubject::Group}) {
        selection.subject = kind;
        CHECK_FALSE(gizmoSubject(session, selection, true));
    }
    selection = objectSelection();
    loaded.scene->objects[0].eulerDegrees = {17, 35, -11};
    loaded.scene->objects[0].scale = {2, 3, 4};
    const auto object = gizmoSubject(session, selection, true);
    REQUIRE(object);
    CHECK(object->world == composeTransform(pose(loaded.scene->objects[0])));
    CHECK(object->move);
    CHECK(object->rotate);
    CHECK(object->scale);
    CHECK(object->live);
    CHECK(object->reason.empty());
    for (uint32_t node : {3u, 4u}) {
        const auto light = gizmoSubject(session, lightSelection(loaded, node), true);
        REQUIRE(light);
        CHECK(light->move);
        CHECK(light->rotate == (node == 4));
        CHECK_FALSE(light->scale);
        CHECK(light->live);
    }
    CHECK_FALSE(gizmoSubject(session, objectSelection(999), true));
    selection = lightSelection(loaded);
    REQUIRE(loaded.scene->removeLight(selection.lightId));
    CHECK_FALSE(gizmoSubject(session, selection, true));
}

//======================================================================================================================
TEST_CASE("gizmo objects and lights share pose locks and effective disabled state",
          "[app][gizmo]") {
    auto loaded = fixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
    auto selection = objectSelection();
    std::string expected;
    SECTION("static object") {
        selection.index = 1;
        expected = poseLockReason(PoseLock::Static);
    }
    SECTION("generated object") {
        loaded.binding.objectGeneratorNode[0] = 1;
        expected = poseLockReason(PoseLock::Generated);
    }
    SECTION("animated object") {
        loaded.scene->animation.tracks.push_back({.objectIndex = 0});
        expected = poseLockReason(PoseLock::Animated);
    }
    SECTION("measuring object") {
        session.setMeasurementActive(true);
        expected = poseLockReason(PoseLock::Measuring);
    }
    SECTION("disabled object through ancestor") {
        loaded.document.nodes.push_back({.name = "Parent", .children = {1}});
        loaded.document.rootNodes = {0, 2, 3, 4, 5};
        loaded.binding.nodes.resize(6);
        session.invalidate(*loaded.scene);
        session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
        REQUIRE(session.setNodeEnabled(5, false));
        CHECK(session.objectEnabled(0));
        expected = "Disabled";
    }
    SECTION("static light") {
        selection = lightSelection(loaded);
        loaded.lightMobility[1] = asset::DocMobility::Static;
        expected = poseLockReason(PoseLock::Static);
    }
    SECTION("animated light") {
        selection = lightSelection(loaded);
        loaded.scene->animation.lightTracks.push_back({.light = 1});
        expected = poseLockReason(PoseLock::Animated);
    }
    SECTION("measuring light") {
        selection = lightSelection(loaded);
        session.setMeasurementActive(true);
        expected = poseLockReason(PoseLock::Measuring);
    }
    SECTION("disabled light through ancestor") {
        selection = lightSelection(loaded);
        loaded.document.nodes.push_back({.name = "Parent", .children = {4}});
        loaded.document.rootNodes = {0, 1, 2, 3, 5};
        loaded.binding.nodes.resize(6);
        session.invalidate(*loaded.scene);
        session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
        REQUIRE(session.setNodeEnabled(5, false));
        CHECK(session.localLightEnabled(selection.lightId));
        expected = "Disabled";
    }
    REQUIRE_FALSE(expected.empty());
    const auto subject = gizmoSubject(session, selection, true);
    REQUIRE(subject);
    CHECK_FALSE(subject->live);
    CHECK(subject->reason == expected);
    GizmoDrag drag;
    drag.begin(session, selection);
    CHECK_FALSE(drag.active());
    const auto generation = session.editGeneration();
    const auto refused =
        applyGizmo(session, selection, glm::translate(subject->world, glm::vec3(1)));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message == expected);
    CHECK(session.editGeneration() == generation);
    CHECK(gizmoSubject(session, selection, true)->world == subject->world);
}

//======================================================================================================================
TEST_CASE("playback is inert and generated lights retain session-only gizmo edits",
          "[app][gizmo]") {
    auto loaded = fixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
    for (const auto& selection : {objectSelection(), lightSelection(loaded)}) {
        const auto subject = gizmoSubject(session, selection, false);
        REQUIRE(subject);
        CHECK_FALSE(subject->live);
        CHECK(subject->reason == "Stop playback to move this");
    }
    const auto selection = lightSelection(loaded);
    const auto key = engine::sceneLightKey(selection.lightId);
    loaded.binding.lightNode.erase(key);
    loaded.binding.nodes[4].light.reset();
    loaded.binding.lightGeneratorNode[key] = 4;
    loaded.scene->animation.lightTracks.push_back({.light = 1});
    const auto subject = gizmoSubject(session, selection, true);
    REQUIRE(subject);
    CHECK(subject->live);
    const auto generation = session.editGeneration();
    REQUIRE(applyGizmo(session, selection, glm::translate(subject->world, glm::vec3(2))));
    CHECK(session.editGeneration() == generation);
}

//======================================================================================================================
TEST_CASE("gizmo rejects invalid matrices and clamps finite decomposed object scales",
          "[app][gizmo]") {
    auto loaded = fixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
    const auto selection = objectSelection();
    const auto original = pose(loaded.scene->objects[0]);
    for (const auto scale :
         {glm::vec3(0, 2, 3), glm::vec3(-2, 3, 4), glm::vec3(200, 0.001f, 1), glm::vec3(0)}) {
        CAPTURE(scale.x, scale.y, scale.z);
        const auto matrix = composeTransform({.eulerDegrees = {90, 15, 25}, .scale = scale});
        REQUIRE(applyGizmo(session, selection, matrix));
        const auto actual = pose(loaded.scene->objects[0]);
        for (int i = 0; i < 3; ++i) {
            CHECK(std::isfinite(actual.position[i]));
            CHECK(std::isfinite(actual.eulerDegrees[i]));
            CHECK(std::isfinite(actual.scale[i]));
            CHECK(actual.scale[i] >= 0.01f);
            CHECK(actual.scale[i] <= 100.0f);
        }
    }
    for (float pitch : {-90.0f, 90.0f})
        REQUIRE(
            applyGizmo(session, selection, composeTransform({.eulerDegrees = {pitch, 15, 25}})));
    REQUIRE(session.editObject(0, original));
    for (int bad = 0; bad < 5; ++bad) {
        auto matrix = composeTransform(original);
        if (bad == 0)
            matrix[0][0] = std::numeric_limits<float>::quiet_NaN();
        if (bad == 1)
            matrix[3][0] = std::numeric_limits<float>::infinity();
        if (bad == 2)
            matrix[1][0] = 0.5f;
        if (bad == 3)
            matrix[0][3] = 0.5f;
        if (bad == 4)
            matrix = composeTransform({.scale = {0, 0, 1}});
        const auto generation = session.editGeneration();
        CHECK_FALSE(applyGizmo(session, selection, matrix));
        samePose(loaded.scene->objects[0], original);
        CHECK(session.editGeneration() == generation);
    }
}

//======================================================================================================================
TEST_CASE("spot world and rotated direction share local minus Z and point moves preserve direction",
          "[app][gizmo]") {
    auto loaded = fixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
    const auto selection = lightSelection(loaded);
    for (const auto direction : {glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), glm::vec3(0, -1, 0),
                                 glm::normalize(glm::vec3(1, 2, -3))}) {
        auto light = *loaded.scene->light(selection.lightId);
        light.direction = direction;
        REQUIRE(session.editLocalLight(selection.lightId, light));
        const auto subject = gizmoSubject(session, selection, true);
        REQUIRE(subject);
        CHECK(glm::length(-glm::vec3(subject->world[2]) - direction) < 0.000001f);
        const auto rotated = glm::rotate(glm::mat4(1), 0.7f, glm::vec3(0, 1, 0)) * subject->world;
        REQUIRE(applyGizmo(session, selection, rotated));
        const auto* actual = loaded.scene->light(selection.lightId);
        CHECK(glm::length(actual->direction - glm::normalize(-glm::vec3(rotated[2]))) < 0.000001f);
        CHECK(std::abs(glm::length(actual->direction) - 1) < 0.000001f);
    }
    const auto point = lightSelection(loaded, 3);
    const auto direction = loaded.scene->light(point.lightId)->direction;
    REQUIRE(applyGizmo(session, point,
                       composeTransform({.position = {7, 8, 9}, .eulerDegrees = {90, 0, 0}})));
    CHECK(loaded.scene->light(point.lightId)->position == glm::vec3(7, 8, 9));
    sameBits(loaded.scene->light(point.lightId)->direction, direction);
    auto invalid = glm::mat4(1);
    invalid[2] = glm::vec4(0);
    const auto before = *loaded.scene->light(selection.lightId);
    CHECK_FALSE(applyGizmo(session, selection, invalid));
    sameBits(loaded.scene->light(selection.lightId)->position, before.position);
    sameBits(loaded.scene->light(selection.lightId)->direction, before.direction);
}

//======================================================================================================================
TEST_CASE("gizmo and inspector produce identical exported and saved object and spot bytes",
          "[app][gizmo]") {
    auto gizmo = fixture();
    auto inspector = fixture();
    SceneSession gizmoSession, inspectorSession;
    gizmoSession.activate(gizmo, SceneActivationMotion::PreserveLoadedMotion);
    inspectorSession.activate(inspector, SceneActivationMotion::PreserveLoadedMotion);
    SECTION("object") {
        REQUIRE(applyGizmo(gizmoSession, objectSelection(),
                           composeTransform({.position = {4, 5, 6}, .scale = {2, 3, 4}})));
        REQUIRE(inspectorSession.editObject(0, pose(gizmo.scene->objects[0])));
    }
    SECTION("spot") {
        REQUIRE(
            applyGizmo(gizmoSession, lightSelection(gizmo),
                       composeTransform({.position = {4, 5, 6}, .eulerDegrees = {25, 40, -10}})));
        const auto edited = *gizmo.scene->light(lightSelection(gizmo).lightId);
        REQUIRE(inspectorSession.editLocalLight(lightSelection(inspector).lightId, edited));
    }
    const auto fromGizmo = exported(gizmo, gizmoSession);
    const auto fromInspector = exported(inspector, inspectorSession);
    CHECK(asset::sceneDocumentJson(fromGizmo, "same.scene.bin") ==
          asset::sceneDocumentJson(fromInspector, "same.scene.bin"));
    CHECK(asset::sceneDocumentBuffer(fromGizmo) == asset::sceneDocumentBuffer(fromInspector));
    CHECK(scenes::documentDirty(gizmo.document, fromGizmo));
    const auto root = std::filesystem::temp_directory_path() /
                      ("lmx-gizmo-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    for (const auto& [name, document] :
         {std::pair{"gizmo", fromGizmo}, std::pair{"inspector", fromInspector}}) {
        const auto path = root / name / "same.scene.gltf";
        std::filesystem::create_directories(path.parent_path());
        const auto saved = asset::saveSceneDocument(document, path);
        INFO((saved ? "saved" : saved.error().message));
        REQUIRE(saved);
        const auto reread = asset::readSceneDocument(path);
        REQUIRE(reread);
        CHECK_FALSE(scenes::documentDirty(document, *reread));
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "gizmo")) {
        if (!entry.is_regular_file())
            continue;
        const auto relative = std::filesystem::relative(entry.path(), root / "gizmo");
        const auto first = readWholeFile(entry.path());
        const auto second = readWholeFile(root / "inspector" / relative);
        REQUIRE(first);
        REQUIRE(second);
        CHECK(*first == *second);
    }
    std::filesystem::remove_all(root);
}

//======================================================================================================================
TEST_CASE("gizmo cancel restores captured primitive pose bits and keeps unrelated spot edits",
          "[app][gizmo]") {
    auto loaded = fixture();
    SceneSession session;
    SECTION("imported primitives") {
        loaded.objectMobility[1] = asset::DocMobility::Movable;
        loaded.binding.objectImportedNode = {0, 0};
        loaded.binding.importedNodes.push_back({.assetRoot = 1, .objects = {0, 1}});
        const DecomposedTransform original{{-0.0f, 2, 3}, {-0.0f, 30, 90}, {2, 3, 4}};
        for (auto& object : loaded.scene->objects) {
            object.position = original.position;
            object.eulerDegrees = original.eulerDegrees;
            object.scale = original.scale;
        }
        session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
        GizmoDrag drag;
        CHECK_FALSE(drag.active());
        drag.begin(session, objectSelection());
        REQUIRE(drag.active());
        REQUIRE(applyGizmo(session, objectSelection(), composeTransform({.position = {7, 8, 9}})));
        CHECK(loaded.scene->objects[1].position == glm::vec3(7, 8, 9));
        REQUIRE(drag.cancel(session));
        CHECK_FALSE(drag.active());
        for (const auto& object : loaded.scene->objects)
            samePose(object, original);
    }
    SECTION("spot with non-unit captured direction") {
        session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
        const auto selection = lightSelection(loaded);
        auto original = *loaded.scene->light(selection.lightId);
        original.position = {-0.0f, 2, 3};
        original.direction = {-0.0f, -2, -3};
        REQUIRE(session.editLocalLight(selection.lightId, original));
        GizmoDrag drag;
        drag.begin(session, selection);
        REQUIRE(drag.active());
        REQUIRE(applyGizmo(session, selection, composeTransform({.position = {7, 8, 9}})));
        auto edited = *loaded.scene->light(selection.lightId);
        edited.intensity = 11;
        edited.colour = {0.2f, 0.3f, 0.4f};
        REQUIRE(session.editLocalLight(selection.lightId, edited));
        REQUIRE(drag.cancel(session));
        CHECK_FALSE(drag.active());
        sameBits(loaded.scene->light(selection.lightId)->position, original.position);
        sameBits(loaded.scene->light(selection.lightId)->direction, original.direction);
        CHECK(loaded.scene->light(selection.lightId)->intensity == 11);
        CHECK(loaded.scene->light(selection.lightId)->colour == edited.colour);
    }
}

//======================================================================================================================
TEST_CASE("gizmo drag rejects changed selection and activation including reused scene storage",
          "[app][gizmo]") {
    auto loaded = fixture();
    auto other = fixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
    const auto selection = objectSelection();
    GizmoDrag drag;
    drag.begin(session, selection);
    REQUIRE(drag.matches(selection));
    SECTION("selection identity") {
        auto changed = selection;
        changed.index = 1;
        CHECK_FALSE(drag.matches(changed));
        changed = selection;
        changed.sceneId.key = "different";
        CHECK_FALSE(drag.matches(changed));
        changed = selection;
        changed.subject = EditorSubject::Camera;
        CHECK_FALSE(drag.matches(changed));
        changed = selection;
        changed.node = 1;
        CHECK_FALSE(drag.matches(changed));
        changed = selection;
        changed.importedNode = 1;
        CHECK_FALSE(drag.matches(changed));
        drag.end();
        const auto before = pose(loaded.scene->objects[0]);
        REQUIRE(drag.cancel(session));
        samePose(loaded.scene->objects[0], before);
    }
    SECTION("away then back") {
        session.activate(other, SceneActivationMotion::PreserveLoadedMotion);
        session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
        CHECK_FALSE(drag.matches(selection));
        const auto before = pose(loaded.scene->objects[0]);
        CHECK_FALSE(drag.cancel(session));
        samePose(loaded.scene->objects[0], before);
        CHECK_FALSE(drag.active());
    }
    SECTION("same ID replacement at same address") {
        auto* storage = loaded.scene.get();
        session.invalidate(*storage);
        CHECK_FALSE(drag.matches(selection));
        std::destroy_at(storage);
        std::construct_at(storage);
        storage->objects.resize(2);
        storage->objects[0].position = {44, 55, 66};
        loaded.binding.lightNode.clear();
        loaded.binding.nodes[3].light.reset();
        loaded.binding.nodes[4].light.reset();
        session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
        CHECK_FALSE(drag.matches(selection));
        CHECK_FALSE(drag.cancel(session));
        CHECK(storage->objects[0].position == glm::vec3(44, 55, 66));
        CHECK_FALSE(drag.active());
    }
    SECTION("inactive invalidation keeps current drag") {
        session.invalidate(*other.scene);
        CHECK(drag.matches(selection));
        REQUIRE(drag.cancel(session));
    }
    SECTION("foreign session cannot restore") {
        SceneSession foreign;
        foreign.activate(other, SceneActivationMotion::PreserveLoadedMotion);
        const auto before = pose(other.scene->objects[0]);
        CHECK_FALSE(drag.cancel(foreign));
        CHECK_FALSE(drag.active());
        samePose(other.scene->objects[0], before);
    }
}

//======================================================================================================================
TEST_CASE("gizmo cancellation after a lock change ends without mutation", "[app][gizmo]") {
    auto loaded = fixture();
    SceneSession session;
    session.activate(loaded, SceneActivationMotion::PreserveLoadedMotion);
    const auto selection = objectSelection();
    GizmoDrag drag;
    drag.begin(session, selection);
    REQUIRE(applyGizmo(session, selection, composeTransform({.position = {7, 8, 9}})));
    SECTION("measurement") {
        session.setMeasurementActive(true);
    }
    SECTION("disabled") {
        REQUIRE(session.setObjectEnabled(0, false));
    }
    SECTION("static") {
        loaded.objectMobility[0] = asset::DocMobility::Static;
    }
    CHECK_FALSE(drag.cancel(session));
    CHECK_FALSE(drag.active());
    CHECK(loaded.scene->objects[0].position == glm::vec3(7, 8, 9));
    REQUIRE(drag.cancel(session));
    CHECK(loaded.scene->objects[0].position == glm::vec3(7, 8, 9));
}

//======================================================================================================================
TEST_CASE("gizmo finite RH NO projection aligns with reversed camera within one hundredth pixel",
          "[app][gizmo]") {
    engine::Camera camera;
    camera.position = {2, 3, 4};
    camera.yaw = 0.4f;
    camera.pitch = -0.2f;
    camera.nearZ = 0.17f;
    camera.farZ = 7;
    camera.fovY = glm::radians(73.0f);
    for (const auto size : {glm::vec2(1280, 720), glm::vec2(720, 1280), glm::vec2(3840, 2160)}) {
        const float aspect = size.x / size.y;
        const auto gizmo = gizmoProjection(camera, aspect);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                CHECK(std::isfinite(gizmo[c][r]));
        const auto nearClip = gizmo * glm::vec4(0, 0, -camera.nearZ, 1);
        const auto farClip = gizmo * glm::vec4(0, 0, -1000, 1);
        CHECK(std::abs(nearClip.z / nearClip.w + 1) < 0.000001f);
        CHECK(std::abs(farClip.z / farClip.w - 1) < 0.000001f);
        const auto view = camera.viewMatrix();
        for (float depth : {0.2f, 1.0f, 10.0f, 999.0f})
            for (const auto offset :
                 {glm::vec2(0), glm::vec2(-0.3f, 0.2f), glm::vec2(0.4f, -0.1f)}) {
                const auto world = glm::inverse(view) * glm::vec4(offset * depth, -depth, 1);
                const auto a = gizmo * view * world;
                const auto b = camera.projectionMatrix(aspect) * view * world;
                const auto delta =
                    glm::abs((glm::vec2(a) / a.w - glm::vec2(b) / b.w) * size * 0.5f);
                CHECK(delta.x <= 0.01f);
                CHECK(delta.y <= 0.01f);
            }
    }
}

} // namespace
