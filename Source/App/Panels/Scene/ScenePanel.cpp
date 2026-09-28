//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePanel.cpp
/// @brief Implements the compact subject Hierarchy and File menu scene-loading workflow.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Scene/ScenePanel.h"

#include "App/Model/Scene/SceneSession.h"
#include "App/Model/Scene/SceneTree.h"
#include "App/Model/Scene/SelectionBounds.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
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
        const bool disabled = !context.activeScene.objects[row.index].enabled;
        culled = disabled || (state && state->state == render::VisibilityState::Rejected);
        visibilityTip = disabled ? "\nDisabled"
                        : state
                            ? "\n" + std::string(visibilityStatusLabel(state->state, state->reason))
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
    ImGui::PushID(context.activeSceneId.key.c_str());
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

//======================================================================================================================
void selectTreeRow(const SceneTreeRow& row, const ScenePanelContext& context) {
    context.selection = {.sceneId = context.activeSceneId,
                         .subject = row.subject,
                         .index = row.index,
                         .lightId = row.lightId,
                         .node = row.node,
                         .importedNode = row.importedNode};
}

//======================================================================================================================
void drawTreeRows(std::span<const SceneTreeRow> rows, size_t& cursor,
                  const ScenePanelContext& context, std::set<uint32_t>& collapsed,
                  bool& rootCollapsed, std::vector<SceneTreeRow>& navigation) {
    const SceneTreeRow& row = rows[cursor++];
    const bool root = row.depth == 0;
    if (!root)
        navigation.push_back(row);
    const uint32_t key = row.importedNode == engine::kGeneratedNode
                             ? row.node
                             : sceneTreeImportedKey(row.importedNode);
    ImGui::PushID(static_cast<int>(row.subject));
    ImGui::PushID(static_cast<int>(row.node));
    ImGui::PushID(static_cast<int>(row.importedNode));
    ImGui::PushID(static_cast<int>(row.index));
    ImGui::PushID(static_cast<int>(row.lightId.slot));
    ImGui::PushID(static_cast<int>(row.lightId.generation));
    const bool selected = sceneTreeRowSelected(row, context.selection);
    const auto* object =
        row.subject == EditorSubject::Object && row.index < context.activeScene.objects.size()
            ? &context.activeScene.objects[row.index]
            : nullptr;
    const auto* status = object ? context.visibilityDisplay.find(
                                      object->id, context.visibilityStatus, context.sceneGeneration)
                                : nullptr;
    size_t rejectedPrimitives = 0;
    size_t knownPrimitives = 0;
    size_t primitiveCount = object ? 1 : 0;
    if (row.importedNode != engine::kGeneratedNode && context.loadedScene && object) {
        const auto& source = context.loadedScene->binding.importedNodes[row.importedNode];
        primitiveCount = source.objects.size();
        for (const size_t index : source.objects) {
            const auto* primitiveStatus =
                context.visibilityDisplay.find(context.activeScene.objects[index].id,
                                               context.visibilityStatus, context.sceneGeneration);
            if (!primitiveStatus)
                continue;
            ++knownPrimitives;
            rejectedPrimitives +=
                primitiveStatus->state == render::VisibilityState::Rejected ? 1 : 0;
        }
    } else if (status) {
        knownPrimitives = 1;
        rejectedPrimitives = status->state == render::VisibilityState::Rejected ? 1 : 0;
    }
    const bool culled = row.effective && primitiveCount > 0 && knownPrimitives == primitiveCount &&
                        rejectedPrimitives == primitiveCount;
    const bool dimmed = !row.effective || culled;
    std::string label = row.label;
    if (root && context.dirty)
        label += " *";
    if (!row.enabled)
        label += " [off]";
    else if (!row.effective)
        label += " [off by parent]";
    if (row.generated)
        label += " · not saved";
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               (selected ? ImGuiTreeNodeFlags_Selected : 0);
    if (!row.group)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    else if (root)
        ImGui::SetNextItemOpen(sceneTreeRootOpen(rootCollapsed, context.filter), ImGuiCond_Always);
    else
        ImGui::SetNextItemOpen(!context.filter.empty() || !collapsed.contains(key),
                               ImGuiCond_Always);
    if (dimmed && !selected)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool opened = ImGui::TreeNodeEx("document-row", flags, "%s", label.c_str());
    if (dimmed && !selected)
        ImGui::PopStyleColor();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        selectTreeRow(row, context);
    std::string tip = row.label;
    if (row.generated && context.loadedScene &&
        row.node < context.loadedScene->document.nodes.size())
        tip += "\nGenerated by " + context.loadedScene->document.nodes[row.node].generator->name +
               " · not saved";
    if (!row.enabled)
        tip += "\nDisabled on this node; enable it in Inspector.";
    else if (!row.effective)
        tip += "\nDisabled by an ancestor.";
    else if (primitiveCount > 1 && rejectedPrimitives > 0)
        tip += std::format("\n{} of {} material primitives culled in this view.",
                           rejectedPrimitives, primitiveCount);
    else if (culled && status)
        tip += "\n" + std::string(visibilityStatusLabel(status->state, status->reason));
    if (row.importedNode != engine::kGeneratedNode && context.loadedScene) {
        const auto& source = context.loadedScene->binding.importedNodes[row.importedNode];
        if (source.objects.size() > 1)
            tip += std::format("\nOne source node controls all {} material primitives.",
                               source.objects.size());
    }
    editorTooltip(tip.c_str());
    if (ImGui::BeginPopupContextItem("SubjectActions")) {
        selectTreeRow(row, context);
        const bool canFrame =
            selectedObjectBounds(context.activeScene, context.selection).has_value();
        if (ImGui::MenuItem("Frame Selected", "F", false, canFrame))
            context.frameSelectionRequested = true;
        if (ImGui::MenuItem("Copy full name"))
            ImGui::SetClipboardText(row.label.c_str());
        ImGui::EndPopup();
    }
    if (row.group) {
        if (root) {
            rootCollapsed = sceneTreeRootCollapsedAfterDraw(rootCollapsed, opened, context.filter);
        } else if (context.filter.empty()) {
            if (opened)
                collapsed.erase(key);
            else
                collapsed.insert(key);
        }
        if (opened) {
            while (cursor < rows.size() && rows[cursor].depth > row.depth)
                drawTreeRows(rows, cursor, context, collapsed, rootCollapsed, navigation);
            ImGui::TreePop();
        } else {
            while (cursor < rows.size() && rows[cursor].depth > row.depth)
                ++cursor;
        }
    }
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
}

//======================================================================================================================
void handleTreeKeyboard(std::span<const SceneTreeRow> rows, const ScenePanelContext& context) {
    if (rows.empty() || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
        ImGui::GetIO().WantTextInput)
        return;
    const int step = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1
                     : ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1
                                                             : 0;
    if (step == 0)
        return;
    if (const auto target = sceneTreeKeyboardTarget(rows, context.selection, step > 0))
        selectTreeRow(*target, context);
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
        ImGui::PushID(entry.id.key.c_str());
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
        struct TreeExpansion {
            std::set<uint32_t> collapsed;
            bool rootCollapsed = false;
        };
        static std::unordered_map<std::string, TreeExpansion> expansionByScene;
        const bool documentTree = context.loadedScene && context.session;
        auto& expansion = expansionByScene[context.activeSceneId.key];
        const auto tree =
            documentTree
                ? buildSceneTreeView(*context.loadedScene, context.session->documentState(),
                                     context.filter, expansion.collapsed, context.session)
                : SceneTreeView{};
        const auto flatRows = documentTree
                                  ? std::vector<EditorSelectionRow>{}
                                  : buildSceneSelectionRows(context.activeScene, context.filter);
        ImGui::TextDisabled("%zu / %zu", documentTree ? tree.matchedCount : flatRows.size(),
                            documentTree ? tree.totalCount : hierarchyTotal(context.activeScene));
        editorTooltip("Matching / total scene subjects, including disabled rows. Search keeps "
                      "ancestors; off and culled rows stay selectable.");
        if (documentTree && !context.filter.empty()) {
            const bool selectedShown = std::ranges::any_of(tree.rows, [&](const auto& row) {
                return sceneTreeRowSelected(row, context.selection);
            });
            if (context.selection.subject != EditorSubject::None && !selectedShown)
                editor_style::message("Selection hidden by search; Inspector keeps it selected.",
                                      true);
        } else if (selectionHiddenByFilter(context.activeScene, context.selection,
                                           context.filter)) {
            editor_style::message("Selection hidden by search; Inspector keeps it selected.", true);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, editor_style::scaled(12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ImGui::GetStyle().FramePadding.x, editor_style::scaled(1.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(ImGui::GetStyle().ItemSpacing.x, editor_style::scaled(2.0f)));
        if (ImGui::BeginChild("SubjectList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            if (documentTree) {
                std::vector<SceneTreeRow> visibleRows;
                size_t cursor = 0;
                if (!tree.rows.empty())
                    drawTreeRows(tree.rows, cursor, context, expansion.collapsed,
                                 expansion.rootCollapsed, visibleRows);
                handleTreeKeyboard(visibleRows, context);
            } else {
                std::vector<EditorSelectionRow> visibleLeaves;
                drawHierarchy(flatRows, context, visibleLeaves);
                handleKeyboardNav(visibleLeaves, context);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
    }
    ImGui::End();
}

} // namespace lmx::app
