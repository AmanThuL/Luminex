//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorPanel.cpp
/// @brief Implements the Inspector panel's shared rows and subject dispatch.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorPanel.h"

#include "App/Panels/Inspector/InspectorInternal.h"
#include "App/Panels/Inspector/InspectorLighting.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <imgui.h>

#include <string>

namespace lmx::app {

using editor_style::field;

//======================================================================================================================
bool drawInspectorHeader(const char* name, const char* kind, const char* resetTooltip, bool changed,
                         bool* enabled) {
    editor_style::beginHeaderRow();
    bool reset = false;
    const auto flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("InspectorHeader", enabled ? 4 : 3, flags)) {
        ImGui::TableSetupColumn("Subject", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize(kind).x);
        if (enabled)
            ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::GetFrameHeight());
        ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed,
                                editor_style::iconButtonWidth(EditorIcon::Reset));
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name);
        editorTooltip(name);
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(editor_style::kMuted, "%s", kind);
        if (enabled) {
            ImGui::TableNextColumn();
            ImGui::Checkbox("##enabled", enabled);
            editorTooltip("Change this subject's own enabled state. A parent can still keep it "
                          "off; its identity and edited fields are retained.");
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
void beginFieldRow(const char* label) {
    field(label);
}

//======================================================================================================================
void valueRow(const char* label, const std::string& value) {
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
