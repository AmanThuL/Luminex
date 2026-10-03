//----------------------------------------------------------------------------------------------------------------------
/// @file EditorIcon.cpp
/// @brief Maps editor actions to Codicons and encodes glyphs for UI text.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Workspace/EditorIcon.h"

#include "Core/Diagnostics/Assert.h"

#include <utility>

namespace lmx::app {

//======================================================================================================================
EditorIconInfo editorIconInfo(EditorIcon icon) {
    switch (icon) {
    case EditorIcon::Play:
        return {static_cast<char32_t>(icon), "Play"};
    case EditorIcon::Pause:
        return {static_cast<char32_t>(icon), "Pause"};
    case EditorIcon::Stop:
        return {static_cast<char32_t>(icon), "Stop"};
    case EditorIcon::Step:
        return {static_cast<char32_t>(icon), "Step"};
    case EditorIcon::Reset:
        return {static_cast<char32_t>(icon), "Reset"};
    case EditorIcon::Close:
        return {static_cast<char32_t>(icon), "Close"};
    case EditorIcon::More:
        return {static_cast<char32_t>(icon), "More"};
    case EditorIcon::Details:
        return {static_cast<char32_t>(icon), "Details"};
    case EditorIcon::NewBelow:
        return {static_cast<char32_t>(icon), "New below"};
    case EditorIcon::FitGraph:
        return {static_cast<char32_t>(icon), "Fit graph"};
    case EditorIcon::FitSelection:
        return {static_cast<char32_t>(icon), "Fit selection"};
    case EditorIcon::Lock:
        return {static_cast<char32_t>(icon), "Freeze"};
    case EditorIcon::Unlock:
        return {static_cast<char32_t>(icon), "Resume"};
    case EditorIcon::Rail:
        return {static_cast<char32_t>(icon), "Follow camera rail"};
    case EditorIcon::Movable:
        return {static_cast<char32_t>(icon), "Movable"};
    case EditorIcon::View:
        return {static_cast<char32_t>(icon), "View"};
    case EditorIcon::Rotate:
        return {static_cast<char32_t>(icon), "Rotate"};
    case EditorIcon::Scale:
        return {static_cast<char32_t>(icon), "Scale"};
    case EditorIcon::Transform:
        return {static_cast<char32_t>(icon), "Transform"};
    case EditorIcon::World:
        return {static_cast<char32_t>(icon), "World"};
    case EditorIcon::Local:
        return {static_cast<char32_t>(icon), "Local"};
    }
    LMX_ASSERT(false, "Invalid editor icon");
    std::unreachable();
}

//======================================================================================================================
std::string encodeUtf8(char32_t codepoint) {
    LMX_ASSERT(codepoint <= 0x10FFFF && (codepoint < 0xD800 || codepoint > 0xDFFF),
               "Expected a Unicode scalar value");
    std::string result;
    if (codepoint <= 0x7F) {
        result += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FF) {
        result += static_cast<char>(0xC0 | (codepoint >> 6));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        result += static_cast<char>(0xE0 | (codepoint >> 12));
        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        result += static_cast<char>(0xF0 | (codepoint >> 18));
        result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    return result;
}

} // namespace lmx::app
