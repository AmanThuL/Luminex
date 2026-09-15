//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePanel.cpp
/// @brief Implements the compact subject Hierarchy and File menu scene-loading workflow.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/ScenePanel.h"

#include "App/Panels/EditorStyle.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace lmx::app {
namespace {

constexpr size_t kFilterBufferSize = 256;
constexpr ImGuiTreeNodeFlags kGroupFlags =
    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

//======================================================================================================================
bool isRowSelected(const EditorSelectionRow& row, const EditorSelection& selection) {
    return row.subject == selection.subject && ((row.subject != EditorSubject::DirectionalLight &&
                                                 row.subject != EditorSubject::Object) ||
                                                row.index == selection.index);
}

//======================================================================================================================
void selectRow(EditorSelection& selection, scene::SceneId sceneId, const EditorSelectionRow& row) {
    selection = {.sceneId = sceneId, .subject = row.subject, .index = row.index};
}

//======================================================================================================================
void drawLeaf(const EditorSelectionRow& row, const ScenePanelContext& context,
              std::vector<EditorSelectionRow>& visibleLeaves) {
    visibleLeaves.push_back(row);
    ImGui::PushID(static_cast<int>(row.subject));
    ImGui::PushID(static_cast<int>(row.index));
    const ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        (isRowSelected(row, context.selection) ? ImGuiTreeNodeFlags_Selected : 0);
    std::string label = row.displayLabel;
    std::string visibilityTip;
    if (row.subject == EditorSubject::Object && row.index < context.activeScene.objects.size()) {
        const auto* state =
            context.visibilityDisplay.find(context.activeScene.objects[row.index].id,
                                           context.visibilityStatus, context.sceneGeneration);
        label = std::format("{} {}", state ? visibilityBadge(state->state) : "[?]", label);
        visibilityTip = state ? std::format("\n{}: {}", visibilityStateName(state->state),
                                            state->state == render::VisibilityState::Rejected
                                                ? "Outside camera frustum"
                                                : visibilityReasonName(state->reason))
                              : "\nAwaiting this object's rendered frame";
    }
    ImGui::TreeNodeEx("subject", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        selectRow(context.selection, context.activeSceneId, row);
    }
    const std::string& fullName = row.detailLabel.empty() ? row.displayLabel : row.detailLabel;
    const std::string tip = fullName + visibilityTip;
    editorTooltip(tip.c_str());
    if (ImGui::BeginPopupContextItem("SubjectActions")) {
        if (ImGui::MenuItem("Copy full name")) {
            ImGui::SetClipboardText(fullName.c_str());
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::PopID();
}

//======================================================================================================================
size_t groupCount(std::span<const EditorSelectionRow> rows, EditorSelectionGroup group) {
    return static_cast<size_t>(std::count_if(
        rows.begin(), rows.end(), [group](const auto& row) { return row.group == group; }));
}

//======================================================================================================================
void drawLeaves(std::span<const EditorSelectionRow> rows, EditorSelectionGroup group,
                const ScenePanelContext& context, std::vector<EditorSelectionRow>& visibleLeaves) {
    for (const auto& row : rows) {
        if (row.group == group) {
            drawLeaf(row, context, visibleLeaves);
        }
    }
}

//======================================================================================================================
void drawHierarchy(std::span<const EditorSelectionRow> rows, const ScenePanelContext& context,
                   std::vector<EditorSelectionRow>& visibleLeaves) {
    if (rows.empty()) {
        editor_style::message("No subjects match. Clear search to restore the tree.");
        return;
    }
    const size_t workspaceCount = groupCount(rows, EditorSelectionGroup::Workspace);
    if (workspaceCount > 0 &&
        ImGui::TreeNodeEx("workspace", kGroupFlags, "Workspace (%zu)", workspaceCount)) {
        drawLeaves(rows, EditorSelectionGroup::Workspace, context, visibleLeaves);
        ImGui::TreePop();
    }
    const size_t lightCount = groupCount(rows, EditorSelectionGroup::DirectionalLights);
    const size_t objectCount = groupCount(rows, EditorSelectionGroup::Objects);
    if (lightCount + objectCount == 0) {
        return;
    }
    ImGui::PushID(static_cast<int>(context.activeSceneId.catalogIndex));
    const bool sceneOpen =
        ImGui::TreeNodeEx("active-scene", kGroupFlags, "%s", context.activeScene.name.c_str());
    editorTooltip(
        "Navigation groups for the active scene. Grouping does not add parent transforms.");
    if (sceneOpen) {
        if (lightCount > 0 &&
            ImGui::TreeNodeEx("lights", kGroupFlags, "Lights (%zu)", lightCount)) {
            drawLeaves(rows, EditorSelectionGroup::DirectionalLights, context, visibleLeaves);
            ImGui::TreePop();
        }
        if (objectCount > 0 &&
            ImGui::TreeNodeEx("objects", kGroupFlags, "Objects (%zu)", objectCount)) {
            const auto groups = groupSceneObjectRows(rows);
            for (const auto& group : groups) {
                if (group.sourceName.empty()) {
                    for (const auto& row : group.rows) {
                        drawLeaf(row, context, visibleLeaves);
                    }
                    continue;
                }
                ImGui::PushID(group.sourceName.c_str());
                const bool sourceOpen =
                    ImGui::TreeNodeEx("source", kGroupFlags, "%s", group.sourceName.c_str());
                const std::string help = std::format(
                    "{}: {} matching primitive subjects. Source-name navigation group only.",
                    group.sourceName, group.rows.size());
                editorTooltip(help.c_str());
                if (sourceOpen) {
                    for (const auto& row : group.rows) {
                        drawLeaf(row, context, visibleLeaves);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

//======================================================================================================================
void handleKeyboardNav(std::span<const EditorSelectionRow> rows, const ScenePanelContext& context) {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
        ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        if (const auto next = nextVisibleRow(rows, context.selection)) {
            selectRow(context.selection, context.activeSceneId, *next);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        if (const auto previous = previousVisibleRow(rows, context.selection)) {
            selectRow(context.selection, context.activeSceneId, *previous);
        }
    }
}

} // namespace

//======================================================================================================================
std::optional<scene::SceneId> drawSceneMenu(const SceneMenuContext& context) {
    std::optional<scene::SceneId> chosen;
    if (!ImGui::BeginMenu("Open Scene")) {
        return chosen;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + editor_style::scaled(360.0f));
    for (const auto& entry : context.library.entries()) {
        ImGui::PushID(static_cast<int>(entry.id.catalogIndex));
        ImGui::BeginDisabled(!entry.available);
        if (ImGui::Selectable(entry.displayName.data(), entry.id == context.activeSceneId,
                              ImGuiSelectableFlags_NoAutoClosePopups) &&
            entry.id != context.activeSceneId) {
            chosen = entry.id;
        }
        const std::string help =
            entry.available
                ? std::format("Open {}. The current scene stays active until loading succeeds.",
                              entry.displayName)
                : entry.hint;
        editorTooltip(help.c_str());
        ImGui::EndDisabled();
        if (!entry.available) {
            editor_style::message(entry.hint.empty() ? "Required scene assets are unavailable."
                                                     : entry.hint.c_str(),
                                  true);
        }
        ImGui::PopID();
    }
    if (!chosen && context.loading.failedScene()) {
        const auto failed = *context.loading.failedScene();
        ImGui::Separator();
        const std::string failure = std::format("{} could not load: {}\nCurrent scene kept. Fix "
                                                "the cause and retry, or choose another scene.",
                                                context.library.entry(failed).displayName,
                                                context.loading.failureMessage());
        editor_style::message(failure.c_str(), true);
        if (ImGui::Button("Retry scene load")) {
            chosen = failed;
        }
        editorTooltip("Retry the failed catalog entry. The current scene and selection are kept if "
                      "it fails again.");
    }
    if (chosen) {
        ImGui::Separator();
        const std::string loading =
            std::format("Loading {}...", context.library.entry(*chosen).displayName);
        editor_style::message(loading.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndMenu();
    return chosen;
}

//======================================================================================================================
void drawScenePanel(bool& open, const ScenePanelContext& context) {
    if (ImGui::Begin(kScenePanelWindowName, &open)) {
        char buffer[kFilterBufferSize];
        const size_t copied = context.filter.copy(buffer, sizeof(buffer) - 1);
        buffer[copied] = '\0';
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##scene-filter", "Search subjects...", buffer,
                                     sizeof(buffer))) {
            context.filter.assign(buffer);
        }
        editorTooltip("Filter subject and full source names without changing selection. Up/Down "
                      "traverses visible leaves.");
        if (ImGui::SmallButton("Clear search")) {
            context.filter.clear();
        }
        editorTooltip("Show all subjects. The selected subject stays selected.");
        const auto rows = buildSceneSelectionRows(context.activeScene, context.filter);
        const std::string count = std::format("{} / {}", rows.size(),
                                              size_t{2} + std::size(context.activeScene.lights) +
                                                  context.activeScene.objects.size());
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        if (right - ImGui::GetItemRectMax().x >=
            ImGui::CalcTextSize(count.c_str()).x + ImGui::GetStyle().ItemSpacing.x) {
            ImGui::SameLine();
        }
        ImGui::TextDisabled("%s", count.c_str());
        editorTooltip("Matching / total selectable subjects, including camera, rendering, lights "
                      "and objects. Badges: V visible, R rejected, B bypassed, ? pending.");
        if (selectionHiddenByFilter(context.activeScene, context.selection, context.filter)) {
            editor_style::message("Selection hidden by search; Inspector keeps it selected.", true);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, editor_style::scaled(12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ImGui::GetStyle().FramePadding.x, editor_style::scaled(1.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(ImGui::GetStyle().ItemSpacing.x, editor_style::scaled(2.0f)));
        if (ImGui::BeginChild("SubjectList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            std::vector<EditorSelectionRow> visibleLeaves;
            drawHierarchy(rows, context, visibleLeaves);
            handleKeyboardNav(visibleLeaves, context);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
    }
    ImGui::End();
}

} // namespace lmx::app
