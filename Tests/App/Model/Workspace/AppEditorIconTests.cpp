#include "App/Model/Workspace/EditorIcon.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <set>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("editor icons use the pinned Codicons code points and readable labels", "[app][icons]") {
    const std::array expected{
        std::pair{EditorIcon::Play, U'\uEAD3'},         std::pair{EditorIcon::Pause, U'\uEAD1'},
        std::pair{EditorIcon::Stop, U'\uEAD7'},         std::pair{EditorIcon::Step, U'\uEAD6'},
        std::pair{EditorIcon::Reset, U'\uEAE2'},        std::pair{EditorIcon::Close, U'\uEA76'},
        std::pair{EditorIcon::More, U'\uEA7C'},         std::pair{EditorIcon::Details, U'\uEB14'},
        std::pair{EditorIcon::NewBelow, U'\uEA9A'},     std::pair{EditorIcon::FitGraph, U'\uEB4C'},
        std::pair{EditorIcon::FitSelection, U'\uEBF8'}, std::pair{EditorIcon::Lock, U'\uEA75'},
        std::pair{EditorIcon::Unlock, U'\uEB74'},       std::pair{EditorIcon::Rail, U'\uEADA'},
        std::pair{EditorIcon::Movable, U'\uEB22'},      std::pair{EditorIcon::View, U'\uEA70'},
        std::pair{EditorIcon::Rotate, U'\uEB37'},       std::pair{EditorIcon::Scale, U'\uEA99'},
        std::pair{EditorIcon::Transform, U'\uEBB6'},    std::pair{EditorIcon::World, U'\uEB01'},
        std::pair{EditorIcon::Local, U'\uEB63'},
    };
    std::set<char32_t> codepoints;
    for (const auto& [icon, codepoint] : expected) {
        const auto info = editorIconInfo(icon);
        REQUIRE(info.codepoint == codepoint);
        REQUIRE_FALSE(info.label.empty());
        REQUIRE(info.codepoint >= 0xEA60);
        REQUIRE(info.codepoint <= 0xEC40);
        REQUIRE(codepoints.insert(info.codepoint).second);
    }
}

//======================================================================================================================
TEST_CASE("Movable hierarchy icon has a readable font fallback", "[app][icons][mobility-display]") {
    CHECK(editorIconInfo(EditorIcon::Movable).label == "Movable");
}

//======================================================================================================================
TEST_CASE("editor icon UTF-8 encoding handles Unicode scalar widths", "[app][icons]") {
    REQUIRE(encodeUtf8(0xEAD3) == "\xEE\xAB\x93");
    REQUIRE(encodeUtf8(U'A') == "A");
    REQUIRE(encodeUtf8(0x7F) == "\x7F");
    REQUIRE(encodeUtf8(0x80) == "\xC2\x80");
    REQUIRE(encodeUtf8(0x7FF) == "\xDF\xBF");
    REQUIRE(encodeUtf8(0x800) == "\xE0\xA0\x80");
    REQUIRE(encodeUtf8(0xFFFF) == "\xEF\xBF\xBF");
    REQUIRE(encodeUtf8(0x10000) == "\xF0\x90\x80\x80");
    REQUIRE(encodeUtf8(0x10FFFF) == "\xF4\x8F\xBF\xBF");
}

//======================================================================================================================
TEST_CASE("gizmo toolbar icons resolve readable pinned glyphs", "[app][icons][gizmo-tools]") {
    for (auto icon : {EditorIcon::View, EditorIcon::Movable, EditorIcon::Rotate, EditorIcon::Scale,
                      EditorIcon::Transform, EditorIcon::World, EditorIcon::Local}) {
        CHECK_FALSE(editorIconInfo(icon).label.empty());
        CHECK(encodeUtf8(editorIconInfo(icon).codepoint).size() == 3);
    }
}
