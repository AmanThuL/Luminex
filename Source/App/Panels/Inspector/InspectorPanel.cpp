//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorPanel.cpp
/// @brief Implements the Inspector panel's shared rows and subject dispatch.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorPanel.h"

#include "App/Panels/Inspector/InspectorInternal.h"
#include "App/Panels/Inspector/InspectorLighting.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <string>

namespace lmx::app {

using editor_style::field;

//======================================================================================================================
void beginFieldRow(const char* label) {
    field(label);
}

//======================================================================================================================
void valueRow(const char* label, const std::string& value) {
    editor_style::readOnly(label, value.c_str());
}

//======================================================================================================================
bool beginReadings(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
        return false;
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.48f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.52f);
    return true;
}

//======================================================================================================================
void drawInspectorPanel(bool& open, const InspectorPanelContext& context) {
    if (ImGui::Begin(kInspectorPanelWindowName, &open)) {
        const auto subject = context.selection.subject;
        if (subject == EditorSubject::Object) {
            ImGui::TextWrapped(
                "%s", sceneObjectLabel(context.session.scene(), context.selection.index).c_str());
            editor_style::message("Object transform");
        } else if (subject == EditorSubject::DirectionalLight) {
            ImGui::TextWrapped("Light %zu", context.selection.index);
            editor_style::message("Directional light");
        } else if (subject == EditorSubject::LocalLight) {
            ImGui::TextWrapped(
                "%s",
                sceneLocalLightLabel(context.session.scene(), context.selection.lightId).c_str());
            editor_style::message("Local light");
        } else if (subject == EditorSubject::Camera) {
            ImGui::TextUnformatted("Editor Camera");
        }
        if (context.selectionHiddenByFilter) {
            editor_style::message("Selection is hidden by the Scene search filter. Clear the "
                                  "filter to find it in the list.",
                                  true);
            if (context.sceneFilter && ImGui::Button("Clear filter")) {
                context.sceneFilter->clear();
            }
        }
        ImGui::Separator();
        const ImGuiID previousKey = ImGui::GetID("PreviousInspectorSubject");
        ImGuiStorage* storage = ImGui::GetStateStorage();
        ImGui::PushID(static_cast<int>(subject));
        ImGui::PushID(static_cast<int>(subject == EditorSubject::LocalLight
                                           ? context.selection.lightId.slot
                                           : context.selection.index));
        ImGui::PushID(static_cast<int>(context.selection.lightId.generation));
        ImGui::PushID(static_cast<int>(context.selection.lightId.store));
        const ImGuiID page = ImGui::GetID("InspectorFields");
        const bool changed = storage->GetInt(previousKey, -1) != static_cast<int>(page);
        storage->SetInt(previousKey, static_cast<int>(page));
        if (ImGui::BeginChild("InspectorFields", ImVec2(0, 0))) {
            if (changed)
                ImGui::SetScrollY(0.0f);
            switch (subject) {
            case EditorSubject::None:
                editor_style::message(
                    "Select a light or object in Hierarchy, or Editor Camera in View.");
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
            }
        }
        ImGui::EndChild();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
    }
    ImGui::End();
}

} // namespace lmx::app
