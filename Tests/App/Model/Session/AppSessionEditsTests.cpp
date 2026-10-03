#include "App/Model/Session/SessionEdits.h"
#include "App/Model/Session/SessionQueries.h"

#include "Engine/Asset/Document/Orientation.h"
#include "Engine/Asset/Model/JsonTokens.h"
#include "Scenes/SceneDocumentExport.h"
#include "Scenes/SceneLibrary.h"
#include "Support/EngineTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <format>

using namespace lmx;

namespace {

//======================================================================================================================
asset::JsonNode arguments(std::string json) {
    const auto parsed = asset::JsonTokens::parse(std::move(json));
    REQUIRE(parsed);
    return parsed->root();
}

//======================================================================================================================
bool dirty(const app::SceneSession& session) {
    const auto exported = scenes::exportSceneDocument(*session.loadedScene(), session.scene(),
                                                      session.documentState());
    REQUIRE(exported);
    return scenes::documentDirty(session.loadedScene()->document, *exported);
}

//======================================================================================================================
engine::LoadedScene editableFixture(engine::LightId& lightId) {
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.document.name = "Bridge fixture";
    loaded.objectMobility = {asset::DocMobility::Movable};
    loaded.lightMobility = {asset::DocMobility::Movable};
    loaded.document.nodes = {
        {.name = "Camera", .translation = {0, 1, 4}, .camera = 0},
        {.name = "Asset",
         .asset = asset::DocAsset{.uri = "fixture.gltf", .sha256 = std::string(64, '0')}},
        {.name = "Lamp", .light = 0}};
    loaded.document.nodes[1].mobility = asset::DocMobility::Movable;
    loaded.document.nodes[2].mobility = asset::DocMobility::Movable;
    loaded.document.cameras = {{.name = "Lens"}};
    loaded.document.lights = {{.name = "Lamp",
                               .type = asset::DocLightType::Spot,
                               .range = 8.0f,
                               .innerCone = 0.2f,
                               .outerCone = 0.7f}};
    loaded.document.rootNodes = {0, 1, 2};
    loaded.binding.nodes.resize(3);
    loaded.binding.nodes[0].camera = true;
    loaded.binding.nodes[1].objects = {0};
    loaded.binding.importedNodes = {
        {.assetRoot = 1, .sourceNode = 0, .name = "Part", .objects = {0}}};
    loaded.binding.objectNode = {1};
    loaded.binding.objectImportedNode = {0};
    loaded.binding.objectGeneratorNode = {engine::kGeneratedNode};
    loaded.binding.generatedObjectEnabled = {true};
    loaded.scene->objects.resize(1);
    loaded.scene->objects[0].name = "Part";
    engine::LocalLight light;
    light.type = engine::LocalLightType::Spot;
    light.position = {1, 2, 3};
    light.range = 8;
    light.innerCone = 0.2f;
    light.outerCone = 0.7f;
    const auto added = loaded.scene->addLight(light);
    REQUIRE(added);
    lightId = *added;
    loaded.binding.nodes[2].light = lightId;
    loaded.binding.lightNode[engine::sceneLightKey(lightId)] = 2;
    loaded.scene->initialCamera = {{0, 1, 4}, 0, 0, 1, 0.1f, 100};
    return loaded;
}

//======================================================================================================================
app::SceneTreeView editableTree(engine::LightId lightId) {
    app::SceneTreeView tree;
    tree.rows = {
        {.subject = app::EditorSubject::Camera, .node = 0, .label = "Camera"},
        {.subject = app::EditorSubject::Group, .node = 1, .label = "Asset"},
        {.subject = app::EditorSubject::Object,
         .index = 0,
         .node = 1,
         .importedNode = 0,
         .label = "Part"},
        {.subject = app::EditorSubject::LocalLight, .lightId = lightId, .node = 2, .label = "Lamp"},
        {.subject = app::EditorSubject::Environment, .label = "Environment"}};
    return tree;
}

} // namespace

//======================================================================================================================
TEST_CASE("bridge edit arguments require typed canonical JSON values", "[app][session-edits]") {
    const auto parsed = app::parseEdits(arguments(R"({"summary":"Review", "edits":[
        {"subject":"environment","field":"shadowFilter","value":"PCSS"}]})"));
    REQUIRE(parsed);
    REQUIRE(parsed->size() == 1);
    CHECK((*parsed)[0].value == R"("PCSS")");
    CHECK_FALSE(app::parseEdits(arguments(R"({"summary":"Review","edits":[
        {"subject":"environment","field":"shadowFilter","value":"PCSS","extra":1}]})")));
    CHECK_FALSE(app::parseEdits(arguments(R"({"summary":"Review","edits":[]})")));
}

//======================================================================================================================
TEST_CASE("bridge preview rejects invalid batches without changing live look or camera",
          "[app][session-edits]") {
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    loaded.document.nodes = {{.name = "Camera", .camera = 0}};
    loaded.document.cameras = {{.name = "Lens"}};
    loaded.document.rootNodes = {0};
    loaded.binding.nodes.resize(1);
    loaded.scene->initialCamera = {{0, 1, 4}, 0, 0, 1, 0.1f, 100};
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    app::SceneTreeView tree;
    tree.rows = {{.subject = app::EditorSubject::Environment, .label = "Environment"},
                 {.subject = app::EditorSubject::Camera, .node = 0, .label = "Camera"}};
    const auto originalCamera = session.camera();
    const auto originalLook = session.look();
    const std::vector<app::ProposalEdit> bad{{"environment", "shadowFilter", R"("pcss")"},
                                             {"node:0", "pitch", "9"}};
    CHECK_FALSE(app::previewEdits(session, tree, bad));
    CHECK_FALSE(app::applyEdits(session, tree, bad));
    CHECK(session.look() == originalLook);
    CHECK(session.camera().position == originalCamera.position);
    CHECK(session.camera().yaw == originalCamera.yaw);
    CHECK(session.camera().pitch == originalCamera.pitch);
    CHECK_FALSE(dirty(session));
    const std::vector<app::ProposalEdit> good{{"environment", "shadowFilter", R"("pcss")"},
                                              {"node:0", "position", "[1,2,3]"}};
    REQUIRE(app::previewEdits(session, tree, good));
    CHECK_FALSE(dirty(session));
    REQUIRE(app::applyEdits(session, tree, good));
    CHECK(dirty(session));
    CHECK(session.camera().position == originalCamera.position);
    CHECK(session.camera().yaw == originalCamera.yaw);
    CHECK(session.camera().pitch == originalCamera.pitch);
    CHECK(session.camera().fovY == originalCamera.fovY);
}

//======================================================================================================================
TEST_CASE("bridge preview rejects generated subjects and stale light ids", "[app][session-edits]") {
    engine::LoadedScene loaded{.scene = std::make_unique<engine::Scene>()};
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    app::SceneTreeView tree;
    tree.rows = {{.subject = app::EditorSubject::Object, .index = 0, .generated = true},
                 {.subject = app::EditorSubject::LocalLight,
                  .lightId = {.slot = 1, .generation = 2, .store = 3}}};
    const std::vector<app::ProposalEdit> generated{{"object:0", "enabled", "false"}};
    const std::vector<app::ProposalEdit> stale{{"light:1:1", "intensity", "2"}};
    CHECK_FALSE(app::previewEdits(session, tree, generated));
    CHECK_FALSE(app::previewEdits(session, tree, stale));
}

//======================================================================================================================
TEST_CASE("bridge attribution is keyed by field and remembers the client", "[app][session-edits]") {
    app::SessionAttribution attribution;
    attribution.mark("node:0/yaw", "Fixture");
    CHECK(attribution.has("node:0/yaw"));
    CHECK_FALSE(attribution.has("node:0/pitch"));
    CHECK(attribution.client("node:0/yaw") == "Fixture");
    attribution.clear();
    CHECK_FALSE(attribution.has("node:0/yaw"));
    CHECK(app::sessionAppliedProvenance("Fixture").kind == app::Provenance::AgentApplied);
}

//======================================================================================================================
TEST_CASE("every supported bridge field exports a dirty document", "[app][session-edits]") {
    const std::vector<app::ProposalEdit> cases = {
        {"node:1", "enabled", "false"},
        {"imported:0", "enabled", "false"},
        {"node:2", "enabled", "false"},
        {"imported:0", "position", "[2,3,4]"},
        {"imported:0", "eulerDegrees", "[0,45,0]"},
        {"imported:0", "scale", "[1.2,1.2,1.2]"},
        {"node:2", "position", "[2,3,4]"},
        {"node:2", "color", "[0.5,0.6,0.7]"},
        {"node:2", "intensity", "2"},
        {"node:2", "range", "9"},
        {"node:2", "direction", "[0,1,0]"},
        {"node:2", "innerCone", "15"},
        {"node:2", "outerCone", "50"},
        {"environment", "exposure", R"({"ev":1})"},
        {"environment", "bloom", R"({"intensity":0.4})"},
        {"environment", "shadowFilter", R"("pcss")"},
        {"node:0", "position", "[2,3,4]"},
        {"node:0", "yaw", "0.5"},
        {"node:0", "pitch", "0.25"},
    };
    for (const auto& edit : cases) {
        DYNAMIC_SECTION(edit.subject << "/" << edit.field) {
            engine::LightId id;
            auto loaded = editableFixture(id);
            app::SceneSession session;
            session.activate(loaded, app::SceneActivationMotion::Reset);
            const auto baseline =
                scenes::exportSceneDocument(loaded, session.scene(), session.documentState());
            REQUIRE(baseline);
            loaded.document = *baseline;
            const auto tree = editableTree(id);
            const std::vector<app::ProposalEdit> batch{edit};
            const auto generation = session.editGeneration();
            const auto camera = session.camera();
            const auto preview = app::previewEdits(session, tree, batch);
            REQUIRE(preview);
            REQUIRE(preview->size() == 1);
            CHECK_FALSE((*preview)[0].before.empty());
            CHECK_FALSE((*preview)[0].after.empty());
            CHECK_FALSE(dirty(session));
            CHECK(session.editGeneration() == generation);
            REQUIRE(app::applyEdits(session, tree, batch));
            CHECK(dirty(session));
            CHECK(session.camera().position == camera.position);
            CHECK(session.camera().yaw == camera.yaw);
            CHECK(session.camera().pitch == camera.pitch);
        }
    }
}

//======================================================================================================================
TEST_CASE("stale full light identity aborts a batch before an earlier valid edit",
          "[app][session-edits]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    auto tree = editableTree(id);
    REQUIRE(session.scene().removeLight(id));
    const auto generation = session.editGeneration();
    const auto look = session.look();
    const std::vector<app::ProposalEdit> edits{{"environment", "shadowFilter", R"("pcss")"},
                                               {"node:2", "intensity", "2"}};
    CHECK_FALSE(app::previewEdits(session, tree, edits));
    CHECK_FALSE(app::applyEdits(session, tree, edits));
    CHECK(session.look() == look);
    CHECK(session.editGeneration() == generation);
}

//======================================================================================================================
TEST_CASE("foreign-store and newer-generation light identities abort the whole batch",
          "[app][session-edits]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto look = session.look();
    const auto intensity = session.scene().light(id)->intensity;
    const auto generation = session.editGeneration();
    auto foreign = id;
    ++foreign.store;
    auto newer = id;
    ++newer.generation;
    for (const auto invalidId : {foreign, newer}) {
        auto tree = editableTree(id);
        tree.rows[3].lightId = invalidId;
        const std::vector<app::ProposalEdit> edits{{"environment", "shadowFilter", R"("pcss")"},
                                                   {"node:2", "intensity", "2"}};
        CHECK_FALSE(app::previewEdits(session, tree, edits));
        CHECK_FALSE(app::applyEdits(session, tree, edits));
        CHECK(session.look() == look);
        CHECK(session.scene().light(id)->intensity == intensity);
        CHECK(session.editGeneration() == generation);
    }
}

//======================================================================================================================
TEST_CASE("animated imported object transforms abort the whole batch", "[app][session-edits]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    loaded.binding.importedNodes[0].animated = true;
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto tree = editableTree(id);
    const auto look = session.look();
    const auto position = session.scene().objects[0].position;
    const auto generation = session.editGeneration();
    const std::vector<app::ProposalEdit> edits{{"environment", "shadowFilter", R"("pcss")"},
                                               {"imported:0", "position", "[2,3,4]"}};
    CHECK_FALSE(app::previewEdits(session, tree, edits));
    CHECK_FALSE(app::applyEdits(session, tree, edits));
    CHECK(session.look() == look);
    CHECK(session.scene().objects[0].position == position);
    CHECK(session.editGeneration() == generation);
}

//======================================================================================================================
TEST_CASE("a non-active camera node aborts the whole batch", "[app][session-edits]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    loaded.document.nodes.push_back({.name = "Other Camera", .camera = 1});
    loaded.document.cameras.push_back({.name = "Other Lens"});
    loaded.document.rootNodes.push_back(3);
    loaded.binding.nodes.resize(4);
    loaded.binding.nodes[3].camera = true;
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    auto tree = editableTree(id);
    tree.rows.push_back(
        {.subject = app::EditorSubject::Camera, .node = 3, .label = "Other Camera"});
    const auto look = session.look();
    const auto camera = session.authoredSceneCamera();
    const auto generation = session.editGeneration();
    const std::vector<app::ProposalEdit> edits{{"environment", "shadowFilter", R"("pcss")"},
                                               {"node:3", "yaw", "0.5"}};
    CHECK_FALSE(app::previewEdits(session, tree, edits));
    CHECK_FALSE(app::applyEdits(session, tree, edits));
    CHECK(session.look() == look);
    CHECK(session.authoredSceneCamera().yaw == camera.yaw);
    CHECK(session.editGeneration() == generation);
}

//======================================================================================================================
TEST_CASE("bridge preview shows final normalized and combined values", "[app][session-edits]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto tree = editableTree(id);
    const std::vector<app::ProposalEdit> edits{
        {"node:0", "yaw", "7"},
        {"node:2", "innerCone", "15"},
        {"node:2", "outerCone", "50"},
        {"environment", "exposure", R"({"ev":1})"},
        {"environment", "exposure", R"({"autoEnabled":true})"}};
    const auto preview = app::previewEdits(session, tree, edits);
    REQUIRE(preview);
    REQUIRE(preview->size() == 4);
    const auto yaw = asset::JsonTokens::parse((*preview)[0].after);
    REQUIRE(yaw);
    CHECK(yaw->root().asFloat() == asset::unwrapYaw(0.0f, 7.0f));
    const auto inner = asset::JsonTokens::parse((*preview)[1].after);
    REQUIRE(inner);
    CHECK(inner->root().asFloat() == glm::degrees(glm::radians(15.0f)));
    const auto outer = asset::JsonTokens::parse((*preview)[2].after);
    REQUIRE(outer);
    CHECK(outer->root().asFloat() == glm::degrees(glm::radians(50.0f)));
    const auto exposureBefore = asset::JsonTokens::parse((*preview)[3].before);
    const auto exposureAfter = asset::JsonTokens::parse((*preview)[3].after);
    REQUIRE(exposureBefore);
    REQUIRE(exposureAfter);
    CHECK(exposureBefore->root().find("ev")->asFloat() == 0.0f);
    CHECK(exposureAfter->root().find("ev")->asFloat() == 1.0f);
    CHECK(exposureAfter->root().find("autoEnabled")->asBool() == true);
    const std::vector<app::ProposalEdit> unchanged{{"node:0", "yaw", "0"}};
    REQUIRE(app::previewEdits(session, tree, unchanged));
    CHECK(app::previewEdits(session, tree, unchanged)->empty());
    const std::vector<app::ProposalEdit> fullTurn{{"node:0", "yaw", "6.283185307179586"}};
    REQUIRE(app::previewEdits(session, tree, fullTurn));
    CHECK(app::previewEdits(session, tree, fullTurn)->empty());
    const std::vector<app::ProposalEdit> enabledLastWrite{{"node:1", "enabled", "false"},
                                                          {"node:1", "enabled", "true"}};
    REQUIRE(app::previewEdits(session, tree, enabledLastWrite));
    CHECK(app::previewEdits(session, tree, enabledLastWrite)->empty());
    const std::vector<app::ProposalEdit> mixed{{"node:0", "yaw", "0"},
                                               {"environment", "shadowFilter", R"("pcss")"}};
    const auto keys = app::changedEditKeys(session, tree, mixed);
    REQUIRE(keys);
    REQUIRE(keys->size() == 1);
    CHECK((*keys)[0] == "environment/shadowFilter");
    REQUIRE(app::applyEdits(session, tree, edits));
    CHECK(inner->root().asFloat() == glm::degrees(session.scene().light(id)->innerCone));
    CHECK(outer->root().asFloat() == glm::degrees(session.scene().light(id)->outerCone));
    CHECK(yaw->root().asFloat() == session.authoredSceneCamera().yaw);
}

//======================================================================================================================
TEST_CASE("bridge edit effects distinguish authored camera from rendered scene changes",
          "[app][session-edits]") {
    engine::LightId id;
    const auto tree = editableTree(id);
    const std::vector<app::ProposalEdit> camera{{"node:0", "yaw", "0.5"}};
    const std::vector<app::ProposalEdit> object{{"imported:0", "position", "[2,3,4]"}};
    CHECK_FALSE(app::sessionEditNeedsCameraCut(tree, camera));
    CHECK(app::sessionEditNeedsCameraCut(tree, object));
}

//======================================================================================================================
TEST_CASE("bridge rejects combined constraint failures and wrong value shapes atomically",
          "[app][session-edits]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto tree = editableTree(id);
    const auto look = session.look();
    const auto light = *session.scene().light(id);
    const auto generation = session.editGeneration();
    const std::vector<std::vector<app::ProposalEdit>> invalid = {
        {{"environment", "shadowFilter", R"("pcss")"},
         {"node:2", "innerCone", "70"},
         {"node:2", "outerCone", "60"}},
        {{"environment", "exposure", R"({"ev":"wrong"})"}},
        {{"environment", "bloom", R"({"unknown":1})"}},
        {{"environment", "exposure", R"({"ev":1,"ev":2})"}},
        {{"node:2", "range", R"("9")"}},
        {{"imported:0", "scale", "[0,1,1]"}},
    };
    for (const auto& edits : invalid) {
        CHECK_FALSE(app::previewEdits(session, tree, edits));
        CHECK_FALSE(app::applyEdits(session, tree, edits));
        CHECK(session.look() == look);
        CHECK(session.scene().light(id)->range == light.range);
        CHECK(session.scene().light(id)->innerCone == light.innerCone);
        CHECK(session.editGeneration() == generation);
    }
    auto generated = tree;
    generated.rows[2].generated = true;
    const std::vector<app::ProposalEdit> enabled{{"imported:0", "enabled", "false"}};
    CHECK_FALSE(app::previewEdits(session, generated, enabled));
    CHECK(session.importedNodeEnabled(0));
}

//======================================================================================================================
TEST_CASE("bridge look edits outside the reader's ranges change nothing", "[app][session-edits]") {
    const std::vector<app::ProposalEdit> cases = {
        {"environment", "exposure", R"({"lowPercentile":-1})"},
        {"environment", "exposure", R"({"lowPercentile":95})"},
        {"environment", "exposure", R"({"highPercentile":101})"},
        {"environment", "exposure", R"({"targetGrey":0})"},
        {"environment", "exposure", R"({"targetGrey":-1})"},
        {"environment", "exposure", R"({"evMin":9})"},
        {"environment", "exposure", R"({"evMax":-9})"},
        {"environment", "exposure", R"({"adaptUpStopsPerSecond":-1})"},
        {"environment", "exposure", R"({"adaptDownStopsPerSecond":-1})"},
        {"environment", "bloom", R"({"threshold":-1})"},
        {"environment", "bloom", R"({"intensity":-1})"},
    };
    for (const auto& edit : cases) {
        DYNAMIC_SECTION(edit.field << " " << edit.value) {
            engine::LightId id;
            auto loaded = editableFixture(id);
            app::SceneSession session;
            session.activate(loaded, app::SceneActivationMotion::Reset);
            const auto tree = editableTree(id);
            const auto look = session.look();
            const auto generation = session.editGeneration();
            // A valid edit earlier in the batch must not survive the refused one.
            const std::vector<app::ProposalEdit> batch{{"environment", "shadowFilter", R"("pcss")"},
                                                       edit};
            const auto preview = app::previewEdits(session, tree, batch);
            REQUIRE_FALSE(preview);
            CHECK(preview.error().starts_with("Invalid look value environment/"));
            CHECK_FALSE(app::applyEdits(session, tree, batch));
            CHECK(session.look() == look);
            CHECK(session.editGeneration() == generation);
        }
    }
    engine::LightId id;
    auto loaded = editableFixture(id);
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const std::vector<app::ProposalEdit> boundary{
        {"environment", "exposure",
         R"({"lowPercentile":0,"highPercentile":100,"evMin":2,"evMax":2,
             "adaptUpStopsPerSecond":0,"adaptDownStopsPerSecond":0})"},
        {"environment", "bloom", R"({"threshold":0,"intensity":0})"}};
    CHECK(app::applyEdits(session, editableTree(id), boundary));
}

//======================================================================================================================
TEST_CASE("a reviewed bridge proposal goes stale when the operator edits what it shows",
          "[app][session-edits]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto tree = editableTree(id);
    const std::vector<app::ProposalEdit> edits{{"environment", "exposure", R"({"ev":1})"},
                                               {"node:2", "intensity", "2"}};
    const auto reviewed = app::previewEdits(session, tree, edits);
    REQUIRE(reviewed);
    REQUIRE(reviewed->size() == 2);
    CHECK(app::reviewedEditsCurrent(session, tree, edits, *reviewed));

    SECTION("an edit to another subject leaves the rows current") {
        auto look = session.look();
        look.shadowFilter = asset::ShadowFilter::PCSS;
        session.editLook(look);
        // A saved file hash takes no part: Save and Save As replace it without staling a card.
        session.loadedScene()->hash = "hash after Save";
        CHECK(app::reviewedEditsCurrent(session, tree, edits, *reviewed));
    }
    SECTION("an edit to another exposure field changes the merged row") {
        auto look = session.look();
        look.exposure.compensationEv = 0.5f;
        session.editLook(look);
        const auto current = app::reviewedEditsCurrent(session, tree, edits, *reviewed);
        REQUIRE_FALSE(current);
        CHECK(current.error() == "stale: the scene changed after the proposal was reviewed");
    }
    SECTION("an edit to the proposed field changes its before value") {
        auto light = *session.scene().light(id);
        light.intensity = 5.0f;
        REQUIRE(session.editLocalLight(id, light));
        CHECK_FALSE(app::reviewedEditsCurrent(session, tree, edits, *reviewed));
    }
    SECTION("an edit that reaches the proposed value drops its row") {
        auto light = *session.scene().light(id);
        light.intensity = 2.0f;
        REQUIRE(session.editLocalLight(id, light));
        CHECK_FALSE(app::reviewedEditsCurrent(session, tree, edits, *reviewed));
    }
    SECTION("a named subject that no longer exists reports the preview's reason") {
        REQUIRE(session.scene().removeLight(id));
        const auto current = app::reviewedEditsCurrent(session, tree, edits, *reviewed);
        REQUIRE_FALSE(current);
        CHECK(current.error().starts_with("stale: "));
    }
}

//======================================================================================================================
TEST_CASE("an orbiting light's position cannot be proposed while playback runs",
          "[app][session-edits]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    loaded.scene->animation.lightTracks.push_back({.light = 0,
                                                   .centre = {0.0f, 2.0f, 0.0f},
                                                   .axis = {0.0f, 1.0f, 0.0f},
                                                   .radius = 3.0f,
                                                   .phase = 0.0f,
                                                   .period = 4.0f});
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto tree = editableTree(id);
    REQUIRE(session.scene().animationLightId(0) == id);
    const std::vector<app::ProposalEdit> position{{"environment", "shadowFilter", R"("pcss")"},
                                                  {"node:2", "position", "[2,3,4]"}};
    const std::vector<app::ProposalEdit> others{{"node:2", "intensity", "2"},
                                                {"imported:0", "position", "[2,3,4]"},
                                                {"node:0", "position", "[0,2,4]"}};
    const auto refusal = app::animationOwnedEditRefusal(session, tree, position, false);
    REQUIRE(refusal);
    CHECK(*refusal == "Stop playback before proposing a position for orbiting light node:2");
    // Stopped playback shows the position Accept will compare against.
    CHECK_FALSE(app::animationOwnedEditRefusal(session, tree, position, true));
    // Fields the orbit does not own, and positions of other subjects, stay proposable.
    CHECK_FALSE(app::animationOwnedEditRefusal(session, tree, others, false));

    SECTION("a light without a track is never refused") {
        engine::LightId still;
        auto resting = editableFixture(still);
        app::SceneSession other;
        other.activate(resting, app::SceneActivationMotion::Reset);
        CHECK_FALSE(app::animationOwnedEditRefusal(other, editableTree(still), position, false));
    }
}

//======================================================================================================================
TEST_CASE("bridge pose refusal leaves every object in a mixed batch unchanged",
          "[app][session-edits][ux6-pose-batch]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    loaded.document.nodes.push_back({.name = "Static asset", .mesh = 0});
    loaded.document.rootNodes.push_back(3);
    loaded.binding.nodes.push_back({.objects = {1}});
    loaded.binding.objectNode.push_back(3);
    loaded.binding.objectImportedNode.push_back(engine::kGeneratedNode);
    loaded.binding.objectGeneratorNode.push_back(engine::kGeneratedNode);
    loaded.objectMobility.push_back(asset::DocMobility::Static);
    loaded.scene->objects.emplace_back();
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    auto tree = editableTree(id);
    tree.rows.push_back(
        {.subject = app::EditorSubject::Object, .index = 1, .node = 3, .label = "Static mesh"});
    std::string expected = "node:3/position: Static: mobility is authored in the scene file";
    SECTION("the selected static mesh refuses the batch") {}
    SECTION("a hidden imported primitive refuses the batch") {
        expected = "Static: mobility is authored in the scene file";
        loaded.objectMobility[1] = asset::DocMobility::Movable;
        loaded.binding.objectImportedNode[1] = 1;
        loaded.binding.importedNodes.push_back({.assetRoot = 3, .objects = {1, 2}});
        loaded.scene->objects.emplace_back();
        loaded.objectMobility.push_back(asset::DocMobility::Static);
        loaded.binding.objectNode.push_back(3);
        loaded.binding.objectImportedNode.push_back(1);
        loaded.binding.objectGeneratorNode.push_back(engine::kGeneratedNode);
    }
    const auto before = loaded.scene->objects;
    const auto generation = session.editGeneration();
    const std::vector<app::ProposalEdit> edits{{"imported:0", "position", "[2,3,4]"},
                                               {"node:3", "position", "[5,6,7]"}};
    const auto result = app::applyEdits(session, tree, edits);
    REQUIRE_FALSE(result);
    CHECK(result.error().message == expected);
    for (size_t i = 0; i < before.size(); ++i) {
        CHECK(loaded.scene->objects[i].position == before[i].position);
        CHECK(loaded.scene->objects[i].previousModel == before[i].previousModel);
    }
    CHECK(session.editGeneration() == generation);
}

//======================================================================================================================
TEST_CASE("bridge light pose refusal precedes earlier object mutation",
          "[app][session-edits][ux6-pose-batch]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    loaded.document.nodes[2].mobility = asset::DocMobility::Static;
    loaded.lightMobility[0] = asset::DocMobility::Static;
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto before = loaded.scene->objects[0];
    const auto lightBefore = *loaded.scene->light(id);
    const auto generation = session.editGeneration();
    const std::vector<app::ProposalEdit> edits{{"imported:0", "position", "[2,3,4]"},
                                               {"node:2", "position", "[5,6,7]"}};
    const auto result = app::applyEdits(session, editableTree(id), edits);
    REQUIRE_FALSE(result);
    CHECK(result.error().message ==
          "node:2/position: Static: mobility is authored in the scene file");
    CHECK(loaded.scene->objects[0].position == before.position);
    CHECK(loaded.scene->objects[0].previousModel == before.previousModel);
    CHECK(loaded.scene->light(id)->position == lightBefore.position);
    CHECK(loaded.scene->light(id)->direction == lightBefore.direction);
    CHECK(session.editGeneration() == generation);
}

//======================================================================================================================
TEST_CASE("bridge pose previews name the subject field and shared lock reason",
          "[app][session-edits][ux6-bridge-lock]") {
    for (const bool object : {true, false}) {
        const auto locks = object ? std::vector{app::PoseLock::Static, app::PoseLock::Generated,
                                                app::PoseLock::Animated, app::PoseLock::Measuring}
                                  : std::vector{app::PoseLock::Static, app::PoseLock::Animated,
                                                app::PoseLock::Measuring};
        for (const auto lock : locks) {
            for (const std::string& field :
                 object ? std::vector<std::string>{"position", "eulerDegrees", "scale"}
                        : std::vector<std::string>{"position", "direction"}) {
                DYNAMIC_SECTION(object << "/" << static_cast<int>(lock) << "/" << field) {
                    engine::LightId id;
                    auto loaded = editableFixture(id);
                    auto tree = editableTree(id);
                    if (lock == app::PoseLock::Static) {
                        (object ? loaded.objectMobility : loaded.lightMobility)[0] =
                            asset::DocMobility::Static;
                        if (object) {
                            loaded.document.nodes[1].asset.reset();
                            loaded.document.nodes[1].mesh = 0;
                            loaded.binding.objectImportedNode[0] = engine::kGeneratedNode;
                            tree.rows[1].node = engine::kGeneratedNode;
                            tree.rows[2].importedNode = engine::kGeneratedNode;
                        }
                    } else if (lock == app::PoseLock::Generated) {
                        loaded.binding.objectGeneratorNode[0] = 1;
                        loaded.binding.objectImportedNode[0] = engine::kGeneratedNode;
                        tree.rows[2].generated = true;
                        tree.rows[2].importedNode = engine::kGeneratedNode;
                    } else if (lock == app::PoseLock::Animated) {
                        if (object)
                            loaded.binding.importedNodes[0].animated = true;
                        else
                            loaded.scene->animation.lightTracks.push_back({.light = 0});
                    }
                    app::SceneSession session;
                    session.activate(loaded, app::SceneActivationMotion::Reset);
                    session.setMeasurementActive(lock == app::PoseLock::Measuring);
                    REQUIRE((object ? session.objectPoseLock(0)
                                    : session.lightPoseLock(app::EditorSubject::LocalLight, 0,
                                                            id)) == lock);
                    const auto subject = app::sceneTreeSubjectId(tree.rows[object ? 2 : 3]);
                    const auto beforeObject = session.scene().objects[0];
                    const auto beforeLight = *session.scene().light(id);
                    const auto beforeLook = session.look();
                    const auto generation = session.editGeneration();
                    const std::vector<app::ProposalEdit> edits{
                        {"environment", "shadowFilter", R"("pcss")"}, {subject, field, "[2,3,4]"}};
                    const auto expected =
                        subject + "/" + field + ": " + std::string(app::poseLockReason(lock));
                    const auto preview = app::previewEdits(session, tree, edits);
                    REQUIRE_FALSE(preview);
                    CHECK(preview.error() == expected);
                    const auto applied = app::applyEdits(session, tree, edits);
                    REQUIRE_FALSE(applied);
                    CHECK(applied.error().message == expected);
                    CHECK(session.scene().objects[0].position == beforeObject.position);
                    CHECK(session.scene().objects[0].eulerDegrees == beforeObject.eulerDegrees);
                    CHECK(session.scene().objects[0].scale == beforeObject.scale);
                    CHECK(session.scene().light(id)->position == beforeLight.position);
                    CHECK(session.scene().light(id)->direction == beforeLight.direction);
                    CHECK(session.look() == beforeLook);
                    CHECK(session.editGeneration() == generation);
                }
            }
        }
    }
}

//======================================================================================================================
TEST_CASE("bridge mobility proposals refuse the complete batch for objects and lights",
          "[app][session-edits][ux6-bridge-lock]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto tree = editableTree(id);
    const auto look = session.look();
    const auto generation = session.editGeneration();
    for (const std::string subject : {"imported:0", "node:2"}) {
        const std::vector<app::ProposalEdit> edits{{"environment", "shadowFilter", R"("pcss")"},
                                                   {subject, "mobility", R"("movable")"}};
        const auto preview = app::previewEdits(session, tree, edits);
        REQUIRE_FALSE(preview);
        CHECK(preview.error() == subject + "/mobility: Mobility is authored in the scene file");
        const auto applied = app::applyEdits(session, tree, edits);
        REQUIRE_FALSE(applied);
        CHECK(applied.error().message == preview.error());
        CHECK(session.look() == look);
        CHECK(session.editGeneration() == generation);
    }
}

//======================================================================================================================
TEST_CASE("static bridge subjects retain enabled and nonpose light edits",
          "[app][session-edits][ux6-bridge-lock]") {
    engine::LightId id;
    auto loaded = editableFixture(id);
    loaded.objectMobility[0] = asset::DocMobility::Static;
    loaded.lightMobility[0] = asset::DocMobility::Static;
    app::SceneSession session;
    session.activate(loaded, app::SceneActivationMotion::Reset);
    const auto tree = editableTree(id);
    const std::vector<app::ProposalEdit> edits{
        {"imported:0", "enabled", "false"},   {"node:2", "enabled", "false"},
        {"node:2", "intensity", "2"},         {"node:2", "range", "9"},
        {"node:2", "color", "[0.5,0.6,0.7]"}, {"node:2", "innerCone", "15"},
        {"node:2", "outerCone", "50"}};
    REQUIRE(app::previewEdits(session, tree, edits));
    REQUIRE(app::applyEdits(session, tree, edits));
    CHECK_FALSE(session.scene().objects[0].enabled);
    CHECK_FALSE(session.localLightEnabled(id));
    CHECK(session.scene().light(id)->intensity == 2);
    session.setMeasurementActive(true);
    const std::vector<app::ProposalEdit> enabled{{"imported:0", "enabled", "true"}};
    const auto refused = app::previewEdits(session, tree, enabled);
    REQUIRE_FALSE(refused);
    CHECK(refused.error() == "Scene edits require a document and no active measurement");
}

//======================================================================================================================
TEST_CASE("stale light pose proposals retain identity refusal before mobility",
          "[app][session-edits][ux6-bridge-stale]") {
    for (const int identity : {0, 1, 2}) {
        DYNAMIC_SECTION(identity) {
            engine::LightId id;
            auto loaded = editableFixture(id);
            app::SceneSession session;
            session.activate(loaded, app::SceneActivationMotion::Reset);
            auto tree = editableTree(id);
            if (identity == 0)
                REQUIRE(session.scene().removeLight(id));
            else if (identity == 1)
                ++tree.rows[3].lightId.store;
            else
                ++tree.rows[3].lightId.generation;
            const auto look = session.look();
            const auto position = session.scene().objects[0].position;
            const auto generation = session.editGeneration();
            for (const std::string field : {"position", "direction"}) {
                const std::vector<app::ProposalEdit> edits{
                    {"environment", "shadowFilter", R"("pcss")"}, {"node:2", field, "[2,3,4]"}};
                const auto preview = app::previewEdits(session, tree, edits);
                REQUIRE_FALSE(preview);
                CHECK(preview.error() == "Stale local-light identity node:2");
                const auto applied = app::applyEdits(session, tree, edits);
                REQUIRE_FALSE(applied);
                CHECK(applied.error().message == preview.error());
                CHECK(session.look() == look);
                CHECK(session.scene().objects[0].position == position);
                CHECK(session.editGeneration() == generation);
                session.setMeasurementActive(true);
                const auto measuring = app::previewEdits(session, tree, edits);
                REQUIRE_FALSE(measuring);
                CHECK(measuring.error() ==
                      "Scene edits require a document and no active measurement");
                session.setMeasurementActive(false);
            }
        }
    }
}
