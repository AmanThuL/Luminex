//----------------------------------------------------------------------------------------------------------------------
/// @file AppEditorActionsTests.cpp
/// @brief Tests the App-owned action intents the main menu and shortcuts emit.
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_test_macros.hpp>

#include "App/EditorActions.h"

using namespace lmx;
using namespace lmx::app;

//======================================================================================================================
TEST_CASE("a fresh EditorActions has no intent pending", "[app]") {
    EditorActions actions;

    REQUIRE_FALSE(actions.quitPending());
    REQUIRE_FALSE(actions.capturePending());
    REQUIRE_FALSE(actions.resetLayoutPending());
}

//======================================================================================================================
// Requesting only ever changes the pending flag it names -- there is no callback or other side
// effect a request can trigger, so inspecting is the whole observable surface of "emission only".
TEST_CASE("requesting an intent makes it pending and nothing else", "[app]") {
    EditorActions actions;

    actions.requestCapture();

    REQUIRE(actions.capturePending());
    REQUIRE_FALSE(actions.quitPending());
    REQUIRE_FALSE(actions.resetLayoutPending());
}

//======================================================================================================================
// The menu and the keyboard shortcut can both raise a capture request in the same or successive
// frames before the frame loop consumes one; they must still leave exactly one pending occurrence.
TEST_CASE("duplicate capture requests coalesce into one pending request", "[app]") {
    EditorActions actions;

    actions.requestCapture();
    actions.requestCapture();
    actions.requestCapture();

    REQUIRE(actions.capturePending());
    REQUIRE(actions.consumeCapture());
    REQUIRE_FALSE(actions.consumeCapture());
}

//======================================================================================================================
// A skipped drawable must not lose the request: inspecting across several simulated frames without
// consuming leaves the intent exactly as pending as it started.
TEST_CASE("an unconsumed capture request survives frames it is not consumed on", "[app]") {
    EditorActions actions;
    actions.requestCapture();

    for (int frame = 0; frame < 3; ++frame) {
        REQUIRE(actions.capturePending());
    }

    REQUIRE(actions.consumeCapture());
    REQUIRE_FALSE(actions.capturePending());
}

//======================================================================================================================
// One consumption clears the request regardless of what the caller does with the answer -- a
// failed capture attempt is consumed and logged, not retried on the next frame.
TEST_CASE("consuming a capture request clears it, and a second consume reports nothing pending",
          "[app]") {
    EditorActions actions;
    actions.requestCapture();

    REQUIRE(actions.consumeCapture());
    REQUIRE_FALSE(actions.consumeCapture());
    REQUIRE_FALSE(actions.capturePending());
}

//======================================================================================================================
// Quit follows the same request/retain/consume rules as capture.
TEST_CASE("a quit request coalesces, retains, and consumes once", "[app]") {
    EditorActions actions;

    actions.requestQuit();
    actions.requestQuit();
    REQUIRE(actions.quitPending());
    REQUIRE(actions.quitPending()); // inspecting again does not consume it

    REQUIRE(actions.consumeQuit());
    REQUIRE_FALSE(actions.consumeQuit());
    REQUIRE_FALSE(actions.quitPending());
}

//======================================================================================================================
// Reset Default Layout follows the same request/retain/consume rules as capture.
TEST_CASE("a reset-layout request coalesces, retains, and consumes once", "[app]") {
    EditorActions actions;

    actions.requestResetLayout();
    actions.requestResetLayout();
    REQUIRE(actions.resetLayoutPending());
    REQUIRE(actions.resetLayoutPending()); // inspecting again does not consume it

    REQUIRE(actions.consumeResetLayout());
    REQUIRE_FALSE(actions.consumeResetLayout());
    REQUIRE_FALSE(actions.resetLayoutPending());
}

//======================================================================================================================
// The three intents are independent: requesting or consuming one must never leak into another.
TEST_CASE("independent intents do not interfere with each other", "[app]") {
    EditorActions actions;

    actions.requestQuit();
    REQUIRE(actions.quitPending());
    REQUIRE_FALSE(actions.capturePending());
    REQUIRE_FALSE(actions.resetLayoutPending());

    actions.requestCapture();
    REQUIRE(actions.consumeQuit());
    REQUIRE_FALSE(actions.quitPending());
    REQUIRE(actions.capturePending());
    REQUIRE_FALSE(actions.resetLayoutPending());

    actions.requestResetLayout();
    REQUIRE(actions.consumeCapture());
    REQUIRE_FALSE(actions.capturePending());
    REQUIRE(actions.resetLayoutPending());
    REQUIRE_FALSE(actions.quitPending());

    REQUIRE(actions.consumeResetLayout());
    REQUIRE_FALSE(actions.resetLayoutPending());
}
