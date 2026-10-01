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
/// Asserts the pinned ImGui color names match the generated ordered table. Needs no context.
void verifyImGuiSlotNames();
/// Asserts the pinned node-editor color names match the generated ordered table.
/// Requires a current node-editor context on the UI thread.
void verifyNodeEditorSlotNames();
} // namespace lmx::app
