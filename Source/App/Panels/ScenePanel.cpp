//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePanel.cpp
/// @brief Implements the Scene panel's scene-catalog selector, filter, and grouped selection rows.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/ScenePanel.h"

#include <imgui.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lmx::app {

namespace {

// Large enough for any realistic object/light/scene name filter; InputTextWithHint has no
// std::string overload in this project's vendored ImGui, so the box edits a fixed buffer and the
// result is copied back into context.filter only on a change.
constexpr size_t kFilterBufferSize = 256;

//======================================================================================================================
const char* groupLabel(EditorSelectionGroup group) {
    switch (group) {
    case EditorSelectionGroup::Workspace:
        return "Workspace";
    case EditorSelectionGroup::DirectionalLights:
        return "Directional Lights";
    case EditorSelectionGroup::Objects:
        return "Objects";
    }
    return "Workspace";
}

//======================================================================================================================
// Row identity is subject kind plus index (spec section 6): duplicate display names must not
// compare equal, and Camera/Rendering ignore index entirely since it carries no meaning for them.
bool isRowSelected(const EditorSelectionRow& row, const EditorSelection& selection) {
    if (row.subject != selection.subject) {
        return false;
    }
    if (row.subject == EditorSubject::DirectionalLight || row.subject == EditorSubject::Object) {
        return row.index == selection.index;
    }
    return true;
}

//======================================================================================================================
void selectRow(EditorSelection& selection, engine::SceneId activeSceneId,
               const EditorSelectionRow& row) {
    selection =
        EditorSelection{.sceneId = activeSceneId, .subject = row.subject, .index = row.index};
}

//======================================================================================================================
// Draws the scene-catalog combo, unchanged from the pre-selection panel: unavailable entries stay
// disabled with their hint shown inline because a disabled entry cannot be hovered reliably.
std::optional<engine::SceneId> drawSceneSelector(const engine::SceneLibrary& library,
                                                 engine::SceneId activeSceneId) {
    std::optional<engine::SceneId> chosen;
    const std::span<const engine::SceneEntry> entries = library.entries();
    if (ImGui::BeginCombo("Scene", library.entry(activeSceneId).displayName.data())) {
        for (size_t i = 0; i < entries.size(); ++i) {
            const engine::SceneEntry& entry = entries[i];
            ImGui::PushID(static_cast<int>(i));
            if (!entry.available) {
                ImGui::BeginDisabled();
            }
            const std::string label =
                entry.hint.empty() ? std::string(entry.displayName)
                                   : std::string(entry.displayName) + " (" + entry.hint + ")";
            if (ImGui::Selectable(label.c_str(), entry.id == activeSceneId)) {
                chosen = entry.id;
            }
            if (!entry.available) {
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return chosen;
}

//======================================================================================================================
void drawFilter(std::string& filter) {
    char buffer[kFilterBufferSize];
    const size_t copied = filter.copy(buffer, sizeof(buffer) - 1);
    buffer[copied] = '\0';
    if (ImGui::InputTextWithHint("##scene-filter", "Filter by name...", buffer, sizeof(buffer))) {
        filter.assign(buffer);
    }
}

//======================================================================================================================
// Draws every visible row, grouped under a header per group transition; a group with no visible
// rows draws no header at all rather than an empty one. A click applies straight to `selection`.
void drawRows(std::span<const EditorSelectionRow> rows, EditorSelection& selection,
              engine::SceneId activeSceneId) {
    if (rows.empty()) {
        ImGui::TextDisabled("no items match filter");
        return;
    }

    std::optional<EditorSelectionGroup> currentGroup;
    for (const EditorSelectionRow& row : rows) {
        if (!currentGroup || *currentGroup != row.group) {
            currentGroup = row.group;
            ImGui::SeparatorText(groupLabel(row.group));
        }
        // Nested IDs (subject, then index) keep duplicate labels -- e.g. two objects both named
        // "Crate" -- from sharing widget state; row identity is subject+index, not label text.
        ImGui::PushID(static_cast<int>(row.subject));
        ImGui::PushID(static_cast<int>(row.index));
        if (ImGui::Selectable(row.displayLabel.c_str(), isRowSelected(row, selection))) {
            selectRow(selection, activeSceneId, row);
        }
        ImGui::PopID();
        ImGui::PopID();
    }
}

//======================================================================================================================
// Up/Down navigation over the currently visible rows, active only while the Scene window itself is
// focused so it does not fire while some other panel has keyboard focus.
void handleKeyboardNav(std::span<const EditorSelectionRow> rows, EditorSelection& selection,
                       engine::SceneId activeSceneId) {
    if (!ImGui::IsWindowFocused()) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        if (const std::optional<EditorSelectionRow> next = nextVisibleRow(rows, selection)) {
            selectRow(selection, activeSceneId, *next);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        if (const std::optional<EditorSelectionRow> previous =
                previousVisibleRow(rows, selection)) {
            selectRow(selection, activeSceneId, *previous);
        }
    }
}

} // namespace

//======================================================================================================================
std::optional<engine::SceneId> drawScenePanel(bool& open, const ScenePanelContext& context) {
    std::optional<engine::SceneId> chosen;
    if (ImGui::Begin(kScenePanelWindowName, &open)) {
        chosen = drawSceneSelector(context.library, context.activeSceneId);

        ImGui::Separator();
        drawFilter(context.filter);

        const std::vector<EditorSelectionRow> rows =
            buildSceneSelectionRows(context.activeScene, context.filter);
        drawRows(rows, context.selection, context.activeSceneId);
        handleKeyboardNav(rows, context.selection, context.activeSceneId);
    }
    ImGui::End();
    return chosen;
}

} // namespace lmx::app
