#include "App/Model/Session/SessionProposal.h"

#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace lmx::app;

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
TEST_CASE("proposal queue remains bounded when all proposals await review", "[app][session]") {
    ProposalQueue queue;
    uint64_t newest = 0;
    for (size_t i = 0; i < 65; ++i) {
        SessionProposal bridge{};
        bridge.source = ProposalSource::Bridge;
        bridge.state = SessionState::Proposed;
        newest = queue.add(std::move(bridge));
    }
    CHECK(queue.all().size() == 64);
    CHECK(queue.pending() == 64);
    CHECK(queue.find(1) == nullptr);
    REQUIRE(queue.find(newest));
    CHECK(queue.find(newest)->id == newest);
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
    CHECK(queue.all().size() == 64);
    CHECK(queue.pending() == 64);
    REQUIRE(queue.pendingFile());
    CHECK(queue.pendingFile()->id == fileId);
    CHECK(queue.find(fileId) != nullptr);
    CHECK(queue.find(firstBridgeId) == nullptr);
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
