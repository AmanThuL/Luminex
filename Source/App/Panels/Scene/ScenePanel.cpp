//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePanel.cpp
/// @brief Implements the compact subject Hierarchy and selection navigation.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Scene/ScenePanel.h"
#include "App/Model/Session/SessionProposal.h"

#include "App/Model/Scene/SceneSession.h"
#include "App/Model/Scene/SceneTree.h"
#include "App/Model/Scene/SelectionBounds.h"
#include "App/Panels/Inspector/InspectorInternal.h"
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
        ImGui::PushStyleColor(ImGuiCol_Text, editor_style::color(ThemeRole::TextDisabled));
    ImGui::TreeNodeEx("subject", flags, "%s", row.displayLabel.c_str());
    if (dimmed)
        ImGui::PopStyleColor();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        selectRow(context.selection, context.activeSceneId, row);
    }
    const std::string& fullName = row.detailLabel.empty() ? row.displayLabel : row.detailLabel;
    const EditorSelection subject{.sceneId = context.activeSceneId,
                                  .subject = row.subject,
                                  .index = row.index,
                                  .lightId = row.lightId};
    const auto mark =
        context.session
            ? inspectorProvenance(*context.session, subject,
                                  inspectorSubjectEdited(*context.session, subject), {}, true)
            : std::nullopt;
    if (mark)
        editor_style::provenanceMark(*mark, true);
    const std::string tip = fullName + visibilityTip +
                            (disabledLight ? "\nDisabled light; enable it in the Inspector." : "") +
                            (mark ? "\n" + mark->source : "");
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
std::string treeRowTooltip(const SceneTreeRow& row, const ScenePanelContext& context,
                           size_t primitiveCount, size_t rejectedPrimitives, bool culled,
                           const render::InstanceVisibility* status) {
    std::string tip = row.label;
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
    return tip;
}

//======================================================================================================================
void drawTreeRow(const SceneTreeRow& row, const ScenePanelContext& context) {
    const bool root = row.depth == 0;
    const std::string& sceneKey = context.activeSceneId.key;
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
    const EditorSelection subject{.sceneId = context.activeSceneId,
                                  .subject = row.subject,
                                  .index = row.index,
                                  .lightId = row.lightId,
                                  .node = row.node,
                                  .importedNode = row.importedNode};
    const bool preview =
        row.subject == EditorSubject::Object && context.session &&
        context.session->objectChanged(row.index) &&
        std::ranges::any_of(context.activeScene.animation.tracks,
                            [&](const auto& track) { return track.objectIndex == row.index; });
    const auto mark =
        root && context.proposal ? std::optional{proposedProvenance(context.proposal->client)}
        : root && context.loadedScene
            ? documentProvenance(context.dirty, context.loadedScene->path.string())
        : context.session
            ? inspectorProvenance(*context.session, subject,
                                  row.generated ? inspectorSubjectEdited(*context.session, subject)
                                                : row.edited,
                                  {}, true, preview)
            : std::nullopt;
    std::string label = row.label;
    if (row.movable) {
        const auto icon = editorIconInfo(EditorIcon::Movable);
        const bool hasGlyph =
            ImGui::GetFontBaked()->FindGlyphNoFallback(static_cast<ImWchar>(icon.codepoint));
        label = (hasGlyph ? encodeUtf8(icon.codepoint) : std::string(icon.label)) + " " + label;
    }
    if (!row.enabled)
        label += " [off]";
    else if (!row.effective)
        label += " [off by parent]";
    if (row.generated && context.loadedScene &&
        row.node < context.loadedScene->document.nodes.size())
        label += " · " + generatedProvenanceSource(
                             context.loadedScene->document.nodes[row.node].generator->name);
    else if (mark && mark->kind == Provenance::SessionOnly)
        label += " · not saved";
    // Rows are drawn flat with an explicit indent so a clipper can skip whole ranges; open state
    // comes from the model and is written back only when the user toggles it.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen |
                               (selected ? ImGuiTreeNodeFlags_Selected : 0);
    if (!row.group)
        flags |= ImGuiTreeNodeFlags_Leaf;
    else if (root)
        ImGui::SetNextItemOpen(
            sceneTreeRootOpen(context.treeState.rootCollapsed(sceneKey), context.filter),
            ImGuiCond_Always);
    else
        ImGui::SetNextItemOpen(!context.filter.empty() ||
                                   !context.treeState.collapsed(sceneKey, key),
                               ImGuiCond_Always);
    const float indent = ImGui::GetStyle().IndentSpacing * static_cast<float>(row.depth);
    if (indent > 0.0f)
        ImGui::Indent(indent);
    if (dimmed && !selected)
        ImGui::PushStyleColor(ImGuiCol_Text, editor_style::color(ThemeRole::TextDisabled));
    const bool opened = ImGui::TreeNodeEx("document-row", flags, "%s", label.c_str());
    if (context.proposal && context.loadedScene && row.node != engine::kGeneratedNode &&
        proposalAffectsNode(*context.proposal, context.loadedScene->document, row.node))
        editor_style::attentionRing(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (mark)
        editor_style::provenanceMark(*mark, true);
    if (dimmed && !selected)
        ImGui::PopStyleColor();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        selectTreeRow(row, context);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
        const bool generated = context.loadedScene &&
                               row.node < context.loadedScene->document.nodes.size() &&
                               context.loadedScene->document.nodes[row.node].generator.has_value();
        const std::string provenance = rowProvenanceTooltip(
            generated ? std::optional<std::string_view>(
                            context.loadedScene->document.nodes[row.node].generator->name)
                      : std::nullopt,
            mark);
        const std::string tip =
            treeRowTooltip(row, context, primitiveCount, rejectedPrimitives, culled, status) +
            (provenance.empty() ? "" : "\n" + provenance);
        editorTooltip(tip.c_str());
    }
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
        if (root)
            context.treeState.setRootCollapsed(
                sceneKey, sceneTreeRootCollapsedAfterDraw(context.treeState.rootCollapsed(sceneKey),
                                                          opened, context.filter));
        else if (context.filter.empty())
            context.treeState.setCollapsed(sceneKey, key, !opened);
    }
    if (indent > 0.0f)
        ImGui::Unindent(indent);
    for (int pops = 0; pops < 6; ++pops)
        ImGui::PopID();
}

//======================================================================================================================
void drawTreeRows(std::span<const SceneTreeRow> rows, const ScenePanelContext& context) {
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step())
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            drawTreeRow(rows[static_cast<size_t>(index)], context);
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
std::optional<bool> drawScenePanel(bool& open, const ScenePanelContext& context) {
    std::optional<bool> selectionHidden;
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
                                                  editor_style::colorU32(ThemeRole::SurfaceSunken),
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
        const bool documentTree = context.loadedScene && context.session;
        const std::string& sceneKey = context.activeSceneId.key;
        static const SceneTreeView kEmptyTree;
        const SceneTreeView& tree =
            documentTree ? context.treeState.view(
                               sceneKey, {.loaded = *context.loadedScene,
                                          .state = context.session->documentState(),
                                          .session = context.session,
                                          .filter = context.filter,
                                          .sceneGeneration = context.sceneGeneration,
                                          .editGeneration = context.session->editGeneration()})
                         : kEmptyTree;
        const auto flatRows = documentTree
                                  ? std::vector<EditorSelectionRow>{}
                                  : buildSceneSelectionRows(context.activeScene, context.filter);
        ImGui::TextDisabled("%zu / %zu", documentTree ? tree.matchedCount : flatRows.size(),
                            documentTree ? tree.totalCount : hierarchyTotal(context.activeScene));
        editorTooltip("Matching / total scene subjects, including disabled rows. Search keeps "
                      "ancestors; off and culled rows stay selectable.");
        if (documentTree) {
            if (sceneTreeSelectionHidden(tree.rows, context.selection, context.filter))
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
                const auto visible = sceneTreeVisibleRows(
                    tree.rows, context.treeState.rootCollapsed(sceneKey), context.filter);
                drawTreeRows(visible, context);
                handleTreeKeyboard(visible.empty() ? visible : visible.subspan(1), context);
            } else {
                std::vector<EditorSelectionRow> visibleLeaves;
                drawHierarchy(flatRows, context, visibleLeaves);
                handleKeyboardNav(visibleLeaves, context);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        if (documentTree)
            selectionHidden =
                sceneTreeSelectionHidden(tree.rows, context.selection, context.filter);
    }
    ImGui::End();
    return selectionHidden;
}

} // namespace lmx::app
