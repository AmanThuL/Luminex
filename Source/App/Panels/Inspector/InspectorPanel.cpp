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
void drawRenderingReset(const InspectorPanelContext& context, EditorRenderGroup group) {
    ImGui::PushID(static_cast<int>(group));
    const bool changed =
        renderingGroupChanged(context.settings, group) ||
        (group == EditorRenderGroup::Display &&
         (context.renderer.clearColor[0] != 0.05f || context.renderer.clearColor[1] != 0.07f ||
          context.renderer.clearColor[2] != 0.10f || context.renderer.clearColor[3] != 1.0f));
    if (ImGui::SmallButton("Reset group")) {
        resetRenderingGroup(context.settings, group);
        if (group == EditorRenderGroup::Exposure) {
            setAutoExposureEnabled(context.settings, context.exposureContext,
                                   context.exposureResetPending,
                                   context.settings.autoExposureEnabled);
        }
        if (group == EditorRenderGroup::Display) {
            constexpr std::array kClear{0.05f, 0.07f, 0.10f, 1.0f};
            std::copy(kClear.begin(), kClear.end(), context.renderer.clearColor);
        }
    }
    editorTooltip("Restore the editor defaults for this rendering group. Other groups, the camera "
                  "and scene playback keep their current settings.");
    if (changed) {
        ImGui::SameLine();
        editor_style::message("Modified");
    }
    ImGui::PopID();
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
        } else if (subject == EditorSubject::Rendering) {
            ImGui::TextUnformatted(
                renderingCategoryLabel(static_cast<RenderingCategory>(context.selection.index))
                    .data());
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
                    "Select a camera, rendering settings, light or object in Scene.");
                break;
            case EditorSubject::Camera:
                drawCameraSection(context);
                break;
            case EditorSubject::Rendering:
                drawRenderingSection(context);
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
