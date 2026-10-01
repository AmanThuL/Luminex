//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorPanel.cpp
/// @brief Implements the Inspector panel's shared rows and subject dispatch.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorPanel.h"

#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"
#include "App/Model/Scene/InspectorSubject.h"
#include "App/Panels/Inspector/InspectorInternal.h"
#include "App/Panels/Inspector/InspectorLighting.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace lmx::app {

using editor_style::field;

//======================================================================================================================
bool drawInspectorHeader(const char* name, const char* kind, const char* resetTooltip, bool changed,
                         bool* enabled, const std::optional<ProvenanceMark>& mark,
                         const std::optional<ProvenanceMark>& enabledMark) {
    editor_style::beginHeaderRow();
    bool reset = false;
    const auto flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("InspectorHeader", enabled ? 4 : 3, flags)) {
        ImGui::TableSetupColumn("Subject", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize(kind).x);
        if (enabled)
            ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight() +
                                        (enabledMark && enabledMark->kind != Provenance::SessionOnly
                                             ? editor_style::scaled(editor_style::kActorMarkSize) +
                                                   ImGui::GetStyle().ItemInnerSpacing.x
                                             : 0));
        ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed,
                                editor_style::iconButtonWidth(EditorIcon::Reset));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        {
            const editor_style::ScopedType type(TypeRole::BodyStrong);
            const float markWidth =
                mark && mark->kind != Provenance::Authored && mark->kind != Provenance::SessionOnly
                    ? editor_style::scaled(editor_style::kActorMarkSize) +
                          ImGui::GetStyle().ItemInnerSpacing.x
                    : 0.0f;
            const float nameWidth = ImGui::GetContentRegionAvail().x - markWidth;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (nameWidth > 0 ? nameWidth : 1.0f));
            ImGui::TextUnformatted(name);
            ImGui::PopTextWrapPos();
        }
        editorTooltip(name);
        if (mark)
            editor_style::provenanceMark(*mark);
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(editor_style::color(ThemeRole::TextSecondary), "%s", kind);
        if (enabled) {
            ImGui::TableNextColumn();
            ImGui::Checkbox("##enabled", enabled);
            editorTooltip("Change this subject's own enabled state. A parent can still keep it "
                          "off; its identity and edited fields are retained.");
            if (enabledMark)
                editor_style::provenanceMark(*enabledMark);
        }
        ImGui::TableNextColumn();
        reset = editor_style::iconButton("resetSubject", EditorIcon::Reset, changed, resetTooltip);
        ImGui::EndTable();
    }
    editor_style::endHeaderRow();
    ImGui::Separator();
    return reset;
}

//======================================================================================================================
bool inspectorSubjectEdited(const SceneSession& session, const EditorSelection& selection) {
    const auto enabled = inspectorEnabledState(session, selection);
    const bool flagChanged = enabled && enabled->own != enabled->baseline;
    switch (selection.subject) {
    case EditorSubject::Object:
        return flagChanged || session.objectChanged(selection.index);
    case EditorSubject::DirectionalLight:
        return flagChanged || session.lightChanged(selection.index);
    case EditorSubject::LocalLight:
        return flagChanged || session.localLightChanged(selection.lightId);
    case EditorSubject::Environment:
        return sceneLookChanged(session);
    case EditorSubject::Group:
        return flagChanged;
    case EditorSubject::None:
    case EditorSubject::Camera:
        return false;
    }
    return false;
}

//======================================================================================================================
std::optional<ProvenanceMark> inspectorProvenance(const SceneSession& session,
                                                  const EditorSelection& selection, bool edited,
                                                  std::string_view field, bool enabled,
                                                  bool preview) {
    const auto state = inspectorEnabledState(session, selection);
    const auto* loaded = session.loadedScene();
    const std::optional<std::string_view> generated =
        state && !state->generatedBy.empty() ? std::optional{std::string_view(state->generatedBy)}
                                             : std::nullopt;
    std::optional<std::string_view> cli;
    if (enabled && loaded && loaded->binding.localLightGroup && session.localLightRigOverride()) {
        const auto group = *loaded->binding.localLightGroup;
        uint32_t node = selection.node;
        if (selection.subject == EditorSubject::LocalLight) {
            const auto found =
                loaded->binding.lightNode.find(engine::sceneLightKey(selection.lightId));
            if (found != loaded->binding.lightNode.end())
                node = found->second;
        }
        std::vector<uint32_t> descendants{group};
        for (size_t i = 0; i < descendants.size(); ++i)
            for (const auto child : loaded->document.nodes[descendants[i]].children)
                descendants.push_back(child);
        if (std::ranges::find(descendants, node) != descendants.end())
            cli =
                *session.localLightRigOverride() ? "--local-light-rig on" : "--local-light-rig off";
    }
    auto mark = subjectProvenance(generated, cli, edited);
    if (preview && edited && !generated && !cli)
        mark = ProvenanceMark{Provenance::SessionOnly, Actor::Operator,
                              "Animation-owned preview · not saved"};
    if (mark) {
        if (preview && edited && generated)
            mark->source += " · animation-owned preview";
        if (loaded)
            mark->source += " · " + loaded->path.string();
        if (!field.empty())
            mark->source += " · " + std::string(field);
    }
    return mark;
}

//======================================================================================================================
void markInspectorField(const InspectorPanelContext& context, bool edited, std::string_view field,
                        bool preview) {
    editor_style::setNextFieldProvenance(
        inspectorProvenance(context.session, context.selection, edited, field, false, preview));
}

//======================================================================================================================
void beginFieldRow(const char* label) {
    field(label);
}

//======================================================================================================================
void valueRow(const char* label, const std::string& value,
              const std::optional<ProvenanceMark>& mark) {
    editor_style::setNextFieldProvenance(mark);
    editor_style::readOnly(label, value.c_str());
}

//======================================================================================================================
void drawInspectorPanel(bool& open, const InspectorPanelContext& context) {
    if (ImGui::Begin(kInspectorPanelWindowName, &open)) {
        const auto subject = context.selection.subject;
        if (context.selectionHiddenByFilter) {
            editor_style::message("Selection is hidden by the Scene search filter. Clear the "
                                  "filter to find it in the list.",
                                  true);
            if (context.sceneFilter && ImGui::Button("Clear filter")) {
                context.sceneFilter->clear();
            }
        }
        const ImGuiID previousKey = ImGui::GetID("PreviousInspectorSubject");
        ImGuiStorage* storage = ImGui::GetStateStorage();
        ImGui::PushID(static_cast<int>(subject));
        ImGui::PushID(static_cast<int>(subject == EditorSubject::LocalLight
                                           ? context.selection.lightId.slot
                                           : context.selection.index));
        ImGui::PushID(static_cast<int>(context.selection.lightId.generation));
        ImGui::PushID(static_cast<int>(context.selection.lightId.store));
        ImGui::PushID(static_cast<int>(context.selection.node));
        ImGui::PushID(static_cast<int>(context.selection.importedNode));
        const ImGuiID page = ImGui::GetID("InspectorFields");
        const bool changed = storage->GetInt(previousKey, -1) != static_cast<int>(page);
        storage->SetInt(previousKey, static_cast<int>(page));
        if (ImGui::BeginChild("InspectorFields", ImVec2(0, 0))) {
            if (changed)
                ImGui::SetScrollY(0.0f);
            switch (subject) {
            case EditorSubject::None:
                editor_style::message(
                    "Select a scene node in Hierarchy, or choose View > Editor Camera.");
                break;
            case EditorSubject::Camera:
                drawCameraSection(context);
                break;
            case EditorSubject::DirectionalLight:
                drawDirectionalLightSection(context, context.selection.index);
                break;
            case EditorSubject::LocalLight:
                drawLocalLightSection(context, context.selection.lightId);
                break;
            case EditorSubject::Object:
                drawObjectSection(context, context.selection.index);
                break;
            case EditorSubject::Group:
                drawGroupSection(context);
                break;
            case EditorSubject::Environment:
                drawEnvironmentSection(context);
                break;
            }
        }
        ImGui::EndChild();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
    }
    ImGui::End();
}

} // namespace lmx::app
