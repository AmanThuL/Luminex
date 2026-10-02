//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorLight.cpp
/// @brief Implements the Inspector panel's directional-light controls.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorInternal.h"

#include "App/Model/Rendering/Lighting/DirectionalLightRole.h"
#include "App/Model/Scene/InspectorSubject.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <limits>
#include <string>

namespace lmx::app {

namespace {

constexpr float kMinLightDirectionLength = 1e-5f;

} // namespace

//======================================================================================================================
void drawDirectionalLightSection(const InspectorPanelContext& context, size_t index) {
    auto& session = context.session;
    auto light = session.scene().lights[index];
    const auto enabledState = inspectorEnabledState(session, context.selection);
    if (!enabledState) {
        editor_style::message("This directional light is unavailable in the document.", true);
        return;
    }
    const auto& baseline = session.lightDefault(index);
    const auto headerMark = inspectorProvenance(
        session, context.selection, inspectorSubjectEdited(session, context.selection), {}, true);
    const auto enabledMark = inspectorAppliedMark(
        context, "enabled",
        inspectorProvenance(session, context.selection, enabledState->own != enabledState->baseline,
                            "Enabled", true));
    bool enabled = enabledState->own;
    if (drawInspectorHeader(enabledState->label.c_str(), "Directional",
                            "Restore this directional light's direction and scene-linear "
                            "radiance and its own enabled state from the document.",
                            session.lightChanged(index), &enabled, headerMark, enabledMark)) {
        if (const auto result = session.resetLight(index); !result)
            editor_style::message(result.error().message.c_str(), true);
        else {
            light = session.lightDefault(index);
            enabled = inspectorEnabledState(session, context.selection)->own;
            requestCameraCut(context.temporalState);
        }
    }
    if (enabledState->own && !enabledState->effective)
        editor_style::message("Off in scene because an ancestor is disabled.");
    light.enabled = enabled;
    if (editor_style::beginPropertyGrid("directionalLightFields")) {
        markInspectorField(context, light.direction != baseline.direction, "Direction (world)");
        glm::vec3 direction = light.direction;
        if (editor_style::vector3("Direction (world)", "direction", &direction.x, 0.01f)) {
            if (glm::length(direction) > kMinLightDirectionLength) {
                light.direction = glm::normalize(direction);
            }
        }
        markInspectorField(context, light.strength != baseline.strength,
                           "Radiance (scene-linear RGB)");
        editor_style::vector3("Radiance (scene-linear RGB)", "radiance", &light.strength.x, 0.01f,
                              0.0f, std::numeric_limits<float>::max(), "%.3f",
                              ImGuiSliderFlags_AlwaysClamp, true,
                              "Scene-linear radiance; values above 1 are valid HDR intensities.");
        valueRow("Role", std::string(directionalLightRoleLabel(index)));
        editor_style::endFields();
    }
    if (const auto result = session.editLight(index, light); !result)
        editor_style::message(result.error().message.c_str(), true);
    else if (enabled != enabledState->own)
        requestCameraCut(context.temporalState);
}

} // namespace lmx::app
