//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorGroup.cpp
/// @brief Draws document and imported-source group enabled controls.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorInternal.h"

#include "App/Model/Scene/InspectorSubject.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <imgui.h>

namespace lmx::app {

//======================================================================================================================
void drawGroupSection(const InspectorPanelContext& context) {
    const auto state = inspectorEnabledState(context.session, context.selection);
    if (!state) {
        const auto* loaded = context.session.loadedScene();
        const char* name =
            loaded ? loaded->document.name.c_str() : context.session.scene().name.c_str();
        drawInspectorHeader(name, "Scene", "The scene root has no editable enabled flag.", false);
        editor_style::message(
            "Select a document group or source node to inspect its enabled state.");
        if (loaded && editor_style::beginDiagnostics()) {
            if (editor_style::beginPropertyGrid("documentIdentity")) {
                valueRow("Document path", loaded->path.string());
                valueRow("Loaded pair hash", loaded->hash);
                editor_style::endFields();
            }
            editor_style::endDiagnostics();
        }
        return;
    }
    bool enabled = state->own;
    const bool reset = drawInspectorHeader(
        state->label.c_str(), state->kind.c_str(),
        "Restore this node's loaded or saved own enabled state; child choices are retained.",
        state->own != state->baseline, &enabled);
    if (reset) {
        if (const auto result = resetInspectorEnabled(context.session, context.selection); !result)
            editor_style::message(result.error().message.c_str(), true);
        else
            requestCameraCut(context.temporalState);
    } else if (enabled != state->own) {
        if (const auto result = setInspectorEnabled(context.session, context.selection, enabled);
            !result)
            editor_style::message(result.error().message.c_str(), true);
        else
            requestCameraCut(context.temporalState);
    }
    const auto current = inspectorEnabledState(context.session, context.selection);
    if (current && editor_style::beginPropertyGrid("groupFields")) {
        valueRow("Own state", current->own ? "On" : "Off");
        valueRow("Effective state", current->effective ? "On"
                                    : current->own     ? "Off by ancestor"
                                                       : "Off");
        if (current->primitiveCount > 0)
            valueRow("Source scope",
                     std::to_string(current->primitiveCount) + " material primitives");
        editor_style::endFields();
    }
}

} // namespace lmx::app
