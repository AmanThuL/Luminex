#include "App/Model/Session/SessionApprovals.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("session approvals reject every non-executable plan shape", "[app][session-approvals]") {
    SessionApprovals approvals;
    CHECK_FALSE(approvals.submit(1, "client", "empty", {}));
    CHECK_FALSE(approvals.submit(1, "client", "long",
                                 std::vector<ApprovalStep>(33, {SessionCommand::GraphDump, "{}"})));
    const auto limit = approvals.submit(
        1, "client", "limit", std::vector<ApprovalStep>(32, {SessionCommand::GraphDump, "{}"}));
    REQUIRE(limit);
    for (const auto& spec : sessionCommands()) {
        if (spec.tier == SessionTier::Apply && spec.command != SessionCommand::PlanSubmit)
            continue;
        INFO(spec.name);
        CHECK_FALSE(approvals.submit(1, "client", "invalid", {{spec.command, "{}"}}));
    }
    CHECK(approvals.pending().size() == 1);
    CHECK(approvals.active()->id == *limit);
}

//======================================================================================================================
TEST_CASE("approval copies a bounded plan and executes its steps once in order",
          "[app][session-approvals]") {
    SessionApprovals approvals;
    std::vector<ApprovalStep> submitted = {{SessionCommand::SettingsSet, R"({"temporal":"off"})"},
                                           {SessionCommand::GraphDump, R"({"name":"graph"})"}};
    const auto first = approvals.submit(41, "client", "two steps", submitted);
    REQUIRE(first);
    submitted[0].arguments = "changed after submission";
    submitted.clear();
    const auto second =
        approvals.submit(42, "client", "single", {{SessionCommand::CaptureGpu, "{}"}});
    REQUIRE(second);
    REQUIRE(approvals.active());
    CHECK(approvals.active()->id == *first);
    CHECK(approvals.active()->request == 41);
    CHECK(approvals.active()->state == SessionState::Awaiting);
    CHECK(approvals.active()->steps[0].arguments == R"({"temporal":"off"})");
    CHECK(approvals.next() == nullptr);

    approvals.approve(*second);
    approvals.deny(*second);
    CHECK(approvals.active()->id == *first);
    CHECK(approvals.next() == nullptr);
    approvals.approve(*first);
    REQUIRE(approvals.next());
    CHECK(approvals.next() == nullptr);
    CHECK(approvals.active()->cursor == 0);
    approvals.finishStep(true);
    CHECK(approvals.active()->cursor == 1);
    REQUIRE(approvals.next());
    CHECK(approvals.active()->steps[1].command == SessionCommand::GraphDump);
    approvals.finishStep(true);
    CHECK(approvals.pending()[0].state == SessionState::Applied);
    CHECK(approvals.active()->id == *second);
    CHECK(approvals.next() == nullptr);
    approvals.approve(*second);
    REQUIRE(approvals.next());
    approvals.finishStep(true);
    CHECK(approvals.active() == nullptr);
    CHECK(approvals.pending()[1].state == SessionState::Applied);
}

//======================================================================================================================
TEST_CASE("denial and failure produce distinct terminal results and stop remaining steps",
          "[app][session-approvals]") {
    SessionApprovals approvals;
    const auto denied =
        approvals.submit(1, "client", "denied", {{SessionCommand::SceneOpen, "{}"}});
    const auto failed =
        approvals.submit(2, "client", "failed",
                         {{SessionCommand::SettingsSet, "{}"}, {SessionCommand::GraphDump, "{}"}});
    REQUIRE(denied);
    REQUIRE(failed);
    approvals.deny(*denied);
    CHECK(approvals.pending()[0].state == SessionState::Error);
    CHECK(approvals.pending()[0].terminalError == SessionError::Denied);
    CHECK(approvals.active()->id == *failed);
    approvals.approve(*failed);
    REQUIRE(approvals.next());
    approvals.finishStep(false);
    CHECK(approvals.pending()[1].state == SessionState::Error);
    CHECK(approvals.pending()[1].terminalError == SessionError::Failed);
    CHECK(approvals.pending()[1].cursor == 0);
    CHECK(approvals.next() == nullptr);
}

//======================================================================================================================
TEST_CASE("cancel drops waiting approvals while an approved plan finishes",
          "[app][session-approvals]") {
    SessionApprovals approvals;
    const auto running = approvals.submit(
        1, "client", "running",
        {{SessionCommand::CaptureSequence, "{}"}, {SessionCommand::GraphDump, "{}"}});
    const auto waiting =
        approvals.submit(2, "client", "waiting", {{SessionCommand::SceneOpen, "{}"}});
    REQUIRE(running);
    REQUIRE(waiting);
    approvals.approve(*running);
    const auto* step = approvals.next();
    REQUIRE(step);
    CHECK(step->command == SessionCommand::CaptureSequence);
    approvals.cancelPending();
    CHECK(approvals.pending()[1].state == SessionState::Error);
    CHECK(approvals.pending()[1].terminalError == SessionError::Cancelled);
    CHECK(approvals.active()->id == *running);
    CHECK(approvals.next() == nullptr);
    approvals.finishStep(true);
    REQUIRE(approvals.next());
    CHECK(approvals.active()->steps[1].command == SessionCommand::GraphDump);
    approvals.finishStep(true);
    CHECK(approvals.pending()[0].state == SessionState::Applied);
    CHECK(approvals.active() == nullptr);
}

//======================================================================================================================
TEST_CASE("a late completion cannot advance a new approval after cancellation",
          "[app][session-approvals]") {
    SessionApprovals approvals;
    const auto old = approvals.submit(7, "first", "old", {{SessionCommand::MeasureRun, "{}"}});
    REQUIRE(old);
    approvals.approve(*old);
    REQUIRE(approvals.next());
    approvals.cancelAll();
    const auto fresh = approvals.submit(8, "second", "new", {{SessionCommand::GraphDump, "{}"}});
    REQUIRE(fresh);
    CHECK_FALSE(approvals.finishStep(*old, true));
    REQUIRE(approvals.active());
    CHECK(approvals.active()->id == *fresh);
    CHECK(approvals.active()->state == SessionState::Awaiting);
}

//======================================================================================================================
TEST_CASE("stale approval identities and invalid commands cannot advance work",
          "[app][session-approvals]") {
    SessionApprovals approvals;
    CHECK_FALSE(
        approvals.submit(1, "client", "invalid", {{static_cast<SessionCommand>(255), "{}"}}));
    const auto id = approvals.submit(1, "client", "valid", {{SessionCommand::SettingsSet, "{}"}});
    REQUIRE(id);
    approvals.approve(*id + 1);
    approvals.deny(*id + 1);
    CHECK(approvals.active()->state == SessionState::Awaiting);
    CHECK(approvals.next() == nullptr);
    approvals.approve(*id);
    approvals.approve(*id);
    approvals.deny(*id);
    REQUIRE(approvals.next());
    approvals.finishStep(true);
    approvals.approve(*id);
    approvals.deny(*id);
    CHECK(approvals.pending()[0].state == SessionState::Applied);
}
