//----------------------------------------------------------------------------------------------------------------------
/// @file EditorThemeApply.cpp
/// @brief Applies all generated color slots and checks their upstream identities.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Shell/EditorThemeApply.h"

#include "Core/Diagnostics/Assert.h"

#include <imgui.h>
#include <imgui_node_editor.h>

namespace lmx::app {
static_assert(ImGuiCol_COUNT == 63);
static_assert(ax::NodeEditor::StyleColor_Count == 19);

//======================================================================================================================
void applyImGuiColors(ImGuiStyle& style, const ThemePalette& palette) {
    for (std::size_t i = 0; i < kImGuiSlots.size(); ++i) {
        const auto& slot = kImGuiSlots[i];
        const auto c = palette[static_cast<std::size_t>(slot.role)];
        style.Colors[i] = {c.r, c.g, c.b, c.a * slot.alphaScale};
    }
}

//======================================================================================================================
void applyNodeEditorColors(ax::NodeEditor::Style& style, const ThemePalette& palette) {
    for (std::size_t i = 0; i < kNodeEditorSlots.size(); ++i) {
        const auto& slot = kNodeEditorSlots[i];
        const auto c = palette[static_cast<std::size_t>(slot.role)];
        style.Colors[i] = {c.r, c.g, c.b, c.a * slot.alphaScale};
    }
}

//======================================================================================================================
void verifyImGuiSlotNames() {
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        LMX_ASSERT(kImGuiSlots[i].name == ImGui::GetStyleColorName(i), "ImGui theme slot mismatch");
}

//======================================================================================================================
void verifyNodeEditorSlotNames() {
    for (int i = 0; i < ax::NodeEditor::StyleColor_Count; ++i)
        LMX_ASSERT(kNodeEditorSlots[i].name == ax::NodeEditor::GetStyleColorName(
                                                   static_cast<ax::NodeEditor::StyleColor>(i)),
                   "node-editor theme slot mismatch");
}

} // namespace lmx::app
