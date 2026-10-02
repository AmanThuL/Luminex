#include "App/Model/Session/SessionQueries.h"

#include "Core/IO/File.h"
#include "Engine/Asset/Model/JsonTokens.h"
#include "Support/SceneDocumentTestSupport.h"
#include <cmath>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <memory>
#include <vector>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("Session hierarchy encodes complete subject identities", "[app][session]") {
    SceneTreeView tree;
    tree.rows = {
        {.subject = EditorSubject::Group, .label = "Root", .group = true},
        {.subject = EditorSubject::Camera, .node = 2, .depth = 1, .label = "Camera"},
        {.subject = EditorSubject::DirectionalLight,
         .index = 1,
         .node = 3,
         .depth = 1,
         .label = "Sun"},
        {.subject = EditorSubject::Object,
         .index = 4,
         .node = 5,
         .importedNode = 7,
         .depth = 2,
         .label = "Part"},
        {.subject = EditorSubject::Object,
         .index = 8,
         .node = 6,
         .depth = 2,
         .label = "Generated",
         .generated = true,
         .enabled = false,
         .effective = false},
        {.subject = EditorSubject::LocalLight,
         .lightId = {.slot = 9, .generation = 3, .store = 2},
         .node = 6,
         .depth = 2,
         .label = "Lamp",
         .generated = true},
        {.subject = EditorSubject::Environment, .depth = 1, .label = "Environment"},
    };
    const auto parsed = lmx::asset::JsonTokens::parse(hierarchyJson(tree));
    REQUIRE(parsed);
    const auto rows = parsed->root().find("rows");
    REQUIRE(rows);
    REQUIRE(rows->size() == 6);
    const std::array<std::string_view, 6> ids{"node:2",   "node:3",    "imported:7",
                                              "object:8", "light:9:3", "environment"};
    for (size_t index = 0; index < ids.size(); ++index) {
        REQUIRE(rows->at(index).find("id")->asString() == ids[index]);
        REQUIRE(parseSubjectId(ids[index], tree));
    }
    REQUIRE_FALSE(parseSubjectId("light:9:2", tree));
    REQUIRE_FALSE(parseSubjectId("node:99", tree));
    REQUIRE_FALSE(parseSubjectId("object:04", tree));
    REQUIRE(parseSubjectId("node:3", tree)->subject == EditorSubject::DirectionalLight);
    REQUIRE(parseSubjectId("imported:7", tree)->importedNode == 7);
    REQUIRE(parseSubjectId("light:9:3", tree)->lightId.store == 2);
    REQUIRE(rows->at(3).find("generated")->asBool() == true);
    REQUIRE(rows->at(3).find("effective")->asBool() == false);
    EditorSelection selection{.subject = EditorSubject::LocalLight,
                              .lightId = {.slot = 9, .generation = 3, .store = 2},
                              .node = 6};
    const auto selected = lmx::asset::JsonTokens::parse(selectionJson(selection, tree));
    REQUIRE(selected);
    REQUIRE(selected->root().find("id")->asString() == "light:9:3");
}

//======================================================================================================================
TEST_CASE("Session JSON encoders preserve fixture content", "[app][session]") {
    EditorRenderSettings settings;
    const auto encoded = lmx::asset::JsonTokens::parse(settingsJson(settings));
    REQUIRE(encoded);
    REQUIRE(encoded->root().find("temporal"));
    REQUIRE(encoded->root().find("render-scale")->asString() == "1");
    lmx::engine::Camera camera;
    camera.position = {1.0f, 2.0f, 3.0f};
    camera.yaw = 0.5f;
    const auto cameraValue = lmx::asset::JsonTokens::parse(cameraJson(camera));
    REQUIRE(cameraValue);
    REQUIRE(cameraValue->root().find("position")->at(2).asFloat() == 3.0f);
    REQUIRE(cameraValue->root().find("yaw")->asFloat() == 0.5f);
    ConsoleSnapshot console;
    console.entries.push_back({.sequence = 5, .message = "hello"});
    console.entries.push_back({.sequence = 6, .message = "after", .actor = Actor::Operator});
    const auto after = lmx::asset::JsonTokens::parse(consoleJson(console, 5));
    REQUIRE(after);
    REQUIRE(after->root().find("entries")->size() == 1);
    REQUIRE(after->root().find("entries")->at(0).find("sequence")->asUInt() == 6);
    REQUIRE(after->root().find("entries")->at(0).find("message")->asString() == "after");
    REQUIRE(after->root().find("entries")->at(0).find("actor")->asString() == "Operator");
    const auto all = lmx::asset::JsonTokens::parse(consoleJson(console, 0));
    REQUIRE(all);
    REQUIRE(all->root().find("entries")->at(0).find("actor")->asString() == "System");
    ProposalQueue proposals;
    SessionProposal proposal;
    proposal.source = ProposalSource::File;
    proposal.client = "Fixture";
    proposal.summary = "Change lamp";
    proposal.state = SessionState::Proposed;
    proposals.add(std::move(proposal));
    const auto proposalValue = lmx::asset::JsonTokens::parse(proposalsJson(proposals));
    REQUIRE(proposalValue);
    REQUIRE(proposalValue->root().find("proposals")->at(0).find("summary")->asString() ==
            "Change lamp");
    PerformanceSnapshot performance;
    performance.frameId = 42;
    performance.passRows.push_back({.label = "scene",
                                    .averageGpuMilliseconds = 1.5,
                                    .latestGpuMilliseconds = 2.0,
                                    .sampleCount = 3});
    const auto performanceValue = lmx::asset::JsonTokens::parse(performanceJson(performance));
    REQUIRE(performanceValue);
    REQUIRE(performanceValue->root().find("frameId")->asUInt() == 42);
    REQUIRE(performanceValue->root().find("passes")->at(0).find("samples")->asUInt() == 3);
    auto consoleStore = std::make_shared<ConsoleLog>();
    SessionLog log(consoleStore);
    log.record({.timestampMilliseconds = 123,
                .actor = Actor::Operator,
                .command = "tier",
                .outcome = "changed"});
    const auto history = lmx::asset::JsonTokens::parse(logJson(log));
    REQUIRE(history);
    REQUIRE(history->root().find("actions")->at(0).find("command")->asString() == "tier");
    REQUIRE(history->root().find("actions")->at(0).find("actor")->asString() == "Operator");
    SessionStatus status{.documentPath = "/tmp/scene.gltf",
                         .documentHash = "abc",
                         .dirty = true,
                         .playback = "paused",
                         .tier = SessionTier::Propose,
                         .pendingProposals = 1};
    const auto statusValue = lmx::asset::JsonTokens::parse(statusJson(status));
    REQUIRE(statusValue);
    REQUIRE(statusValue->root().find("document")->find("dirty")->asBool() == true);
    REQUIRE(statusValue->root().find("tier")->asUInt() == 1);
    lmx::render::VisibilityStatus visibility;
    visibility.frameNumber = 71;
    lmx::render::LightingStatus lighting;
    lighting.frameNumber = 72;
    const auto readings = lmx::asset::JsonTokens::parse(readingsJson(visibility, lighting));
    REQUIRE(readings);
    REQUIRE(readings->root().find("visibility")->find("frameId")->asUInt() == 71);
    REQUIRE(readings->root().find("lighting")->find("frameId")->asUInt() == 72);
}

//======================================================================================================================
TEST_CASE("tier audit action retains the authenticated client and resulting ceiling",
          "[app][session]") {
    const auto action = sessionTierAction("Writer", SessionTier::Apply, 123);
    CHECK(action.client == "Writer");
    CHECK(action.tier == SessionTier::Apply);
    CHECK(action.actor == Actor::Operator);
    CHECK(action.command == "session.tier");
    CHECK(action.outcome == "changed");
    CHECK(action.timestampMilliseconds == 123);
}

//======================================================================================================================
TEST_CASE("camera query represents a document perspective camera without zfar",
          "[app][session][camera-query]") {
    auto document = lmx::scenes::readCatalogDocument("light-lab");
    REQUIRE(document);
    const auto& cameraNode = document->nodes.at(document->camera);
    document->cameras.at(*cameraNode.camera).farZ.reset();
    const auto path = std::filesystem::path("SessionQueryFixtures/infinite.scene.gltf");
    std::filesystem::create_directories(path.parent_path());
    REQUIRE(lmx::asset::saveSceneDocument(*document, path));
    const auto bytes = lmx::readWholeFile(path);
    REQUIRE(bytes);
    const std::string json(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    CHECK(json.find("zfar") == std::string::npos);
    const auto read = lmx::asset::readSceneDocument(path);
    REQUIRE(read);
    lmx::engine::Scene scene;
    lmx::engine::applyDocumentCamera(scene, *read);
    const auto camera = lmx::engine::cameraFromScene(scene.initialCamera);
    REQUIRE(std::isinf(camera.farZ));
    const auto encoded = lmx::asset::JsonTokens::parse(cameraJson(camera));
    REQUIRE(encoded);
    CHECK(encoded->root().find("farZ")->asString() == "infinite");
    CHECK(encoded->root().find("nearZ")->asFloat() == camera.nearZ);
    std::filesystem::remove_all(path.parent_path());
}

//======================================================================================================================
TEST_CASE("session queries report non-finite values as strings", "[app][session]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    lmx::engine::Camera camera;
    camera.position = {nan, infinity, -infinity};
    camera.yaw = nan;
    camera.pitch = infinity;
    camera.fovY = -infinity;
    camera.nearZ = nan;
    camera.farZ = nan;
    const auto cameraValue = lmx::asset::JsonTokens::parse(cameraJson(camera));
    REQUIRE(cameraValue);
    const auto root = cameraValue->root();
    CHECK(root.find("position")->at(0).asString() == "nan");
    CHECK(root.find("position")->at(1).asString() == "infinite");
    CHECK(root.find("position")->at(2).asString() == "-infinite");
    CHECK(root.find("yaw")->asString() == "nan");
    CHECK(root.find("pitch")->asString() == "infinite");
    CHECK(root.find("fovY")->asString() == "-infinite");
    CHECK(root.find("nearZ")->asString() == "nan");
    CHECK(root.find("farZ")->asString() == "nan");

    PerformanceSnapshot performance;
    performance.timedPassSumMilliseconds = std::numeric_limits<double>::quiet_NaN();
    performance.passRows.push_back(
        {.label = "scene",
         .averageGpuMilliseconds = std::numeric_limits<double>::infinity(),
         .latestGpuMilliseconds = -std::numeric_limits<double>::infinity(),
         .minimumGpuMilliseconds = std::numeric_limits<double>::quiet_NaN(),
         .maximumGpuMilliseconds = 2.5});
    const auto performanceValue = lmx::asset::JsonTokens::parse(performanceJson(performance));
    REQUIRE(performanceValue);
    CHECK(performanceValue->root().find("timedPassSumMilliseconds")->asString() == "nan");
    const auto pass = performanceValue->root().find("passes")->at(0);
    CHECK(pass.find("averageMilliseconds")->asString() == "infinite");
    CHECK(pass.find("latestMilliseconds")->asString() == "-infinite");
    CHECK(pass.find("minimumMilliseconds")->asString() == "nan");
    CHECK(pass.find("maximumMilliseconds")->asFloat() == 2.5f);

    lmx::JsonWriter writer;
    writer.beginArray(true);
    writeSessionNumber(writer, 0.1f);
    writeSessionNumber(writer, 0.25);
    writer.endArray();
    CHECK(writer.take() == "[0.1, 0.25]\n");
}

//======================================================================================================================
TEST_CASE("log query pages by sequence and keeps the newest rows that fit", "[app][session]") {
    SessionLog log(std::make_shared<ConsoleLog>());
    for (int index = 0; index < 6; ++index)
        log.record({.actor = Actor::Agent,
                    .client = "Writer",
                    .command = "query.status",
                    .arguments = std::string(100, 'a'),
                    .outcome = "answered"});
    const auto sequences = [](const lmx::asset::JsonNode& root) {
        std::vector<uint64_t> values;
        for (const auto& action : root.find("actions")->elements())
            values.push_back(*action.find("sequence")->asUInt());
        return values;
    };
    const auto everything = lmx::asset::JsonTokens::parse(logJson(log));
    REQUIRE(everything);
    CHECK(sequences(everything->root()) == std::vector<uint64_t>{1, 2, 3, 4, 5, 6});
    CHECK(everything->root().find("nextSequence")->asUInt() == 7);
    CHECK(everything->root().find("dropped")->asUInt() == 0);
    CHECK(everything->root().find("omitted")->asUInt() == 0);
    CHECK(everything->root().find("actions")->at(0).find("actor")->asString() == "Agent");

    const auto after = lmx::asset::JsonTokens::parse(logJson(log, 4));
    REQUIRE(after);
    CHECK(sequences(after->root()) == std::vector<uint64_t>{5, 6});
    const auto none = lmx::asset::JsonTokens::parse(logJson(log, 6));
    REQUIRE(none);
    CHECK(none->root().find("actions")->size() == 0);

    // Each row is over 200 bytes, so 700 bytes hold the newest two or three and never more.
    const auto bounded = logJson(log, 1, 700);
    const auto boundedValue = lmx::asset::JsonTokens::parse(bounded);
    REQUIRE(boundedValue);
    const auto kept = sequences(boundedValue->root());
    REQUIRE(!kept.empty());
    CHECK(kept.size() < 5);
    CHECK(kept.back() == 6);
    CHECK(kept.front() == 7 - kept.size());
    CHECK(boundedValue->root().find("omitted")->asUInt() == 5 - kept.size());
    const auto empty = lmx::asset::JsonTokens::parse(logJson(log, 0, 10));
    REQUIRE(empty);
    CHECK(empty->root().find("actions")->size() == 0);
    CHECK(empty->root().find("omitted")->asUInt() == 6);
}

//======================================================================================================================
TEST_CASE("a full session log still answers within one response line", "[app][session]") {
    SessionLog log(std::make_shared<ConsoleLog>());
    for (size_t index = 0; index < kMaxSessionActions + 5; ++index)
        log.record({.actor = Actor::Agent,
                    .client = "Writer",
                    .command = std::string(400, 'c'),
                    .arguments = std::string(2 * kMaxActionArgumentBytes, '"'),
                    .outcome = "invalid"});
    const auto reply = logJson(log);
    CHECK(reply.size() + 96 <= kMaxLineBytes);
    const auto parsed = lmx::asset::JsonTokens::parse(reply);
    REQUIRE(parsed);
    const auto root = parsed->root();
    CHECK(root.find("dropped")->asUInt() == 5);
    const auto rows = root.find("actions")->size();
    CHECK(rows > 0);
    CHECK(root.find("omitted")->asUInt() == kMaxSessionActions - rows);
    CHECK(root.find("actions")->at(rows - 1).find("sequence")->asUInt() == kMaxSessionActions + 5);
}
