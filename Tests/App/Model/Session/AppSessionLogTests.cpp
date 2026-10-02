#include "App/Model/Console/ConsoleModel.h"
#include "App/Model/Session/SessionLog.h"
#include "Engine/Asset/Model/JsonTokens.h"

#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("session states have stable lowercase labels", "[app][session]") {
    CHECK(sessionStateLabel(SessionState::Idle) == "idle");
    CHECK(sessionStateLabel(SessionState::Working) == "working");
    CHECK(sessionStateLabel(SessionState::Awaiting) == "awaiting");
    CHECK(sessionStateLabel(SessionState::Proposed) == "proposed");
    CHECK(sessionStateLabel(SessionState::Applied) == "applied");
    CHECK(sessionStateLabel(SessionState::Error) == "error");
    CHECK(sessionStateLabel(SessionState::Stale) == "stale");
}

//======================================================================================================================
TEST_CASE("session log assigns sequences and attributes a Console row", "[app][session]") {
    auto console = std::make_shared<ConsoleLog>();
    SessionLog log(console);
    SessionAction first{};
    first.timestampMilliseconds = 1234;
    first.actor = Actor::Agent;
    first.client = "client one";
    first.command = "propose.edits";
    first.arguments = "{\"summary\":\"lamp\"}";
    first.tier = SessionTier::Propose;
    first.outcome = "proposed";
    const auto firstSequence = log.record(first);

    SessionAction second{};
    second.timestampMilliseconds = 1235;
    second.actor = Actor::Operator;
    second.client = "client one";
    second.command = "Accept";
    second.arguments = "proposal 1";
    second.tier = SessionTier::Propose;
    second.plan = 7;
    second.outcome = "applied";
    const auto secondSequence = log.record(second);

    REQUIRE(log.actions().size() == 2);
    CHECK(firstSequence == 1);
    CHECK(secondSequence == 2);
    CHECK(log.actions()[0].sequence == firstSequence);
    CHECK(log.actions()[1].sequence == secondSequence);
    CHECK(log.actions()[1].plan == 7);
    const auto entries = console->snapshot().entries;
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].actor == Actor::Agent);
    CHECK(entries[0].timestampMilliseconds == 1234);
    CHECK(entries[0].message == "propose.edits {\"summary\":\"lamp\"} -> proposed");
    CHECK(entries[1].actor == Actor::Operator);
    CHECK(entries[1].message == "Accept proposal 1 -> applied");

    log.attach(firstSequence, SessionEvidence{"session/frame.png", "a1b2"});
    REQUIRE(log.actions()[0].evidence.size() == 1);
    CHECK(log.actions()[0].evidence[0].path == "session/frame.png");
    CHECK(log.actions()[0].evidence[0].sha256 == "a1b2");
    CHECK(log.actions()[1].evidence.empty());
}

//======================================================================================================================
TEST_CASE("session evidence requires a recorded action", "[app][session]") {
    auto console = std::make_shared<ConsoleLog>();
    SessionLog log(console);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        log.attach(42, SessionEvidence{"missing", "hash"});
        _exit(0);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGABRT);
}

//======================================================================================================================
TEST_CASE("session export keeps filtered frozen Console entries intact", "[app][session-export]") {
    auto store = std::make_shared<ConsoleLog>();
    SessionLog log(store);
    log.record({.timestampMilliseconds = 4,
                .actor = Actor::Agent,
                .client = "tester",
                .command = "request",
                .arguments = "line one\nline two",
                .outcome = "ok"});
    log.record({.timestampMilliseconds = 5,
                .actor = Actor::Operator,
                .client = "tester",
                .command = "approval",
                .plan = 9,
                .outcome = "approved"});
    store->append(lmx::log::Level::Warning, 6, "keep\nmultiline", Actor::Operator);
    store->append(lmx::log::Level::Error, 7, "keep system", Actor::System);
    ConsoleModel model(store);
    model.filter.minimumSeverity = lmx::log::Level::Warning;
    model.filter.search = "keep";
    model.filter.actors = {false, true, false};
    model.setFrozen(true);
    store->append(lmx::log::Level::Error, 8, "keep new", Actor::Agent);
    const auto parsed = lmx::asset::JsonTokens::parse(sessionRecordJson(
        log, model.snapshot(), "scene.gltf", "document hash", "tester", model.filter));
    REQUIRE(parsed);
    const auto root = parsed->root();
    CHECK(root.find("schema")->asUInt() == 1);
    CHECK(root.find("protocol")->asUInt() == 1);
    CHECK(root.find("client")->asString() == "tester");
    CHECK(root.find("document")->find("path")->asString() == "scene.gltf");
    CHECK(root.find("document")->find("hash")->asString() == "document hash");
    const auto actions = *root.find("actions");
    REQUIRE(actions.size() == 2);
    CHECK(actions.at(0).find("sequence")->asUInt() == 1);
    CHECK(actions.at(1).find("sequence")->asUInt() == 2);
    CHECK(actions.at(1).find("plan")->asUInt() == 9);
    const auto console = *root.find("console");
    REQUIRE(console.size() == 1);
    const auto entry = console.at(0).asString();
    REQUIRE(entry);
    auto filter = model.filter;
    filter.actors = {true, false, true};
    CHECK(*entry + "\n" == consoleVisibleText(model.snapshot(), filter));
    CHECK(entry->find("keep\nmultiline") != std::string::npos);
    CHECK(consoleVisibleText(model.retainedSnapshot(), filter) != *entry + "\n");
}

//======================================================================================================================
TEST_CASE("session evidence hashes the explicit file bytes", "[app][session-export]") {
    const auto dir =
        std::filesystem::temp_directory_path() / ("lmx-evidence-" + std::to_string(getpid()));
    std::filesystem::create_directory(dir);
    const auto file = dir / "frame.png";
    {
        std::ofstream out(file, std::ios::binary);
        out << "abc";
    }
    const std::string expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    auto evidence = hashSessionEvidence(file);
    REQUIRE(evidence);
    CHECK(evidence->sha256 == expected);
    CHECK(evidence->path == file.string());
    const auto sequence = dir / "sequence";
    std::filesystem::create_directory(sequence);
    {
        std::ofstream out(sequence / "manifest.json", std::ios::binary);
        out << "abc";
    }
    CHECK_FALSE(hashSessionEvidence(sequence));
    evidence = hashSessionEvidence(sequence / "manifest.json");
    REQUIRE(evidence);
    CHECK(evidence->path == (sequence / "manifest.json").string());
    CHECK(evidence->sha256 == expected);
    const auto trace = dir / "frame.gputrace";
    std::filesystem::create_directory(trace);
    CHECK_FALSE(hashSessionEvidence(trace));
    const auto schema = std::filesystem::path(trace.string() + ".schema.json");
    {
        std::ofstream out(schema, std::ios::binary);
        out << "abc";
    }
    CHECK_FALSE(hashSessionEvidence(trace));
    evidence = hashSessionEvidence(schema);
    REQUIRE(evidence);
    CHECK(evidence->path == schema.string());
    CHECK(evidence->sha256 == expected);
    auto store = std::make_shared<ConsoleLog>();
    SessionLog log(store);
    const auto id = log.record({.command = "capture.gpu", .outcome = "applied"});
    log.attach(id, *evidence);
    const auto parsed =
        lmx::asset::JsonTokens::parse(sessionRecordJson(log, store->snapshot(), "", "", ""));
    REQUIRE(parsed);
    const auto item = parsed->root().find("actions")->at(0).find("evidence")->at(0);
    CHECK(item.find("path")->asString() == schema.string());
    CHECK(item.find("sha256")->asString() == expected);
    CHECK_FALSE(hashSessionEvidence(dir / "missing"));
    std::filesystem::remove_all(dir);
}

//======================================================================================================================
TEST_CASE("GPU evidence hashes every regular bundle file and schema in path order",
          "[app][session-export]") {
    const auto dir =
        std::filesystem::temp_directory_path() / ("lmx-trace-evidence-" + std::to_string(getpid()));
    const auto trace = dir / "capture.gputrace";
    std::filesystem::create_directories(trace / "nested");
    const auto schema = std::filesystem::path(trace.string() + ".schema.json");
    {
        std::ofstream out(schema, std::ios::binary);
        out << "schema";
    }
    {
        std::ofstream out(trace / "z.bin", std::ios::binary);
        out << "abc";
    }
    {
        std::ofstream out(trace / "nested/a.bin", std::ios::binary);
        out.write("x\0y", 3);
    }
    auto evidence = hashSessionOutputEvidence(trace);
    REQUIRE(evidence);
    REQUIRE(evidence->size() == 3);
    CHECK((*evidence)[0].path == schema.string());
    CHECK((*evidence)[1].path == (trace / "nested/a.bin").string());
    CHECK((*evidence)[2].path == (trace / "z.bin").string());
    CHECK((*evidence)[2].sha256 ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const auto original = *evidence;
    {
        std::ofstream out(trace / "nested/a.bin", std::ios::binary);
        out.write("x\0z", 3);
    }
    evidence = hashSessionOutputEvidence(trace);
    REQUIRE(evidence);
    CHECK((*evidence)[0].sha256 == original[0].sha256);
    CHECK((*evidence)[1].sha256 != original[1].sha256);
    CHECK((*evidence)[2].sha256 == original[2].sha256);
    std::filesystem::create_symlink(dir / "missing", trace / "unsafe");
    CHECK_FALSE(hashSessionOutputEvidence(trace));
    std::filesystem::remove(trace / "unsafe");
    std::filesystem::create_directory_symlink(dir, trace / "unsafe");
    CHECK_FALSE(hashSessionOutputEvidence(trace));
    std::filesystem::remove(trace / "unsafe");
    REQUIRE(mkfifo((trace / "unsafe").c_str(), 0600) == 0);
    CHECK_FALSE(hashSessionOutputEvidence(trace));
    std::filesystem::remove(trace / "unsafe");
    std::filesystem::permissions(trace / "z.bin", std::filesystem::perms::none);
    CHECK_FALSE(hashSessionOutputEvidence(trace));
    std::filesystem::permissions(trace / "z.bin", std::filesystem::perms::owner_read |
                                                      std::filesystem::perms::owner_write);
    std::filesystem::remove(schema);
    CHECK_FALSE(hashSessionOutputEvidence(trace));
    std::filesystem::remove_all(dir);
}

//======================================================================================================================
TEST_CASE("successful evidence is required and failed jobs may omit unwritten outputs",
          "[app][session-export]") {
    const auto dir =
        std::filesystem::temp_directory_path() / ("lmx-certification-" + std::to_string(getpid()));
    std::filesystem::create_directory(dir);
    std::vector<std::string> outputs{(dir / "missing.png").string(),
                                     (dir / "failure.log").string()};
    {
        std::ofstream out(outputs[1], std::ios::binary);
    }
    CHECK_FALSE(hashSessionOutputs(outputs, true));
    const auto evidence = hashSessionOutputs(outputs, false);
    REQUIRE(evidence);
    REQUIRE(evidence->size() == 1);
    CHECK((*evidence)[0].path == outputs[1]);
    CHECK((*evidence)[0].sha256 ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::filesystem::create_symlink(dir / "missing", outputs[0]);
    CHECK_FALSE(hashSessionOutputs(outputs, false));
    std::filesystem::remove_all(dir);
}
