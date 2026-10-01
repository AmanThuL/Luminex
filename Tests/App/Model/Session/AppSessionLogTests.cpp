#include "App/Model/Session/SessionLog.h"

#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <memory>
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
