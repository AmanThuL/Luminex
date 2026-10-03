//----------------------------------------------------------------------------------------------------------------------
/// @file EditorIcon.h
/// @brief Names editor actions and their pinned Codicons glyphs and fallback labels.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <string>
#include <string_view>

namespace lmx::app {

/// Editor actions with glyphs from the pinned Codicons font.
enum class EditorIcon : char32_t {
    Play = 0xEAD3,         ///< Start or resume playback.
    Pause = 0xEAD1,        ///< Pause playback.
    Stop = 0xEAD7,         ///< Stop playback or measurement.
    Step = 0xEAD6,         ///< Advance one frame.
    Reset = 0xEAE2,        ///< Restore the subject defaults.
    Close = 0xEA76,        ///< Dismiss a surface or clear a field.
    More = 0xEA7C,         ///< Open the overflow menu.
    Details = 0xEB14,      ///< Open the detailed diagnostic window.
    NewBelow = 0xEA9A,     ///< Resume at the newest log entry.
    FitGraph = 0xEB4C,     ///< Fit the whole graph.
    FitSelection = 0xEBF8, ///< Fit the selected subject.
    Lock = 0xEA75,         ///< Freeze a diagnostic snapshot.
    Unlock = 0xEB74,       ///< Resume diagnostic publication.
    Rail = 0xEADA,         ///< Follow the authored camera rail.
    Movable = 0xEB22,      ///< Subject mobility permits pose editing; also the Move tool glyph.
    View = 0xEA70,         ///< Hide the transform gizmo.
    Rotate = 0xEB37,       ///< Rotate the selected subject.
    Scale = 0xEA99,        ///< Scale the selected object.
    Transform = 0xEBB6,    ///< Combine the supported transform operations.
    World = 0xEB01,        ///< Use world transform axes.
    Local = 0xEB63,        ///< Use local transform axes.
};

/// A font glyph and a readable label used when the icon font is unavailable.
struct EditorIconInfo {
    char32_t codepoint;     ///< Pinned Unicode private-use code point.
    std::string_view label; ///< Static storage; non-empty action label.
};

/// Returns the pinned glyph and fallback label for a valid EditorIcon.
EditorIconInfo editorIconInfo(EditorIcon icon);

/// Encodes one Unicode scalar value as UTF-8; asserts on surrogates or values above U+10FFFF.
std::string encodeUtf8(char32_t codepoint);

} // namespace lmx::app
