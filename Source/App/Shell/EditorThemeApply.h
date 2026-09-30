//----------------------------------------------------------------------------------------------------------------------
/// @file EditorThemeApply.h
/// @brief Declares the encoded theme adapters for ImGui and the graph canvas.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "App/Model/Workspace/EditorThemeTokens.h"

struct ImGuiStyle;
namespace ax::NodeEditor {
struct Style;
}

namespace lmx::app {
/// Copies every mapped encoded-sRGB color into style without changing metrics or fonts.
void applyImGuiColors(ImGuiStyle& style, const ThemePalette& palette);
/// Copies every mapped encoded-sRGB graph color without changing canvas geometry.
void applyNodeEditorColors(ax::NodeEditor::Style& style, const ThemePalette& palette);
/// Asserts the pinned library color names match the generated ordered tables.
/// Requires current ImGui and node-editor contexts on the UI thread.
void verifyThemeSlotNames();
} // namespace lmx::app
