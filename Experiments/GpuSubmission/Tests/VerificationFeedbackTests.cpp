//----------------------------------------------------------------------------------------------------------------------
/// @file VerificationFeedbackTests.cpp
/// @brief Tests failed, delayed, and independently owned commit callbacks without creating a GPU.
//----------------------------------------------------------------------------------------------------------------------
#include "Metal/VerificationFeedback.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <future>
#include <memory>
#include <thread>

namespace {
using Feedback = lmx::experimental::submission::detail::VerificationFeedback;
using Clock = std::chrono::steady_clock;
} // namespace

//======================================================================================================================
TEST_CASE("submission verification waits for delayed and out-of-order callback delivery",
          "[unit][submission][native][feedback]") {
    auto ledger = std::make_shared<Feedback>(2);
    const auto first = ledger->recordSubmission();
    const auto second = ledger->recordSubmission();
    ledger->complete(second, {});
    REQUIRE_FALSE(ledger->deliveredThrough(second));
    const auto incomplete = ledger->waitThrough(second, Clock::now());
    REQUIRE_FALSE(incomplete.has_value());
    REQUIRE(incomplete.error().find("delivery timed out") != std::string::npos);

    std::promise<void> release;
    std::jthread callback([ledger, first, gate = release.get_future()]() mutable {
        gate.wait();
        ledger->complete(first, {});
    });
    release.set_value();
    const auto delivered = ledger->waitThrough(second, Clock::now() + std::chrono::seconds(1));
    REQUIRE(delivered.has_value());
    REQUIRE(ledger->deliveredThrough(second));
}

//======================================================================================================================
TEST_CASE("submission verification errors remain hard failures while outstanding callbacks drain",
          "[unit][submission][native][feedback]") {
    Feedback ledger(2);
    const auto first = ledger.recordSubmission();
    const auto second = ledger.recordSubmission();
    ledger.complete(first, "injected GPU timeout: case=test slot=0 frame=7 kind=raster");
    const auto failed = ledger.waitThrough(first, Clock::now());
    REQUIRE_FALSE(failed.has_value());
    REQUIRE(failed.error().find("injected GPU timeout") != std::string::npos);
    REQUIRE_FALSE(ledger.deliveredThrough(second));
    const auto stillPending = ledger.waitThrough(second, Clock::now());
    REQUIRE_FALSE(stillPending.has_value());
    REQUIRE(stillPending.error().find("delivery timed out") != std::string::npos);
    REQUIRE(stillPending.error().find("injected GPU timeout") != std::string::npos);

    ledger.complete(second, {});
    REQUIRE(ledger.deliveredThrough(second));
    const auto drainedFailure = ledger.waitThrough(second, Clock::now());
    REQUIRE_FALSE(drainedFailure.has_value());
    REQUIRE(drainedFailure.error() == failed.error());
}

//======================================================================================================================
TEST_CASE(
    "submission callback state survives the submitting owner without retaining native objects",
    "[unit][submission][native][feedback]") {
    auto owner = std::make_shared<Feedback>(1);
    const auto sequence = owner->recordSubmission();
    std::weak_ptr<Feedback> weak = owner;
    std::function<void()> callback = [ledger = owner, sequence] { ledger->complete(sequence, {}); };
    owner.reset();
    REQUIRE_FALSE(weak.expired());
    callback();
    REQUIRE(weak.lock()->waitThrough(sequence, Clock::now()).has_value());
    callback = {};
    REQUIRE(weak.expired());
}

//======================================================================================================================
TEST_CASE("submission callback ledger rejects duplicate or unknown commit identities",
          "[unit][submission][native][feedback]") {
    Feedback ledger(1);
    const auto sequence = ledger.recordSubmission();
    REQUIRE_FALSE(ledger.waitThrough(sequence + 1, Clock::now()).has_value());
    ledger.complete(sequence, {});
    REQUIRE(ledger.waitThrough(sequence, Clock::now()).has_value());
    ledger.complete(sequence, {});
    const auto duplicate = ledger.waitThrough(sequence, Clock::now());
    REQUIRE_FALSE(duplicate.has_value());
    REQUIRE(duplicate.error().find("Duplicate or unknown") != std::string::npos);
}
