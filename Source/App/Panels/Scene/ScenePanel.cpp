//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePanel.cpp
/// @brief Implements the compact subject Hierarchy and File menu scene-loading workflow.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Scene/ScenePanel.h"

#include "App/Model/Scene/SelectionBounds.h"
#include "App/Panels/Shared/EditorStyle.h"

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
    if (row.subject == EditorSubject::LocalLight)
        return selection.subject == row.subject && selection.lightId == row.lightId;
    return row.subject == selection.subject && ((row.subject != EditorSubject::DirectionalLight &&
                                                 row.subject != EditorSubject::Object) ||
                                                row.index == selection.index);
}

//======================================================================================================================
void selectRow(EditorSelection& selection, scenes::SceneId sceneId, const EditorSelectionRow& row) {
    selection = {
        .sceneId = sceneId, .subject = row.subject, .index = row.index, .lightId = row.lightId};
}

//======================================================================================================================
void drawLeaf(const EditorSelectionRow& row, const ScenePanelContext& context,
              std::vector<EditorSelectionRow>& visibleLeaves, bool appendNavigation = true) {
    if (appendNavigation)
        visibleLeaves.push_back(row);
    ImGui::PushID(static_cast<int>(row.subject));
    ImGui::PushID(static_cast<int>(row.index));
    ImGui::PushID(static_cast<int>(row.lightId.generation));
    ImGui::PushID(static_cast<int>(row.lightId.store));
    const ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        (isRowSelected(row, context.selection) ? ImGuiTreeNodeFlags_Selected : 0);
    bool culled = false;
    std::string visibilityTip;
    if (row.subject == EditorSubject::Object && row.index < context.activeScene.objects.size()) {
        const auto* state =
            context.visibilityDisplay.find(context.activeScene.objects[row.index].id,
                                           context.visibilityStatus, context.sceneGeneration);
        culled = state && state->state == render::VisibilityState::Rejected;
        visibilityTip =
            state ? std::format("\n{}: {}", visibilityStateName(state->state),
                                state->state == render::VisibilityState::Rejected &&
                                        state->reason != render::VisibilityReason::Occluded
                                    ? "Outside camera frustum"
                                    : visibilityReasonName(state->reason))
                  : "\nAwaiting this object's rendered frame";
    }
    bool disabledLight = false;
    if (row.subject == EditorSubject::LocalLight) {
        if (const auto* light = context.activeScene.light(row.lightId)) {
            disabledLight = !light->enabled;
        }
    }
    const bool dimmed = (culled || disabledLight) && !isRowSelected(row, context.selection);
    if (dimmed)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TreeNodeEx("subject", flags, "%s", row.displayLabel.c_str());
    if (dimmed)
        ImGui::PopStyleColor();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        selectRow(context.selection, context.activeSceneId, row);
    }
    const std::string& fullName = row.detailLabel.empty() ? row.displayLabel : row.detailLabel;
    const std::string tip = fullName + visibilityTip +
                            (disabledLight ? "\nDisabled light; enable it in the Inspector." : "");
    editorTooltip(tip.c_str());
    if (ImGui::BeginPopupContextItem("SubjectActions")) {
        selectRow(context.selection, context.activeSceneId, row);
        const bool canFrame =
            selectedObjectBounds(context.activeScene, context.selection).has_value();
        if (ImGui::MenuItem("Frame Selected", "F", false, canFrame)) {
            context.frameSelectionRequested = true;
        }
        editorTooltip(canFrame ? "Fit this object's bounds in the viewport."
                               : "Select an object with reliable bounds to frame it.");
        if (ImGui::MenuItem("Copy full name")) {
            ImGui::SetClipboardText(fullName.c_str());
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::PopID();
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
    const size_t lightCount = groupCount(rows, EditorSelectionGroup::DirectionalLights);
    const size_t objectCount = groupCount(rows, EditorSelectionGroup::Objects);
    const size_t localCount = groupCount(rows, EditorSelectionGroup::LocalLights);
    if (lightCount + objectCount + localCount == 0) {
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
        if (localCount > 0 &&
            ImGui::TreeNodeEx("local-lights", kGroupFlags, "Local lights (%zu)", localCount)) {
            std::vector<EditorSelectionRow> localRows;
            for (const auto& row : rows)
                if (row.group == EditorSelectionGroup::LocalLights)
                    localRows.push_back(row);
            visibleLeaves.insert(visibleLeaves.end(), localRows.begin(), localRows.end());
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(localRows.size()));
            for (size_t index = 0; index < localRows.size(); ++index)
                if (isRowSelected(localRows[index], context.selection))
                    clipper.IncludeItemByIndex(static_cast<int>(index));
            while (clipper.Step())
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                    drawLeaf(localRows[static_cast<size_t>(index)], context, visibleLeaves, false);
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
std::optional<scenes::SceneId> drawSceneMenu(const SceneMenuContext& context) {
    std::optional<scenes::SceneId> chosen;
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
        const bool showClear = !context.filter.empty();
        const float width = ImGui::GetContentRegionAvail().x;
        const float clearWidth =
            showClear ? editor_style::iconButtonWidth(EditorIcon::Close) : 0.0f;
        const ImVec2 searchPosition = ImGui::GetCursorScreenPos();
        const ImVec2 searchEnd{searchPosition.x + width,
                               searchPosition.y + ImGui::GetFrameHeight()};
        ImGui::GetWindowDrawList()->AddRectFilled(searchPosition, searchEnd,
                                                  ImGui::GetColorU32(ImGuiCol_FrameBg),
                                                  ImGui::GetStyle().FrameRounding);
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::SetNextItemWidth(std::max(1.0f, width - clearWidth));
        if (ImGui::InputTextWithHint("##scene-filter", "Search subjects...", buffer,
                                     sizeof(buffer))) {
            context.filter.assign(buffer);
        }
        ImGui::PopStyleColor();
        editorTooltip("Filter scene subjects and full source names without changing selection. "
                      "Up/Down traverses visible subjects.");
        if (showClear) {
            ImGui::SameLine();
            if (editor_style::iconButton("ClearSearch", EditorIcon::Close, true,
                                         "Clear search. The selected subject stays selected.")) {
                context.filter.clear();
            }
        }
        ImGui::PopStyleVar();
        ImGui::EndGroup();
        const auto rows = buildSceneSelectionRows(context.activeScene, context.filter);
        const auto count = hierarchyCount(context.activeScene, context.filter);
        ImGui::TextDisabled("%zu / %zu", count.shown, count.total);
        editorTooltip(
            "Matching / total selectable scene subjects, including directional lights, local "
            "lights and objects. Dimmed names are culled objects or disabled lights. "
            "They remain selectable; hover a name for its status.");
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
