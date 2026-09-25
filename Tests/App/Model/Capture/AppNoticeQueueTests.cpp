//----------------------------------------------------------------------------------------------------------------------
/// @file AppNoticeQueueTests.cpp
/// @brief Tests transient success notices and retained exceptional results.
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_test_macros.hpp>

#include "App/Model/Capture/NoticeQueue.h"

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("success notices expire six seconds after posting", "[app][notice]") {
    NoticeQueue notices;
    notices.post({ActionStatus::Succeeded, "Saved", "/frame.gputrace"}, 10.0);
    REQUIRE(notices.current(10.0));
    REQUIRE(notices.current(15.999));
    REQUIRE(notices.current(15.999)->path == "/frame.gputrace");
    REQUIRE_FALSE(notices.current(16.0));
    REQUIRE_FALSE(notices.current(100.0));
}

//======================================================================================================================
TEST_CASE("exception notices remain until dismissed or replaced", "[app][notice]") {
    for (auto status : {ActionStatus::Failed, ActionStatus::Pending, ActionStatus::Unavailable}) {
        NoticeQueue notices;
        notices.post({status, "Capture result", {}}, 1.0);
        REQUIRE(notices.current(1000.0));
        REQUIRE(notices.current(1000.0)->status == status);
        notices.dismiss();
        REQUIRE_FALSE(notices.current(1000.0));
        notices.post({status, "Second result", {}}, 1001.0);
        REQUIRE(notices.current(1001.0));
        notices.post({ActionStatus::Succeeded, "Saved", "/new.gputrace"}, 1002.0);
        REQUIRE(notices.current(1007.0)->status == ActionStatus::Succeeded);
        REQUIRE_FALSE(notices.current(1008.0));
    }
}

//======================================================================================================================
TEST_CASE("new notices replace the message and success expiry", "[app][notice]") {
    NoticeQueue notices;
    notices.post({ActionStatus::Succeeded, "First", "/first"}, 0.0);
    notices.post({ActionStatus::Succeeded, "Second", "/second"}, 5.0);
    REQUIRE(notices.current(6.0));
    REQUIRE(notices.current(6.0)->message == "Second");
    REQUIRE(notices.current(6.0)->path == "/second");
    REQUIRE(notices.current(10.999));
    REQUIRE_FALSE(notices.current(11.0));
}

//======================================================================================================================
TEST_CASE("ready and empty results suppress and replace notices", "[app][notice]") {
    NoticeQueue notices;
    REQUIRE_FALSE(notices.current(0.0));
    for (const ActionResult& result :
         {ActionResult{}, ActionResult{ActionStatus::Ready, "Ready", "/unused"},
          ActionResult{ActionStatus::Failed, {}, {}}}) {
        notices.post({ActionStatus::Pending, "Waiting", {}}, 0.0);
        notices.post(result, 1.0);
        REQUIRE_FALSE(notices.current(1.0));
    }
    notices.post({ActionStatus::Succeeded, {}, "/only-path"}, 2.0);
    REQUIRE(notices.current(2.0));
    notices.dismiss();
    REQUIRE_FALSE(notices.current(2.0));
}

//======================================================================================================================
TEST_CASE("notice feedback retains output-action errors without replacing its lifetime",
          "[app][notice]") {
    NoticeQueue notices;
    notices.post({ActionStatus::Succeeded, "Saved", "/frame.gputrace"}, 10.0);
    auto* result = notices.current(11.0);
    REQUIRE(result);
    result->pathActionError = "Copy path failed";
    const NoticeQueue& readOnly = notices;
    REQUIRE(readOnly.current(12.0));
    REQUIRE(readOnly.current(12.0)->pathActionError == "Copy path failed");
    REQUIRE(readOnly.current(12.0)->path == "/frame.gputrace");
    REQUIRE_FALSE(readOnly.current(16.0));
    notices.post({ActionStatus::Failed, "Write failed", "/other.gputrace"}, 17.0);
    REQUIRE(notices.current(17.0)->pathActionError.empty());
}
