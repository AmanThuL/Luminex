//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorLight.cpp
/// @brief Implements the Inspector panel's directional-light controls.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorInternal.h"

#include "App/Model/Rendering/Lighting/DirectionalLightRole.h"
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
    auto& light = session.scene().lights[index];
    const std::string name = "Light " + std::to_string(index);
    if (drawInspectorHeader(name.c_str(), "Directional",
                            "Restore this directional light's direction and scene-linear "
                            "radiance from the current scene defaults.")) {
        session.resetLight(index);
    }
    if (editor_style::beginPropertyGrid("directionalLightFields")) {
        glm::vec3 direction = light.direction;
        if (editor_style::vector3("Direction (world)", "direction", &direction.x, 0.01f)) {
            if (glm::length(direction) > kMinLightDirectionLength) {
                light.direction = glm::normalize(direction);
            }
        }
        editor_style::vector3("Radiance (scene-linear RGB)", "radiance", &light.strength.x, 0.01f,
                              0.0f, std::numeric_limits<float>::max(), "%.3f",
                              ImGuiSliderFlags_AlwaysClamp, true);
        editorTooltip("Scene-linear radiance; values above 1 are valid HDR intensities.");
        valueRow("Role", std::string(directionalLightRoleLabel(index)));
        editor_style::endFields();
    }
}

} // namespace lmx::app
