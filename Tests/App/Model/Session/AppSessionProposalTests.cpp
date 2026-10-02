#include "App/Model/Session/SessionProposal.h"
#include "App/Model/Session/SessionProtocol.h"
#include "App/Model/Session/SessionQueries.h"
#include "Engine/Asset/Document/SceneDocument.h"
#include <fstream>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("review rows distinguish owners and evidence remains visible on errors",
          "[app][session]") {
    SessionProposal proposal;
    proposal.state = SessionState::Error;
    proposal.evidence = {"before.png", "after.png"};
    proposal.changes = {
        {lmx::asset::DocumentChangeOwner::Light, 2, "Front", "intensity", "10", "12"},
        {lmx::asset::DocumentChangeOwner::Light, 3, "Back", "intensity", "10", "12"},
        {lmx::asset::DocumentChangeOwner::Node, 8, {}, "enabled", {}, "true"}};
    const auto collapsed = proposalReviewDetails(proposal, false);
    REQUIRE(collapsed.changes.empty());
    REQUIRE(collapsed.evidence.size() == 2);
    REQUIRE(collapsed.evidence[0] == "before.png");
    const auto expanded = proposalReviewDetails(proposal, true);
    REQUIRE(expanded.changes.size() == 3);
    REQUIRE(expanded.evidence.size() == 2);
    const auto front = proposalChangeLabel(expanded.changes[0]);
    const auto back = proposalChangeLabel(expanded.changes[1]);
    const auto unnamed = proposalChangeLabel(expanded.changes[2]);
    REQUIRE(front != back);
    REQUIRE(front.find("Light 2") != std::string::npos);
    REQUIRE(front.find("Front") != std::string::npos);
    REQUIRE(back.find("Light 3") != std::string::npos);
    REQUIRE(back.find("Back") != std::string::npos);
    REQUIRE(unnamed.find("Node 8") != std::string::npos);
    REQUIRE(unnamed.find("enabled") != std::string::npos);
}

//======================================================================================================================
TEST_CASE("sidecar accepts schema one and ignores extension keys", "[app][session]") {
    const auto parsed = parseSidecar(
        R"({"schema":1,"actor":"Lighting client","summary":"Move lamp","evidence":["frame.png","notes/report.json"],"documentSha256":"abc123","future":{"nested":true}})");
    REQUIRE(parsed);
    CHECK(parsed->actor == "Lighting client");
    CHECK(parsed->summary == "Move lamp");
    CHECK(parsed->evidence == std::vector<std::string>{"frame.png", "notes/report.json"});
    CHECK(parsed->documentSha256 == "abc123");
    CHECK(sidecarPath("/tmp/x.scene.gltf") == "/tmp/x.scene.proposal.json");
    CHECK(sidecarPath("/tmp/x.gltf") == "/tmp/x.gltf.proposal.json");
}

//======================================================================================================================
TEST_CASE("sidecar rejects malformed schema, missing fields and wrong types", "[app][session]") {
    const auto base =
        std::string{R"({"schema":1,"actor":"C","summary":"S","documentSha256":"h","evidence":[]})"};
    const auto rejects = [](std::string text, std::string_view key) {
        const auto result = parseSidecar(std::move(text));
        REQUIRE_FALSE(result);
        CHECK(result.error().find(key) != std::string::npos);
    };
    rejects("{", "JSON");
    rejects("[]", "object");
    rejects(R"({"actor":"C","summary":"S","documentSha256":"h"})", "schema");
    rejects(R"({"schema":2,"actor":"C","summary":"S","documentSha256":"h"})", "schema");
    rejects(R"({"schema":"1","actor":"C","summary":"S","documentSha256":"h"})", "schema");
    rejects(R"({"schema":1.0,"actor":"C","summary":"S","documentSha256":"h"})", "schema");
    rejects(R"({"schema":true,"actor":"C","summary":"S","documentSha256":"h"})", "schema");
    rejects(R"({"schema":1,"summary":"S","documentSha256":"h"})", "actor");
    rejects(R"({"schema":1,"actor":"C","documentSha256":"h"})", "summary");
    rejects(R"({"schema":1,"actor":"C","summary":"S"})", "documentSha256");
    rejects(R"({"schema":1,"actor":7,"summary":"S","documentSha256":"h"})", "actor");
    rejects(R"({"schema":1,"actor":"C","summary":null,"documentSha256":"h"})", "summary");
    rejects(R"({"schema":1,"actor":"C","summary":"S","documentSha256":false})", "documentSha256");
    rejects(R"({"schema":1,"actor":"C","summary":"S","documentSha256":"h","evidence":{}})",
            "evidence");
    rejects(R"({"schema":1,"actor":"C","summary":"S","documentSha256":"h","evidence":["ok",3]})",
            "evidence");
    CHECK(parseSidecar(base)->evidence.empty());
}

//======================================================================================================================
TEST_CASE("file proposals retain attribution, supersede, and reject only their hash",
          "[app][session]") {
    ProposalQueue queue;
    SessionProposal unknown{};
    unknown.source = ProposalSource::File;
    unknown.actor = Actor::Agent;
    unknown.hash = "hash-a";
    unknown.state = SessionState::Proposed;
    const auto first = queue.add(std::move(unknown));
    REQUIRE(queue.pendingFile());
    CHECK(queue.pendingFile()->actor == Actor::System);
    CHECK(queue.pendingFile()->client == "Unknown external change");
    CHECK(queue.pending() == 1);
    CHECK_FALSE(queue.rejected("hash-a"));

    SessionProposal second{};
    second.source = ProposalSource::File;
    second.actor = Actor::Agent;
    second.client = "Lighting client";
    second.hash = "hash-b";
    second.state = SessionState::Error;
    const auto replacement = queue.add(std::move(second));
    REQUIRE(queue.find(first));
    CHECK(queue.find(first)->state == SessionState::Stale);
    REQUIRE(queue.pendingFile());
    CHECK(queue.pendingFile()->id == replacement);
    CHECK(queue.pending() == 1);

    queue.reject(replacement);
    CHECK(queue.rejected("hash-b"));
    CHECK_FALSE(queue.rejected("hash-a"));
    CHECK(queue.pending() == 0);
    CHECK(queue.pendingFile() == nullptr);
    queue.markStale(ProposalSource::File);
    CHECK_FALSE(queue.rejected("hash-b"));
}

//======================================================================================================================
TEST_CASE("proposal queue preserves bridge proposals and bounds retained history",
          "[app][session]") {
    ProposalQueue queue;
    SessionProposal bridge{};
    bridge.source = ProposalSource::Bridge;
    bridge.state = SessionState::Proposed;
    const auto bridgeId = queue.add(std::move(bridge));
    uint64_t lastFileId = 0;
    for (size_t i = 0; i < 80; ++i) {
        SessionProposal file{};
        file.source = ProposalSource::File;
        file.hash = std::to_string(i);
        file.state = SessionState::Proposed;
        const auto id = queue.add(std::move(file));
        CHECK(id > lastFileId);
        lastFileId = id;
        queue.reject(id);
    }
    CHECK(queue.all().size() == 64);
    CHECK(queue.pending() == 1);
    CHECK(queue.find(bridgeId) != nullptr);
    CHECK(queue.find(2) == nullptr);
    REQUIRE(queue.find(lastFileId));
    CHECK(queue.find(lastFileId)->hash == "79");
    CHECK(queue.rejected("79"));
    CHECK(queue.rejected("0"));
    queue.markStale(ProposalSource::Bridge);
    CHECK(queue.pending() == 0);
    REQUIRE(queue.find(bridgeId));
    CHECK(queue.find(bridgeId)->state == SessionState::Stale);
}

//======================================================================================================================
TEST_CASE("reject click cannot consume a newer unreviewed file hash", "[app][session]") {
    ProposalQueue queue;
    SessionProposal first;
    first.source = ProposalSource::File;
    first.client = "First client";
    first.hash = "hash-a";
    first.state = SessionState::Proposed;
    const auto id = queue.add(std::move(first));
    CHECK_FALSE(queue.rejectIfCurrent(id, "hash-b"));
    CHECK(queue.find(id)->state == SessionState::Stale);
    CHECK_FALSE(queue.rejected("hash-a"));
    SessionProposal next;
    next.source = ProposalSource::File;
    next.client = "Second client";
    next.hash = "hash-b";
    next.state = SessionState::Proposed;
    const auto nextId = queue.add(std::move(next));
    CHECK(queue.rejectIfCurrent(nextId, "hash-b"));
    CHECK(queue.rejected("hash-b"));
}

//======================================================================================================================
TEST_CASE("light and camera changes ring every referencing document node", "[app][session]") {
    lmx::asset::SceneDocument document;
    document.nodes.resize(4);
    document.nodes[0].light = 0;
    document.nodes[2].light = 0;
    document.nodes[1].camera = 0;
    SessionProposal proposal;
    proposal.changes = {{lmx::asset::DocumentChangeOwner::Light, 0, "", "intensity", "1", "2"},
                        {lmx::asset::DocumentChangeOwner::Camera, 0, "", "fov", "1", "2"}};
    CHECK(proposalAffectsNode(proposal, document, 0));
    CHECK(proposalAffectsNode(proposal, document, 1));
    CHECK(proposalAffectsNode(proposal, document, 2));
    CHECK_FALSE(proposalAffectsNode(proposal, document, 3));
    proposal.changes = {{lmx::asset::DocumentChangeOwner::Node, 3, "", "enabled", "true", "false"}};
    CHECK(proposalAffectsNode(proposal, document, 3));
}

//======================================================================================================================
TEST_CASE("pending bridge proposals fill the queue instead of evicting each other",
          "[app][session]") {
    ProposalQueue queue;
    uint64_t newest = 0;
    for (size_t i = 0; i < ProposalQueue::kMaxPendingBridge; ++i) {
        CHECK_FALSE(queue.bridgeFull());
        SessionProposal bridge{};
        bridge.source = ProposalSource::Bridge;
        bridge.state = SessionState::Proposed;
        newest = queue.add(std::move(bridge));
    }
    CHECK(queue.bridgeFull());
    CHECK(queue.all().size() == 64);
    CHECK(queue.pending() == 64);
    REQUIRE(queue.find(1));
    CHECK(queue.find(1)->state == SessionState::Proposed);
    REQUIRE(queue.find(newest));

    queue.resolve(1, SessionState::Stale);
    CHECK_FALSE(queue.bridgeFull());
    SessionProposal bridge{};
    bridge.source = ProposalSource::Bridge;
    bridge.state = SessionState::Proposed;
    const auto replacement = queue.add(std::move(bridge));
    CHECK(queue.bridgeFull());
    CHECK(queue.all().size() == 64);
    CHECK(queue.find(1) == nullptr);
    CHECK(queue.find(2) != nullptr);
    CHECK(queue.find(replacement) != nullptr);
}

//======================================================================================================================
TEST_CASE("queue pressure preserves an active file proposal", "[app][session]") {
    ProposalQueue queue;
    SessionProposal file{};
    file.source = ProposalSource::File;
    file.state = SessionState::Proposed;
    const auto fileId = queue.add(std::move(file));
    uint64_t firstBridgeId = 0;
    for (size_t i = 0; i < 64; ++i) {
        SessionProposal bridge{};
        bridge.source = ProposalSource::Bridge;
        bridge.state = SessionState::Proposed;
        const auto id = queue.add(std::move(bridge));
        if (i == 0)
            firstBridgeId = id;
    }
    CHECK(queue.all().size() == 65);
    CHECK(queue.pending() == 65);
    REQUIRE(queue.pendingFile());
    CHECK(queue.pendingFile()->id == fileId);
    CHECK(queue.find(fileId) != nullptr);
    CHECK(queue.find(firstBridgeId) != nullptr);

    SessionProposal next{};
    next.source = ProposalSource::File;
    next.hash = "next";
    next.state = SessionState::Proposed;
    const auto nextId = queue.add(std::move(next));
    CHECK(queue.all().size() == 65);
    CHECK(queue.find(fileId) == nullptr);
    CHECK(queue.pendingFile()->id == nextId);
    CHECK(queue.find(firstBridgeId) != nullptr);
}

//======================================================================================================================
TEST_CASE("a card without change rows shows its state alone", "[app][session]") {
    SessionProposal card;
    card.state = SessionState::Awaiting;
    CHECK(proposalStatusLine(card) == "awaiting");
    card.state = SessionState::Proposed;
    card.changes = {{lmx::asset::DocumentChangeOwner::Node, 3, "", "enabled", "true", "false"},
                    {lmx::asset::DocumentChangeOwner::Look, 0, "", "bloom", "1", "2"}};
    CHECK(proposalStatusLine(card) == "2 changes · proposed");
}

//======================================================================================================================
TEST_CASE("a stale proposal cannot be applied", "[app][session]") {
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ProposalQueue queue;
        SessionProposal file{};
        file.source = ProposalSource::File;
        file.state = SessionState::Stale;
        const auto id = queue.add(std::move(file));
        queue.resolve(id, SessionState::Applied);
        _exit(0);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGABRT);
}

//======================================================================================================================
TEST_CASE("disk sidecar rejects invalid UTF-8 before proposal and log queries",
          "[app][session][sidecar-utf8]") {
    const auto directory = std::filesystem::path("SessionSidecarUtf8");
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{directory};
    const auto documentPath = directory / "changed.scene.gltf";
    lmx::asset::SceneDocument document;
    document.name = "External change";
    document.nodes = {{.name = "Camera", .camera = 0}};
    document.rootNodes = {0};
    document.cameras = {{.name = "Lens"}};
    const auto before = document;
    document.look.bloom.intensity += 1;
    REQUIRE(lmx::asset::saveSceneDocument(document, documentPath));
    const auto pairHash = lmx::asset::sceneDocumentHash(documentPath);
    REQUIRE(pairHash);
    const auto path = sidecarPath(documentPath);
    for (const std::string field : {"actor", "summary", "evidence"}) {
        INFO(field);
        const std::string bad(1, static_cast<char>(0xff));
        const std::string actor = field == "actor" ? bad : "Writer";
        const std::string summary = field == "summary" ? bad : "Move lamp";
        const std::string evidence = field == "evidence" ? bad : "frame.png";
        const auto text = "{\"schema\":1,\"actor\":\"" + actor + "\",\"summary\":\"" + summary +
                          "\",\"documentSha256\":\"" + *pairHash + "\",\"evidence\":[\"" +
                          evidence + "\"]}";
        {
            std::ofstream file(path, std::ios::binary);
            REQUIRE(file.good());
            file << text;
        }
        std::ifstream file(path, std::ios::binary);
        const std::string disk(std::istreambuf_iterator<char>{file}, {});
        const auto sidecar = parseSidecar(disk);
        CHECK_FALSE(sidecar);
        if (!sidecar)
            CHECK(sidecar.error().find("UTF-8") != std::string::npos);
        ProposalQueue queue;
        SessionProposal proposal;
        proposal.source = ProposalSource::File;
        proposal.actor = sidecar ? Actor::Agent : Actor::System;
        proposal.client = sidecar ? sidecar->actor : "Unknown external change";
        proposal.summary = sidecar ? sidecar->summary : "External scene change";
        proposal.hash = *pairHash;
        proposal.changes = lmx::asset::diffSceneDocuments(before, document);
        proposal.state = SessionState::Proposed;
        if (sidecar)
            proposal.evidence = sidecar->evidence;
        queue.add(proposal);
        SessionLog log(std::make_shared<ConsoleLog>());
        log.record({.actor = proposal.actor,
                    .client = proposal.client,
                    .command = "proposal.arrived",
                    .arguments = proposal.summary,
                    .outcome = "proposed"});
        const auto proposals = lmx::asset::JsonTokens::parse(encodeResult(1, proposalsJson(queue)));
        REQUIRE(proposals);
        CHECK(proposals->root().find("ok")->asBool() == true);
        const auto history = lmx::asset::JsonTokens::parse(encodeResult(2, logJson(log)));
        REQUIRE(history);
        CHECK(history->root().find("ok")->asBool() == true);
        const auto next = decodeRequest(R"({"id":3,"command":"query.status"})");
        REQUIRE(next);
        const auto continued =
            lmx::asset::JsonTokens::parse(encodeResult(next->id, statusJson({})));
        REQUIRE(continued);
        CHECK(continued->root().find("ok")->asBool() == true);
    }
}

//======================================================================================================================
TEST_CASE("a reconnected client withdraws its own proposals and a live one keeps its own",
          "[app][session]") {
    SessionProposal proposal;
    proposal.source = ProposalSource::Bridge;
    proposal.state = SessionState::Proposed;
    proposal.client = "writer";
    proposal.connection = 3;
    // The submitting connection, under any name it now reports.
    CHECK(proposalWithdrawable(proposal, 3, 3, "writer"));
    CHECK(proposalWithdrawable(proposal, 3, 3, "renamed"));
    // Connection 3 is gone; connection 5 is the same client reconnected.
    CHECK(proposalWithdrawable(proposal, 5, 5, "writer"));
    CHECK_FALSE(proposalWithdrawable(proposal, 5, 5, "other"));
    CHECK_FALSE(proposalWithdrawable(proposal, 5, 5, ""));
    // While the submitting connection is the live one, a matching name is not enough.
    CHECK_FALSE(proposalWithdrawable(proposal, 5, 3, "writer"));
    // Only a Bridge proposal still awaiting review can be withdrawn.
    proposal.state = SessionState::Applied;
    CHECK_FALSE(proposalWithdrawable(proposal, 3, 3, "writer"));
    proposal.state = SessionState::Stale;
    CHECK_FALSE(proposalWithdrawable(proposal, 5, 5, "writer"));
    proposal.state = SessionState::Proposed;
    proposal.source = ProposalSource::File;
    CHECK_FALSE(proposalWithdrawable(proposal, 3, 3, "writer"));
}
