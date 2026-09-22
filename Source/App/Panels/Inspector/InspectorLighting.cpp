//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorLighting.cpp
/// @brief Implements editable local-light subjects and clustered-lighting controls.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorLighting.h"

#include "App/Model/Rendering/Lighting/LightingDiagnostics.h"
#include "App/Model/Rendering/Lighting/LightingHistory.h"
#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"
#include "App/Model/Rendering/Temporal/DiagnosticLegend.h"
#include "App/Panels/Inspector/InspectorPanel.h"
#include "App/Panels/Shared/EditorStyle.h"
#include "Core/Math/Color.h"

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
    if (ImGui::Button("Reset light")) {
        if (const auto result = session.resetLocalLight(id); !result)
            editor_style::message(result.error().message.c_str(), true);
        else
            requestCameraCut(context.temporalState);
    }
    editorTooltip("Restore authored colour, intensity, range and cone. An orbiting light resets "
                  "its position to the track at the current playback time.");
    ImGui::SameLine();
    editor_style::message(session.localLightChanged(id) ? "Changed from scene default"
                                                        : "Scene default");
    auto light = *session.scene().light(id);
    bool edited = false;
    if (editor_style::beginFields("localLightFields")) {
        editor_style::readOnly("Type",
                               light.type == engine::LocalLightType::Point ? "Point" : "Spot");
        edited |=
            editor_style::vector3("Position (world metres)", "position", &light.position.x, 0.05f);
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
    const bool animated =
        std::ranges::any_of(session.scene().animation.lightTracks, [&](const auto& track) {
            return session.scene().animationLightId(track.light) == id;
        });
    if (animated)
        editor_style::message("Orbit playback replaces position on its next sample. Pause to edit "
                              "position; other light edits survive Play and Stop.");
    editor_style::message("Local lights illuminate opaque and masked surfaces without shadows.");
}

//======================================================================================================================
void drawLightingSection(const InspectorPanelContext& context) {
    auto& settings = context.settings;
    auto& session = context.session;
    const auto previousMode = settings.localLightMode;
    bool contentChanged = false;
    ImGui::PushID(&session.scene());
    auto* storage = ImGui::GetStateStorage();
    const auto draftId = ImGui::GetID("pileDraft");
    ImGui::PopID();
    if (ImGui::Button("Reset Lighting")) {
        contentChanged = session.lightLabPileCount() > 0;
        resetRenderingGroup(settings, EditorRenderGroup::Lighting);
        if (session.lightLabPileAvailable()) {
            session.setLightLabPile(0);
            storage->SetInt(draftId, 0);
        }
    }
    editorTooltip("Restore local-light mode and diagnostics, "
                  "and clear the LightLab overflow pile. Other Rendering topics are unchanged.");
    ImGui::SameLine();
    const bool changed = renderingGroupChanged(settings, EditorRenderGroup::Lighting) ||
                         session.lightLabPileCount() > 0;
    editor_style::message(changed ? "Changed from default" : "Default");
    if (editor_style::beginFields("lightingControls")) {
        editor_style::field("Local lights");
        int mode = static_cast<int>(settings.localLightMode);
        if (ImGui::Combo("##localLightMode", &mode, "Off\0Direct\0Clustered\0")) {
            settings.localLightMode = static_cast<engine::LocalLightMode>(mode);
            if (settings.localLightMode != engine::LocalLightMode::Clustered) {
                settings.lightDebugView = engine::LightDebugView::Off;
                settings.lightCheck = false;
            }
        }
        editorTooltip("Direct evaluates every enabled local light. Clustered builds bounded lists "
                      "per froxel. Off keeps directional lights and the environment.");
        editor_style::field("Lighting view");
        int view = static_cast<int>(settings.lightDebugView);
        if (ImGui::Combo("##lightView", &view, "Final\0Count\0Overflow\0Missed\0")) {
            settings.lightDebugView = static_cast<engine::LightDebugView>(view);
            if (settings.lightDebugView != engine::LightDebugView::Off) {
                settings.localLightMode = engine::LocalLightMode::Clustered;
                settings.temporalDebugView = render::TemporalDebugView::Off;
                settings.hzbDebugLevel = -1;
            }
        }
        editor_style::field("CPU list check");
        if (ImGui::Checkbox("##lightCheck", &settings.lightCheck)) {
            if (settings.lightCheck)
                settings.localLightMode = engine::LocalLightMode::Clustered;
        }
        editorTooltip("Compare retired GPU lists and counters with an independent CPU mirror. "
                      "Checking adds CPU work and is excluded from scored measurements.");
        if (session.lightLabPileAvailable()) {
            ImGui::PushID(&session.scene());
            int pile = storage->GetInt(draftId, static_cast<int>(session.lightLabPileCount()));
            editor_style::field("Overflow pile lights");
            if (ImGui::InputInt("##pile", &pile))
                storage->SetInt(
                    draftId, std::clamp(pile, 0, static_cast<int>(session.lightLabPileCapacity())));
            pile = std::clamp(pile, 0, static_cast<int>(session.lightLabPileCapacity()));
            if (ImGui::Button("Apply pile")) {
                const auto result = session.setLightLabPile(static_cast<uint32_t>(pile));
                if (!result)
                    editor_style::message(result.error().message.c_str(), true);
                else
                    contentChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear pile")) {
                session.setLightLabPile(0);
                storage->SetInt(draftId, 0);
                contentChanged = true;
            }
            ImGui::Text("Active %u / available %u", session.lightLabPileCount(),
                        session.lightLabPileCapacity());
            editorTooltip("A fixed colocated light pile deliberately exceeds froxel capacity. "
                          "Apply and Clear preserve the authored grid and its orbit tracks.");
            ImGui::PopID();
        }
        editor_style::endFields();
    }
    if (lightingChangeNeedsHistoryReset(previousMode, settings.localLightMode,
                                        session.scene().enabledLightCount(), contentChanged))
        requestCameraCut(context.temporalState);
    if (settings.lightDebugView != engine::LightDebugView::Off) {
        if (session.scene().enabledLightCount() == 0) {
            editor_style::message(
                "Lighting view unavailable: this scene has no enabled local lights. "
                "Showing Final.");
            editor_style::message("Enable lights using their Hierarchy checkboxes.");
        } else {
            const auto legend = diagnosticLegend(settings.lightDebugView);
            editor_style::message(legend.description.data());
        }
    }
    if (context.lightingDisplay) {
        const auto& latest = context.lightingDisplay->status();
        if (latest.isRetired && latest.counters.truncatedFroxels > 0)
            editor_style::message("LIGHT OVERFLOW: one or more froxel lists were truncated. "
                                  "Some local-light contribution is missing.",
                                  true);
        if (latest.isRetired) {
            const auto failure = lightingFailure(latest);
            if (!failure.empty())
                editor_style::message(failure.c_str(), true);
        }
        if (editor_style::beginFields("lightingReadings")) {
            for (const auto& field : lightingFields(context.lightingDisplay->readingsStatus(),
                                                    context.lightingDisplay->readingsTimings()))
                editor_style::readOnly(field.label.c_str(), field.value.c_str());
            editor_style::endFields();
        }
    }
    editor_style::message("Local point and spot lights are unshadowed. Directional lighting, IBL "
                          "and exposure retain their existing controls.");
}

} // namespace lmx::app
