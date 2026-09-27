//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorObject.cpp
/// @brief Implements the Inspector panel's object controls and visibility readings.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorInternal.h"

#include "App/Panels/Shared/EditorStyle.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace lmx::app {

//======================================================================================================================
void drawObjectSection(const InspectorPanelContext& context, size_t index) {
    auto& session = context.session;
    auto& object = session.scene().objects[index];
    const bool animated =
        std::ranges::any_of(session.scene().animation.tracks,
                            [index](const auto& track) { return track.objectIndex == index; });
    if (drawInspectorHeader(sceneObjectLabel(session.scene(), index).c_str(), "Object",
                            "Restore this object's authored transform and reset temporal "
                            "history. Animated objects use their authored track at the current "
                            "playback time.")) {
        session.resetObject(index);
        requestCameraCut(context.temporalState);
    }
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
        bool edited =
            editor_style::vector3("Position (world)", "position", &transform.position.x, 0.05f);
        if (animated)
            editorTooltip("Pause playback to edit. Reset samples the authored track at the current "
                          "time; playback replaces transform edits on its next sample.");
        edited |= editor_style::vector3("Rotation (XYZ degrees)", "rotation",
                                        &transform.eulerDegrees.x, 1.0f);
        edited |= editor_style::vector3("Scale", "scale", &transform.scale.x, 0.01f, 0.01f, 100.0f,
                                        "%.3f", ImGuiSliderFlags_AlwaysClamp);
        if (edited) {
            session.editObject(index, transform);
            requestCameraCut(context.temporalState);
        }
        for (const auto& row : visibilityFields) {
            if (!diagnostic(row))
                valueRow(row.label.c_str(), row.value);
        }
        editor_style::endFields();
    }
    if (editor_style::beginDiagnostics() && editor_style::beginPropertyGrid("objectDiagnostics")) {
        valueRow("Mesh row", std::to_string(object.mesh.slot));
        valueRow("Material row", std::to_string(object.material.slot));
        for (const auto& row : visibilityFields) {
            if (diagnostic(row))
                valueRow(row.label.c_str(), row.value);
        }
        editor_style::endFields();
    }
}

} // namespace lmx::app
