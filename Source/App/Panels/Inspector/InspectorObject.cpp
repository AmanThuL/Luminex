//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorObject.cpp
/// @brief Implements the Inspector panel's object controls and visibility readings.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorInternal.h"

#include "App/Model/Scene/InspectorSubject.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace lmx::app {

//======================================================================================================================
void drawObjectSection(const InspectorPanelContext& context, size_t index) {
    auto& session = context.session;
    auto& object = session.scene().objects[index];
    const auto enabledState = inspectorEnabledState(session, context.selection);
    if (!enabledState) {
        editor_style::message("This object no longer exists in the active document.", true);
        return;
    }
    const bool animated =
        std::ranges::any_of(session.scene().animation.tracks,
                            [index](const auto& track) { return track.objectIndex == index; });
    const auto baseline = session.objectDefault(index);
    const auto headerMark = inspectorProvenance(session, context.selection,
                                                inspectorSubjectEdited(session, context.selection),
                                                {}, true, animated && session.objectChanged(index));
    const auto enabledMark = inspectorAppliedMark(
        context, "enabled",
        inspectorProvenance(session, context.selection, enabledState->own != enabledState->baseline,
                            "Enabled", true));
    bool enabled = enabledState->own;
    if (drawInspectorHeader(enabledState->label.c_str(), enabledState->kind.c_str(),
                            "Restore this object's authored transform and own enabled state. "
                            "Animated objects use their track at the current time.",
                            session.objectChanged(index) ||
                                enabledState->own != enabledState->baseline,
                            &enabled, headerMark, enabledMark)) {
        if (const auto result = session.resetObject(index); !result)
            editor_style::message(result.error().message.c_str(), true);
        else
            requestCameraCut(context.temporalState);
        if (const auto result = resetInspectorEnabled(session, context.selection); !result)
            editor_style::message(result.error().message.c_str(), true);
        else
            requestCameraCut(context.temporalState);
    } else if (enabled != enabledState->own) {
        if (const auto result = setInspectorEnabled(session, context.selection, enabled); !result)
            editor_style::message(result.error().message.c_str(), true);
        else
            requestCameraCut(context.temporalState);
    }
    if (!enabledState->generatedBy.empty()) {
        const std::string marker = generatedProvenanceSource(enabledState->generatedBy);
        editor_style::message(marker.c_str());
        editorTooltip(kGeneratedPopulationTooltip.data());
    }
    if (enabledState->own && !enabledState->effective)
        editor_style::message("Off in scene because an ancestor is disabled.");
    if (enabledState->primitiveCount > 1)
        ImGui::TextDisabled("One source node controls all %zu material primitives.",
                            enabledState->primitiveCount);
    const auto visibilityFields = context.visibilityDisplay == nullptr
                                      ? objectVisibilityFields(nullptr)
                                      : context.visibilityDisplay->objectFields(
                                            object.id, context.temporalState.sceneGeneration);
    const auto diagnostic = [](const VisibilityField& row) {
        return row.label != "Visibility" && row.label != "Reason" &&
               row.label != "Occlusion outcome";
    };
    if (editor_style::beginPropertyGrid("objectFields")) {
        DecomposedTransform transform{object.position, object.eulerDegrees, object.scale};
        markInspectorField(context, object.position != baseline.position, "Position", animated);
        bool edited = editor_style::vector3(
            "Position", "position", &transform.position.x, 0.05f, 0.0f, 0.0f, "%.3f", 0, false,
            animated ? "World-space position. Pause playback to edit. Reset samples the authored "
                       "track at the current time; playback replaces transform edits on its next "
                       "sample."
                     : "World-space position.");
        markInspectorField(context, object.eulerDegrees != baseline.eulerDegrees, "Rotation",
                           animated);
        edited |= editor_style::vector3("Rotation", "rotation", &transform.eulerDegrees.x, 1.0f,
                                        0.0f, 0.0f, "%.3f", 0, false,
                                        "Euler rotation about X, Y and Z, in degrees.");
        markInspectorField(context, object.scale != baseline.scale, "Scale", animated);
        edited |= editor_style::vector3("Scale", "scale", &transform.scale.x, 0.01f, 0.01f, 100.0f,
                                        "%.3f", ImGuiSliderFlags_AlwaysClamp, false,
                                        "Scale factor per axis, from 0.01 to 100.");
        if (edited) {
            if (const auto result = session.editObject(index, transform); !result)
                editor_style::message(result.error().message.c_str(), true);
            else
                requestCameraCut(context.temporalState);
        }
        for (const auto& row : visibilityFields) {
            if (!diagnostic(row))
                valueRow(row.label.c_str(), row.value);
        }
        editor_style::endFields();
    }
    if (editor_style::beginDiagnostics()) {
        if (editor_style::beginPropertyGrid("objectDiagnostics")) {
            valueRow("Mesh row", std::to_string(object.mesh.slot));
            valueRow("Material row", std::to_string(object.material.slot));
            for (const auto& row : visibilityFields) {
                if (diagnostic(row))
                    valueRow(row.label.c_str(), row.value);
            }
            editor_style::endFields();
        }
        editor_style::endDiagnostics();
    }
}

} // namespace lmx::app
