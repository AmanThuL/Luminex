#include "App/Model/Session/SessionQueries.h"

#include "Engine/Asset/Model/JsonTokens.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

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
    console.entries.push_back({.sequence = 6, .message = "after"});
    const auto after = lmx::asset::JsonTokens::parse(consoleJson(console, 5));
    REQUIRE(after);
    REQUIRE(after->root().find("entries")->size() == 1);
    REQUIRE(after->root().find("entries")->at(0).find("sequence")->asUInt() == 6);
    REQUIRE(after->root().find("entries")->at(0).find("message")->asString() == "after");
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
