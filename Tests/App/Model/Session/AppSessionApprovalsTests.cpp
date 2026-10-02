#include "App/Model/Session/SessionApprovals.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
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

//======================================================================================================================
TEST_CASE("terminal approval results are consumed exactly once", "[app][session-approval]") {
    SessionApprovals approvals;
    const auto pending =
        approvals.submit(18, "client", "pending", {{SessionCommand::GraphDump, "{}"}});
    REQUIRE(pending);
    CHECK(approvals.takeTerminalResults().empty());
    approvals.cancelPending();
    auto results = approvals.takeTerminalResults();
    REQUIRE(results.size() == 1);
    CHECK(results[0].request == 18);
    CHECK(results[0].terminalError == SessionError::Cancelled);
    approvals.cancelAll();
    CHECK(approvals.takeTerminalResults().empty());
    const auto running =
        approvals.submit(28, "client", "running", {{SessionCommand::CaptureScreenshot, "{}"}});
    REQUIRE(running);
    approvals.approve(*running);
    REQUIRE(approvals.next());
    approvals.cancelAll();
    CHECK_FALSE(approvals.finishStep(*running, true));
    results = approvals.takeTerminalResults();
    REQUIRE(results.size() == 1);
    CHECK(results[0].request == 28);
    CHECK(results[0].terminalError == SessionError::Cancelled);
    CHECK(approvals.takeTerminalResults().empty());
}

//======================================================================================================================
TEST_CASE("unresolved approvals are capped and reported results leave the queue",
          "[app][session-approvals]") {
    SessionApprovals approvals;
    std::vector<uint64_t> ids;
    for (uint64_t request = 0; request < kMaxUnresolvedApprovals; ++request) {
        const auto id =
            approvals.submit(request, "client", "queued", {{SessionCommand::GraphDump, "{}"}});
        REQUIRE(id);
        ids.push_back(*id);
    }
    CHECK_FALSE(approvals.submit(99, "client", "ninth", {{SessionCommand::GraphDump, "{}"}}));
    CHECK(approvals.pending().size() == kMaxUnresolvedApprovals);

    approvals.approve(ids[0]);
    CHECK_FALSE(approvals.submit(99, "client", "working", {{SessionCommand::GraphDump, "{}"}}));
    REQUIRE(approvals.next());
    approvals.finishStep(true);
    const auto admitted =
        approvals.submit(100, "client", "admitted", {{SessionCommand::GraphDump, "{}"}});
    REQUIRE(admitted);
    CHECK(approvals.pending().size() == kMaxUnresolvedApprovals + 1);

    approvals.deny(ids[1]);
    const auto results = approvals.takeTerminalResults();
    REQUIRE(results.size() == 2);
    CHECK(results[0].id == ids[0]);
    CHECK(results[0].state == SessionState::Applied);
    CHECK(results[1].id == ids[1]);
    CHECK(results[1].terminalError == SessionError::Denied);
    REQUIRE(approvals.pending().size() == kMaxUnresolvedApprovals - 1);
    CHECK(approvals.pending().front().id == ids[2]);
    CHECK(approvals.pending().back().id == *admitted);
    CHECK(approvals.active()->id == ids[2]);
    CHECK(approvals.takeTerminalResults().empty());
}

//======================================================================================================================
TEST_CASE("a newly shown approval card ignores clicks for half a second",
          "[app][session-approvals]") {
    ApprovalClickGuard guard;
    CHECK_FALSE(guard.accepts(0, 10.0));
    CHECK_FALSE(guard.accepts(7, 10.0));
    CHECK_FALSE(guard.accepts(7, 10.49));
    CHECK(guard.accepts(7, 10.5));
    CHECK(guard.accepts(7, 30.0));
    // The next queued request takes the same position in the frame after a decision.
    CHECK_FALSE(guard.accepts(8, 30.01));
    CHECK_FALSE(guard.accepts(8, 30.5));
    CHECK(guard.accepts(8, 30.51));
    // An approved plan hides the card while it runs; the request behind it starts a new delay.
    CHECK_FALSE(guard.accepts(0, 31.0));
    CHECK_FALSE(guard.accepts(9, 40.0));
    CHECK(guard.accepts(9, 40.5));
    CHECK_FALSE(guard.accepts(0, 60.0));
    CHECK_FALSE(guard.accepts(9, 60.0));
}

//======================================================================================================================
TEST_CASE("a cancelled approval carries the reason its client is told",
          "[app][session-approvals]") {
    SessionApprovals approvals;
    const auto running =
        approvals.submit(1, "client", "running", {{SessionCommand::GraphDump, "{}"}});
    const auto waiting =
        approvals.submit(2, "client", "waiting", {{SessionCommand::SceneOpen, "{}"}});
    REQUIRE(running);
    REQUIRE(waiting);
    approvals.approve(*running);
    approvals.cancelPending("cancelled: scene replaced");
    // The approved plan keeps running and carries no reason.
    CHECK(approvals.pending()[0].state == SessionState::Working);
    CHECK(approvals.pending()[0].terminalMessage.empty());
    CHECK(approvals.pending()[1].terminalError == SessionError::Cancelled);
    CHECK(approvals.pending()[1].terminalMessage == "cancelled: scene replaced");
    const auto later = approvals.submit(3, "client", "later", {{SessionCommand::GraphDump, "{}"}});
    REQUIRE(later);
    approvals.cancelPending();
    CHECK(approvals.pending()[2].terminalError == SessionError::Cancelled);
    CHECK(approvals.pending()[2].terminalMessage.empty());
    // A second cancellation does not rewrite the reason of a request already cancelled.
    approvals.cancelPending("cancelled: client disconnected");
    CHECK(approvals.pending()[1].terminalMessage == "cancelled: scene replaced");
}

//======================================================================================================================
TEST_CASE("proposal cards ignore clicks for half a second after the card list changes",
          "[app][session-approvals]") {
    CardListClickGuard guard;
    const std::vector<uint64_t> three{0, 4, 5, 6};
    CHECK_FALSE(guard.accepts(three, 10.0));
    CHECK_FALSE(guard.accepts(three, 10.49));
    CHECK(guard.accepts(three, 10.5));
    CHECK(guard.accepts(three, 30.0));
    // Rejecting the middle card moves the last one into its place.
    const std::vector<uint64_t> two{0, 4, 6};
    CHECK_FALSE(guard.accepts(two, 30.01));
    CHECK_FALSE(guard.accepts(two, 30.5));
    CHECK(guard.accepts(two, 30.51));
    // An approval card appearing above moves every proposal card down.
    const std::vector<uint64_t> withApproval{9, 4, 6};
    CHECK_FALSE(guard.accepts(withApproval, 40.0));
    CHECK(guard.accepts(withApproval, 40.5));
    // The same cards in another order are a different layout.
    const std::vector<uint64_t> reordered{9, 6, 4};
    CHECK_FALSE(guard.accepts(reordered, 41.0));
    CHECK(guard.accepts(reordered, 41.5));
    // No approval and no proposal: nothing to click, and the next card starts its own delay.
    const std::vector<uint64_t> none{0};
    CHECK_FALSE(guard.accepts(none, 50.0));
    const std::vector<uint64_t> one{0, 7};
    CHECK_FALSE(guard.accepts(one, 60.0));
    CHECK(guard.accepts(one, 60.5));
}
