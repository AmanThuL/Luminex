//----------------------------------------------------------------------------------------------------------------------
/// @file AppEditorShortcutsTests.cpp
/// @brief Tests global editor shortcut focus and capability gates.
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_test_macros.hpp>

#include "App/Model/Capture/EditorShortcuts.h"

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("editor shortcuts reject text look and popup contexts", "[app][shortcuts]") {
    for (auto shortcut : {EditorShortcut::FrameSelected, EditorShortcut::ResetCamera,
                          EditorShortcut::Capture, EditorShortcut::Document}) {
        for (unsigned flags = 1; flags < 8; ++flags) {
            INFO(flags);
            REQUIRE_FALSE(shortcutAllowed(
                shortcut, {bool(flags & 1), bool(flags & 2), bool(flags & 4), false, true}));
        }
        REQUIRE(shortcutAllowed(shortcut, {false, false, false, false, true}));
    }
}

//======================================================================================================================
TEST_CASE("editor shortcuts reject focus on a detached surface", "[app][shortcuts]") {
    for (auto shortcut : {EditorShortcut::FrameSelected, EditorShortcut::ResetCamera,
                          EditorShortcut::Capture, EditorShortcut::Document}) {
        INFO(static_cast<int>(shortcut));
        REQUIRE_FALSE(
            shortcutAllowed(shortcut, {.otherSurfaceFocused = true, .hasSelection = true}));
        REQUIRE(shortcutAllowed(shortcut, {.hasSelection = true}));
    }
}

//======================================================================================================================
TEST_CASE("editor shortcuts require their own capability", "[app][shortcuts]") {
    REQUIRE_FALSE(shortcutAllowed(EditorShortcut::FrameSelected, {}));
    REQUIRE(shortcutAllowed(EditorShortcut::ResetCamera, {}));
    REQUIRE(shortcutAllowed(EditorShortcut::FrameSelected, {.hasSelection = true}));
}

//======================================================================================================================
TEST_CASE("capture shortcut reaches the intent without availability so it can explain recovery",
          "[app][shortcuts]") {
    REQUIRE(shortcutAllowed(EditorShortcut::Capture, {}));
    REQUIRE_FALSE(shortcutAllowed(EditorShortcut::Capture, {.textInput = true}));
    REQUIRE_FALSE(shortcutAllowed(EditorShortcut::Capture, {.otherSurfaceFocused = true}));
}

//======================================================================================================================
TEST_CASE("document shortcuts need no selected object", "[app][shortcuts]") {
    REQUIRE(shortcutAllowed(EditorShortcut::Document, {}));
    REQUIRE_FALSE(shortcutAllowed(EditorShortcut::Document, {.popupOpen = true}));
}

//======================================================================================================================
TEST_CASE("quit ignores every focus gate", "[app][shortcuts]") {
    for (unsigned flags = 0; flags < 16; ++flags) {
        INFO(flags);
        REQUIRE(shortcutAllowed(EditorShortcut::Quit, {bool(flags & 1), bool(flags & 2),
                                                       bool(flags & 4), bool(flags & 8)}));
    }
}
