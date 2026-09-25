//----------------------------------------------------------------------------------------------------------------------
/// @file AppEditorShortcutsTests.cpp
/// @brief Tests global editor shortcut focus and capability gates.
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_test_macros.hpp>

#include "App/Model/Capture/EditorShortcuts.h"

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("editor shortcuts reject text look and popup contexts", "[app][shortcuts]") {
    for (auto shortcut :
         {EditorShortcut::FrameSelected, EditorShortcut::ResetCamera, EditorShortcut::Capture}) {
        for (unsigned flags = 1; flags < 8; ++flags) {
            INFO(flags);
            REQUIRE_FALSE(shortcutAllowed(
                shortcut, {bool(flags & 1), bool(flags & 2), bool(flags & 4), true, true}));
        }
        REQUIRE(shortcutAllowed(shortcut, {false, false, false, true, true}));
    }
}

//======================================================================================================================
TEST_CASE("editor shortcuts require their own capability", "[app][shortcuts]") {
    REQUIRE_FALSE(shortcutAllowed(EditorShortcut::FrameSelected, {}));
    REQUIRE_FALSE(shortcutAllowed(EditorShortcut::Capture, {}));
    REQUIRE(shortcutAllowed(EditorShortcut::ResetCamera, {}));
    REQUIRE(shortcutAllowed(EditorShortcut::FrameSelected, {false, false, false, false, true}));
    REQUIRE(shortcutAllowed(EditorShortcut::Capture, {false, false, false, true, false}));
}
