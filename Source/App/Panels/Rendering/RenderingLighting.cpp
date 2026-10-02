//----------------------------------------------------------------------------------------------------------------------
/// @file RenderingLighting.cpp
/// @brief Implements local-light rendering controls and coherent readings.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Rendering/Lighting/LightingDiagnostics.h"
#include "App/Model/Rendering/Lighting/LightingHistory.h"
#include "App/Model/Rendering/Settings/RenderSettingCommands.h"
#include "App/Model/Rendering/Temporal/DiagnosticLegend.h"
#include "App/Panels/Rendering/RenderingInternal.h"
#include "App/Panels/Shared/EditorStyle.h"
#include <algorithm>
namespace lmx::app {
//======================================================================================================================
void drawLightingTopic(const InspectorPanelContext& context) {
    auto& settings = context.settings;
    auto& session = context.session;
    const auto previousMode = settings.localLightMode;
    bool contentChanged = false;
    ImGui::PushID(&session.scene());
    auto* storage = ImGui::GetStateStorage();
    const auto draftId = ImGui::GetID("pileDraft");
    ImGui::PopID();
    if (editor_style::beginPropertyGrid("lightingControls")) {
        if (context.settingAttribution && context.settingAttribution->has("setting/local-lights"))
            editor_style::setNextFieldProvenance(sessionAppliedProvenance(
                context.settingAttribution->client("setting/local-lights")));
        editor_style::field("Local lights");
        int mode = static_cast<int>(settings.localLightMode);
        if (ImGui::Combo("##localLightMode", &mode, "Off\0Direct\0Clustered\0")) {
            const auto value = mode == static_cast<int>(engine::LocalLightMode::Off) ? "off"
                               : mode == static_cast<int>(engine::LocalLightMode::Direct)
                                   ? "direct"
                                   : "clustered";
            applyRenderSetting(settings, RenderSettingKey::LocalLights, value);
        }
        editorTooltip("Direct evaluates every enabled local light. Clustered builds bounded lists "
                      "per froxel. Off keeps directional lights and the environment. Views: View > "
                      "Debug View.");
        if (context.settingAttribution && context.settingAttribution->has("setting/light-check"))
            editor_style::setNextFieldProvenance(sessionAppliedProvenance(
                context.settingAttribution->client("setting/light-check")));
        editor_style::field("CPU list check");
        bool lightCheck = settings.lightCheck;
        if (ImGui::Checkbox("##lightCheck", &lightCheck))
            applyRenderSetting(settings, RenderSettingKey::LightCheck, lightCheck ? "on" : "off");
        editorTooltip("Compare retired GPU lists and counters with an independent CPU mirror. "
                      "Checking adds CPU work and is excluded from scored measurements.");
        if (session.localLightRigAvailable()) {
            if (context.settingAttribution &&
                context.settingAttribution->has("setting/local-light-rig"))
                editor_style::setNextFieldProvenance(sessionAppliedProvenance(
                    context.settingAttribution->client("setting/local-light-rig")));
            editor_style::field("Local light rig");
            ImGui::TextUnformatted(session.localLightRigEnabled() ? "On" : "Off");
        }
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
            editor_style::message("Enable lights using the Inspector header checkbox.");
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
        const auto fields = lightingFields(context.lightingDisplay->readingsStatus(), {});
        const auto diagnostic = [](const LightingField& row) {
            return row.label == "Frame" || row.label == "Grid / slices" ||
                   row.label == "Capacities" || row.label == "List bytes (used / allocated)";
        };
        const auto drawRows = [&](bool details) {
            if (!editor_style::beginPropertyGrid(details ? "lightingDiagnostics"
                                                         : "lightingReadings"))
                return;
            for (const auto& row : fields) {
                if (row.label == "Lighting GPU time" || row.label.starts_with("lmx.pass."))
                    continue;
                if (diagnostic(row) == details)
                    editor_style::readOnly(row.label.c_str(), row.value.c_str());
            }
            editor_style::endFields();
        };
        drawRows(false);
        drawPerformanceDetails(context);
        if (editor_style::beginDiagnostics()) {
            drawRows(true);
            editor_style::endDiagnostics();
        }
    }
}

} // namespace lmx::app
