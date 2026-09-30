//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorEnvironment.cpp
/// @brief Draws scene-document exposure, bloom, shadow and read-only environment fields.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorInternal.h"

#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <imgui.h>

#include <format>

namespace lmx::app {
namespace {

constexpr float kMinExposurePercentileGap = 1.0f;

} // namespace

//======================================================================================================================
void drawEnvironmentSection(const InspectorPanelContext& context) {
    auto& session = context.session;
    if (drawInspectorHeader(
            "Environment", "Scene look",
            "Restore Exposure, Bloom and Shadows from the loaded or saved scene "
            "document. Renderer configuration stays unchanged.",
            sceneLookChanged(session), nullptr,
            inspectorProvenance(session, context.selection, sceneLookChanged(session)))) {
        resetSceneLook(session);
        reconcileExposureLook(session.look(), context.exposureContext,
                              context.exposureResetPending);
    }
    auto look = session.look();
    const auto& baseline = session.lookDefault();
    bool edited = false;
    ImGui::SeparatorText("Exposure");
    if (editor_style::beginPropertyGrid("environmentExposure")) {
        markInspectorField(context, look.exposure.ev != baseline.exposure.ev,
                           "Manual exposure (EV)");
        edited |= editor_style::slider("Manual exposure (EV)", "##exposure", &look.exposure.ev,
                                       -6.0f, 6.0f);
        editorTooltip("Each +1 EV doubles manual exposure. With auto exposure enabled, this "
                      "value seeds exposure when it resets; Compensation adjusts metering.");
        markInspectorField(context, look.exposure.autoEnabled != baseline.exposure.autoEnabled,
                           "Auto exposure");
        edited |=
            editor_style::checkbox("Auto exposure", "##autoExposure", &look.exposure.autoEnabled);
        editorTooltip("Meter scene luminance and apply the result on the following frame. "
                      "Enabling starts from Manual exposure, then adapts toward the target.");
        editor_style::endFields();
    }
    ImGui::BeginDisabled(!look.exposure.autoEnabled);
    if (editor_style::beginPropertyGrid("environmentMetering")) {
        markInspectorField(context, look.exposure.lowPercentile != baseline.exposure.lowPercentile,
                           "Low percentile (%)");
        edited |=
            editor_style::slider("Low percentile (%)", "##low", &look.exposure.lowPercentile, 0.0f,
                                 look.exposure.highPercentile - kMinExposurePercentileGap, "%.0f");
        editorTooltip("Exclude the darkest part of the pixel population from metering.");
        markInspectorField(context,
                           look.exposure.highPercentile != baseline.exposure.highPercentile,
                           "High percentile (%)");
        edited |= editor_style::slider(
            "High percentile (%)", "##high", &look.exposure.highPercentile,
            look.exposure.lowPercentile + kMinExposurePercentileGap, 100.0f, "%.0f");
        editorTooltip("The retained range determines the metered log luminance.");
        markInspectorField(context, look.exposure.targetGrey != baseline.exposure.targetGrey,
                           "Target gray");
        edited |= editor_style::slider("Target gray", "##grey", &look.exposure.targetGrey, 0.01f,
                                       1.0f, "%.3f");
        markInspectorField(context, look.exposure.evMin != baseline.exposure.evMin, "Minimum (EV)");
        edited |= editor_style::slider("Minimum (EV)", "##minimum", &look.exposure.evMin, -12.0f,
                                       look.exposure.evMax);
        markInspectorField(context, look.exposure.evMax != baseline.exposure.evMax, "Maximum (EV)");
        edited |= editor_style::slider("Maximum (EV)", "##maximum", &look.exposure.evMax,
                                       look.exposure.evMin, 12.0f);
        markInspectorField(context,
                           look.exposure.compensationEv != baseline.exposure.compensationEv,
                           "Compensation (EV)");
        edited |= editor_style::slider("Compensation (EV)", "##compensation",
                                       &look.exposure.compensationEv, -6.0f, 6.0f);
        markInspectorField(
            context, look.exposure.adaptUpStopsPerSecond != baseline.exposure.adaptUpStopsPerSecond,
            "Adapt up (stops/s)");
        edited |= editor_style::slider("Adapt up (stops/s)", "##adaptUp",
                                       &look.exposure.adaptUpStopsPerSecond, 0.0f, 16.0f);
        markInspectorField(context,
                           look.exposure.adaptDownStopsPerSecond !=
                               baseline.exposure.adaptDownStopsPerSecond,
                           "Adapt down (stops/s)");
        edited |= editor_style::slider("Adapt down (stops/s)", "##adaptDown",
                                       &look.exposure.adaptDownStopsPerSecond, 0.0f, 16.0f);
        editor_style::endFields();
    }
    ImGui::EndDisabled();
    if (!look.exposure.autoEnabled)
        editor_style::message("Enable auto exposure to edit metering and adaptation.");

    ImGui::SeparatorText("Bloom");
    if (editor_style::beginPropertyGrid("environmentBloom")) {
        markInspectorField(context, look.bloom.enabled != baseline.bloom.enabled, "Bloom");
        edited |= editor_style::checkbox("Bloom", "##bloom", &look.bloom.enabled);
        ImGui::BeginDisabled(!look.bloom.enabled);
        markInspectorField(context, look.bloom.threshold != baseline.bloom.threshold,
                           "Threshold (linear)");
        edited |= editor_style::slider("Threshold (linear)", "##threshold", &look.bloom.threshold,
                                       0.0f, 10.0f);
        editorTooltip("Bloom extracts highlights above this pre-exposed linear luminance.");
        markInspectorField(context, look.bloom.intensity != baseline.bloom.intensity, "Intensity");
        edited |=
            editor_style::slider("Intensity", "##intensity", &look.bloom.intensity, 0.0f, 2.0f);
        ImGui::EndDisabled();
        editor_style::endFields();
    }
    ImGui::SeparatorText("Shadows");
    if (editor_style::beginPropertyGrid("environmentShadows")) {
        markInspectorField(context, look.shadowFilter != baseline.shadowFilter, "Shadow filter");
        editor_style::field("Shadow filter");
        int filter = static_cast<int>(look.shadowFilter);
        constexpr const char* kFilterNames[] = {"PCF", "PCSS"};
        if (ImGui::Combo("##shadowFilter", &filter, kFilterNames, 2)) {
            look.shadowFilter = static_cast<asset::ShadowFilter>(filter);
            edited = true;
        }
        editor_style::endFields();
    }
    if (edited) {
        session.editLook(look);
        reconcileExposureLook(session.look(), context.exposureContext,
                              context.exposureResetPending);
    }

    ImGui::SeparatorText("Sky and IBL");
    const auto& environment = session.look().environment;
    if (editor_style::beginPropertyGrid("environmentReadOnly")) {
        if (environment.hdri) {
            const auto& hdri = *environment.hdri;
            valueRow("Sky", "HDRI · " + hdri.uri);
            valueRow("IBL", "From the same HDRI");
            valueRow("Yaw (radians)", std::format("{:.3f}", hdri.yaw));
            valueRow("Scale", std::format("{:.3f}", hdri.scale));
            valueRow("Faces", std::format("{} / {} px", hdri.faceSize, hdri.diffuseFaceSize));
        } else {
            const auto& rgb = environment.skySrgb8;
            valueRow("Sky (sRGB)", std::format("{}, {}, {}", rgb[0], rgb[1], rgb[2]));
            valueRow("IBL", "Neutral sky");
        }
        editor_style::endFields();
    }
    editorTooltip("Sky and IBL are defined by the scene document and are read-only here.");
}

} // namespace lmx::app
