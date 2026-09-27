//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorLighting.cpp
/// @brief Implements editable local-light subjects and clustered-lighting controls.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorLighting.h"

#include "App/Panels/Inspector/InspectorInternal.h"
#include "App/Panels/Shared/EditorStyle.h"
#include "Core/Math/Color.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <algorithm>

namespace lmx::app {

//======================================================================================================================
void drawLocalLightSection(const InspectorPanelContext& context, engine::LightId id) {
    auto& session = context.session;
    const auto* current = session.scene().light(id);
    if (!current) {
        editor_style::message("This light no longer exists. Select a live light in Hierarchy.",
                              true);
        return;
    }
    auto light = *current;
    const bool animated =
        std::ranges::any_of(session.scene().animation.lightTracks, [&](const auto& track) {
            return session.scene().animationLightId(track.light) == id;
        });
    if (drawInspectorHeader(sceneLocalLightLabel(session.scene(), id).c_str(),
                            light.type == engine::LocalLightType::Point ? "Point" : "Spot",
                            "Restore the authored enable state, colour, intensity, range, "
                            "direction and cones. An orbiting light resets its position to the "
                            "track at the current playback time.",
                            session.localLightChanged(id), &light.enabled)) {
        if (const auto result = session.resetLocalLight(id); !result)
            editor_style::message(result.error().message.c_str(), true);
        else
            requestCameraCut(context.temporalState);
        light = *session.scene().light(id);
    }
    bool edited = light.enabled != session.scene().light(id)->enabled;
    if (editor_style::beginPropertyGrid("localLightFields")) {
        edited |=
            editor_style::vector3("Position (world metres)", "position", &light.position.x, 0.05f);
        if (animated)
            editorTooltip("Orbit playback replaces position on its next sample. Pause to edit "
                          "position; other light edits survive Play and Stop.");
        glm::vec3 colour(linearToSrgb(light.colour.r), linearToSrgb(light.colour.g),
                         linearToSrgb(light.colour.b));
        editor_style::field("Colour (sRGB)");
        if (ImGui::ColorEdit3("##colour", &colour.x, ImGuiColorEditFlags_Float)) {
            light.colour = srgbToLinear(colour);
            edited = true;
        }
        editor_style::field("Intensity (relative)");
        edited |= ImGui::DragFloat("##intensity", &light.intensity, 0.1f, 0.0f, 100000.0f, "%.3f",
                                   ImGuiSliderFlags_AlwaysClamp);
        editorTooltip("Relative intensity on opaque and masked surfaces; local lights do not "
                      "cast shadows.");
        editor_style::field("Range (metres)");
        edited |= ImGui::DragFloat("##range", &light.range, 0.05f, 0.01f, 1000.0f, "%.3f",
                                   ImGuiSliderFlags_AlwaysClamp);
        if (light.type == engine::LocalLightType::Spot) {
            auto direction = light.direction;
            if (editor_style::vector3("Direction (world)", "direction", &direction.x, 0.01f)) {
                if (glm::length(direction) > 1e-5f) {
                    light.direction = glm::normalize(direction);
                    edited = true;
                }
            }
            float inner = glm::degrees(light.innerCone);
            float outer = glm::degrees(light.outerCone);
            bool coneEdited = editor_style::slider("Inner cone (degrees)", "##innerCone", &inner,
                                                   0.0f, outer - 0.1f);
            coneEdited |= editor_style::slider("Outer cone (degrees)", "##outerCone", &outer,
                                               inner + 0.1f, 89.0f);
            if (coneEdited) {
                light.innerCone = glm::radians(inner);
                light.outerCone = glm::radians(outer);
                edited = true;
            }
        }
        editor_style::endFields();
    }
    if (edited) {
        if (const auto result = session.editLocalLight(id, light); !result)
            editor_style::message(result.error().message.c_str(), true);
        else
            requestCameraCut(context.temporalState);
    }
}

} // namespace lmx::app
