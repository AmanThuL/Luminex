//----------------------------------------------------------------------------------------------------------------------
/// @file RenderingTopics.cpp
/// @brief Implements Rendering topic controls, readings and diagnostics.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Rendering/RenderingInternal.h"

#include "App/Model/Rendering/Settings/RenderSettingCommands.h"
#include "App/Model/Rendering/Visibility/VisibilityDiagnostics.h"
#include "App/Model/Scene/SceneTableDisplay.h"

#include "App/Panels/Inspector/InspectorInternal.h"
#include "App/Panels/Shared/EditorStyle.h"
#include "Render/Passes/Temporal/Temporal.h"
#include "Render/Passes/Temporal/TemporalHistory.h"

#include <imgui.h>

#include <cstddef>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

namespace lmx::app {

using editor_style::checkbox;
using editor_style::field;
using editor_style::slider;

//======================================================================================================================
static void drawTemporalReadings(const InspectorPanelContext& context) {
    const auto& renderer = context.renderer;
    const auto status = renderer.temporalStatus();
    const auto presentation =
        temporalPresentation(context.temporalState, context.settings, status,
                             context.temporalSupport, renderer.width(), renderer.height());
    {
        ImGui::TextColored(editor_style::color(ThemeRole::AccentOperatorText), "%s · %.0f%%",
                           std::string(presentation.effectiveName).c_str(),
                           presentation.effectiveScale * 100.0f);
        ImGui::TextWrapped("Render %u × %u px  /  Output %u × %u px",
                           presentation.extents.renderWidth, presentation.extents.renderHeight,
                           presentation.extents.outputWidth, presentation.extents.outputHeight);
    }
    if (presentation.requestedName != presentation.effectiveName)
        ImGui::TextWrapped("Requested: %s", std::string(presentation.requestedName).c_str());
}

//======================================================================================================================
static void drawTemporalSection(const InspectorPanelContext& context, RenderingCategory category) {
    auto& settings = context.settings;
    const auto status = context.renderer.temporalStatus();
    const auto presentation =
        temporalPresentation(context.temporalState, settings, status, context.temporalSupport,
                             context.renderer.width(), context.renderer.height());
    if (category == RenderingCategory::Reconstruction) {
        if (editor_style::beginPropertyGrid("reconstructionFields")) {
            bool temporalEnabled = settings.temporalEnabled;
            if (checkbox("Temporal inputs", "##temporal", &temporalEnabled)) {
                auto retained = settings;
                retained.temporalEnabled = true;
                applyRenderSetting(settings, RenderSettingKey::Temporal,
                                   temporalEnabled
                                       ? renderSettingValue(retained, RenderSettingKey::Temporal)
                                       : "off");
            }
            editorTooltip("Enable motion and history inputs for reconstruction and diagnostics. "
                          "Turning this off renders at full resolution; algorithm and scale "
                          "requests are retained. Debug views are in View > Debug View.");
            ImGui::BeginDisabled(!settings.temporalEnabled);
            field("Algorithm");
            if (ImGui::BeginCombo("##reconstruction",
                                  std::string(presentation.requestedName).c_str())) {
                for (int index = 0; index < 3; ++index) {
                    const auto mode = static_cast<render::ReconstructionMode>(index);
                    const auto name = reconstructionName(mode, context.temporalSupport);
                    if (ImGui::Selectable(std::string(name).c_str(),
                                          settings.reconstruction == mode)) {
                        const auto value = mode == render::ReconstructionMode::Raw ? "raw"
                                           : mode == render::ReconstructionMode::NativeTaa
                                               ? "taa"
                                               : "metalfx";
                        applyRenderSetting(settings, RenderSettingKey::Temporal, value);
                    }
                }
                ImGui::EndCombo();
            }
            editorTooltip("Raw keeps the current frame without temporal accumulation. Native TAA "
                          "accumulates engine history. The device algorithm may fall back to "
                          "Native TAA; the effective mode and reason appear above.");
            ImGui::EndDisabled();
            editor_style::endFields();
        }
        if (!settings.temporalEnabled)
            editor_style::message("Off: full resolution; temporal settings are retained.");
        if (settings.temporalEnabled &&
            settings.temporalDebugView != render::TemporalDebugView::Off)
            editor_style::message("Diagnostic image active. Choose Final in View > Debug View.");
        {
            ImGui::BeginDisabled(!settings.temporalEnabled);
            if (editor_style::beginPropertyGrid("temporalDiagnostics")) {
                checkbox("Jitter", "##jitter", &settings.jitterEnabled);
                editorTooltip(
                    "Offset raster samples each frame for temporal reconstruction. Motion "
                    "vectors stay unjittered; turning jitter off does not stop the sequence.");
                editor_style::endFields();
            }
            ImGui::EndDisabled();
            if (ImGui::Button("Reset history")) {
                requestCameraCut(context.temporalState);
            }
            editorTooltip("Discard temporal history on the next frame without moving the camera. "
                          "Use after a camera teleport or to inspect history warmup.");
        }
        drawTemporalReadings(context);
        if (editor_style::beginDiagnostics()) {
            if (editor_style::beginPropertyGrid("historyFields")) {
                valueRow("Declared device frame",
                         std::to_string(context.temporalState.declaredFrameId));
                valueRow("History", !presentation.temporalActive ? "N/A"
                                    : status.historyValid        ? "Valid"
                                                                 : "Reset / warming up");
                valueRow("Warmup", !presentation.temporalActive ? "N/A"
                                   : status.warmupComplete      ? "Complete"
                                                                : "In progress");
                valueRow("History age", presentation.temporalActive
                                            ? std::format("{} frames", status.historyAge)
                                            : "N/A");
                valueRow("Jitter index",
                         presentation.temporalActive ? std::to_string(status.jitterIndex) : "N/A");
                valueRow("Last reset event",
                         context.temporalState.lastResetFrame == 0
                             ? "N/A"
                             : std::format("{} · declared frame {}",
                                           render::historyResetReasonName(
                                               context.temporalState.lastResetReason),
                                           context.temporalState.lastResetFrame));
                valueRow("History memory",
                         std::format("{} color + {} depth bytes", status.historyBytes,
                                     status.depthHistoryBytes));
                valueRow("Vendor capability", context.temporalSupport.available
                                                  ? std::string(context.temporalSupport.name)
                                                  : "Unavailable");
                valueRow("Vendor scale range",
                         context.temporalSupport.available
                             ? std::format("{:.2f}–{:.2f}", context.temporalSupport.minInputScale,
                                           context.temporalSupport.maxInputScale)
                             : "N/A");
                valueRow("Vendor generation",
                         presentation.temporalActive &&
                                 status.reconstruction == render::ReconstructionMode::VendorTemporal
                             ? std::to_string(status.vendorScalerGeneration)
                             : "N/A");
                editor_style::endFields();
            }
            editorTooltip("Motion = current UV - previous UV, unjittered render-extent UV, +Y "
                          "down; infinity marks invalid motion.");
            editor_style::endDiagnostics();
        }
    }
    if (category == RenderingCategory::Resolution) {
        if (editor_style::beginPropertyGrid("resolutionFields")) {
            ImGui::BeginDisabled(!settings.temporalEnabled || settings.dynamicResolutionEnabled);
            float scale = settings.renderScale;
            if (slider("Render scale", "##scale", &scale, render::kMinRenderScale, 1.0f))
                applyRenderSetting(settings, RenderSettingKey::RenderScale,
                                   renderScaleCommandValue(scale));
            std::string scaleTooltip =
                "Scale the render width and height relative to output; 0.50 uses one quarter as "
                "many pixels. Reconstruction returns the image to output size. Device limits may "
                "clamp the effective scale.";
            if (!settings.temporalEnabled || settings.dynamicResolutionEnabled) {
                auto draft = settings;
                const auto available =
                    applyRenderSetting(draft, RenderSettingKey::RenderScale, "1");
                if (!available)
                    scaleTooltip = available.error() + " " + scaleTooltip;
            }
            editorTooltip(scaleTooltip.c_str());
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!settings.temporalEnabled);
            checkbox("Dynamic resolution", "##dynamic", &settings.dynamicResolutionEnabled);
            editorTooltip("Adjust render scale from retired timed-pass measurements, starting at "
                          "the current manual scale. Disabling keeps the last requested scale "
                          "for manual editing.");
            ImGui::EndDisabled();
            if (settings.dynamicResolutionEnabled) {
                ImGui::BeginDisabled(!dynamicResolutionActive(settings));
                slider("GPU budget (ms)", "##budget", &settings.gpuBudgetMilliseconds, 2.0f, 33.0f);
                editorTooltip("Budget for the sum of measured GPU passes. The controller keeps "
                              "headroom and waits for repeated samples before changing scale. "
                              "This is not a total frame-time or FPS limit.");
                ImGui::EndDisabled();
            }
            editor_style::endFields();
        }
        if (settings.temporalEnabled && settings.dynamicResolutionEnabled)
            editor_style::message("Scale adjusts automatically to the GPU budget.");
        drawTemporalReadings(context);
        if (editor_style::beginPropertyGrid("resolutionReadings")) {
            auto mark =
                resolutionProvenance(dynamicResolutionActive(settings), presentation.effectiveScale,
                                     settings.gpuBudgetMilliseconds);
            if (mark) {
                mark->source += " · latest declared effective scale";
                if (presentation.waitingForDeclaration)
                    mark->source += " · awaiting current declaration";
            }
            valueRow("Effective scale", std::format("{:.2f}", presentation.effectiveScale), mark);
            valueRow("Controller", dynamicResolutionActive(settings) ? "Active" : "Inactive");
            editor_style::endFields();
        }
        drawPerformanceDetails(context);
        if (editor_style::beginDiagnostics()) {
            if (editor_style::beginPropertyGrid("resolutionDiagnostics")) {
                const uint64_t sampleFrame = context.dynamicResolutionState.lastMeasurementFrame;
                valueRow("Controller sample frame",
                         sampleFrame == 0 ? "N/A" : std::to_string(sampleFrame));
                editor_style::endFields();
            }
            editor_style::endDiagnostics();
        }
    }
}

//======================================================================================================================
static void drawDisplaySection(const InspectorPanelContext& context) {
    ImGui::SeparatorText("Display");
    ImGui::TextWrapped("%s", render::describe(context.renderer.displayDomain()).c_str());
    ImGui::TextWrapped("UI: encoded sRGB, straight alpha, SDR white");
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    ImGui::Text("Framebuffer scale: %.2f x %.2f", scale.x, scale.y);
    ImGui::Text("Display target: %u x %u px", context.renderer.width(), context.renderer.height());
    const bool identity = context.viewportWidth == context.renderer.width() &&
                          context.viewportHeight == context.renderer.height();
    ImGui::Text("Viewport image: %s", !context.viewportVisible ? "not visible"
                                      : identity               ? "1:1 backing pixels"
                                                               : "resizing (stretched)");
}

//======================================================================================================================
void drawRenderingTopic(const InspectorPanelContext& context, RenderingCategory category) {
    auto& settings = context.settings;
    auto& renderer = context.renderer;
    const auto& visibility = context.visibilityDisplay ? context.visibilityDisplay->readingsStatus()
                                                       : renderer.visibilityStatus();
    const bool currentVisibility =
        visibility.frameNumber != 0 &&
        visibility.sceneGeneration == context.temporalState.sceneGeneration;
    const bool visibilityReady =
        currentVisibility &&
        (visibility.classifyMode == render::ClassifyMode::Cpu || visibility.isRetired);
    drawTemporalSection(context, category);
    if (category == RenderingCategory::Visibility) {
        if (editor_style::beginPropertyGrid("visibilityControls")) {
            bool visibilityEnabled = settings.visibilityEnabled;
            if (checkbox("Frustum culling", "##frustum", &visibilityEnabled))
                applyRenderSetting(settings, RenderSettingKey::Visibility,
                                   visibilityEnabled ? "cull" : "off");
            editorTooltip("Conservative camera-frustum test. Shadow candidates stay unculled.");
            editor_style::field("Classifier");
            int classifier = static_cast<int>(settings.classifyMode);
            if (ImGui::Combo("##classifier", &classifier, "CPU\0GPU\0")) {
                applyRenderSetting(
                    settings, RenderSettingKey::Classify,
                    classifier == static_cast<int>(render::ClassifyMode::Gpu) ? "gpu" : "cpu");
            }
            editorTooltip(
                "GPU results arrive after retirement. CPU remains the default reference.");
            if (settings.classifyMode == render::ClassifyMode::Gpu) {
                bool classifyCheck = settings.classifyCheck;
                if (checkbox("Verify against CPU", "##classifyCheck", &classifyCheck))
                    applyRenderSetting(settings, RenderSettingKey::ClassifyCheck,
                                       classifyCheck ? "on" : "off");
                editorTooltip(
                    "CPU oracle check: compare GPU states, ordered rows, counts and arguments "
                    "with the CPU reference. Diagnostic runs are unscored.");
            }
            editor_style::endFields();
        }
    }
    if (category == RenderingCategory::Occlusion) {
        if (editor_style::beginPropertyGrid("occlusionControls")) {
            const bool occlusionAvailable =
                settings.classifyMode == render::ClassifyMode::Gpu && settings.visibilityEnabled;
            ImGui::BeginDisabled(!occlusionAvailable);
            bool occlusionEnabled = settings.occlusionEnabled;
            if (checkbox("Occlusion", "##occlusion", &occlusionEnabled))
                applyRenderSetting(settings, RenderSettingKey::Occlusion,
                                   occlusionEnabled ? "on" : "off");
            ImGui::EndDisabled();
            editorTooltip(occlusionAvailable ? "Previous-frame depth evidence; camera motion can "
                                               "delay newly visible geometry by one frame. "
                                               "Any coverage change retains all candidates. "
                                               "HZB views are in View > Debug View."
                                             : "Requires GPU classification and frustum culling. "
                                               "HZB views are in View > Debug View.");
            if (!settings.occlusionEnabled && settings.occlusionCheck)
                applyRenderSetting(settings, RenderSettingKey::OcclusionCheck, "off");
            if (settings.occlusionEnabled) {
                bool occlusionCheck = settings.occlusionCheck;
                if (checkbox("Independent ID check", "##occlusionCheck", &occlusionCheck))
                    applyRenderSetting(settings, RenderSettingKey::OcclusionCheck,
                                       occlusionCheck ? "on" : "off");
                editorTooltip("Draw all candidates independently and check missing visible "
                              "instances. Unscored.");
                checkbox("Rejected bounds (max 128)", "##occlusionBounds",
                         &settings.showOcclusionBounds);
            }
            editor_style::endFields();
        }
        if (settings.classifyMode != render::ClassifyMode::Gpu || !settings.visibilityEnabled)
            editor_style::message("Enable GPU classification and frustum culling in Visibility.");
    }
    if (category == RenderingCategory::Submission) {
        if (editor_style::beginPropertyGrid("submissionControls")) {
            editor_style::field("Submission");
            int mode = static_cast<int>(settings.submission);
            if (ImGui::Combo("##submission", &mode, "Direct\0Indirect\0Batched\0")) {
                const auto value =
                    mode == static_cast<int>(render::SubmissionMode::Direct)     ? "direct"
                    : mode == static_cast<int>(render::SubmissionMode::Indirect) ? "indirect"
                                                                                 : "batched";
                applyRenderSetting(settings, RenderSettingKey::Submission, value);
            }
            editorTooltip(
                "CPU indirect issues one command per retained object. GPU indirect issues one "
                "command per candidate slot; GPU batched issues one per run, including empty "
                "runs.");
            editor_style::endFields();
        }
    }
    if (category == RenderingCategory::Visibility || category == RenderingCategory::Occlusion ||
        category == RenderingCategory::Submission) {
        if (visibilityReady) {
            const auto& counts = visibility.sceneCounters;
            const uint64_t kept = uint64_t{counts.visible} + counts.bypassed[1] +
                                  counts.bypassed[2] + counts.bypassed[3] + counts.bypassed[4];
            ImGui::TextWrapped("%llu kept / %u candidates · %u culled",
                               static_cast<unsigned long long>(kept), counts.candidates,
                               counts.rejected);
            editorTooltip("Kept includes visible objects and conservative bypasses. Culled means "
                          "outside the camera frustum or rejected by previous-frame depth; shadows "
                          "stay unculled. The detail rows "
                          "describe the same declared or retired frame as this summary.");

        } else {
            editor_style::message("Waiting for this scene's visibility result.");
        }
        const auto group =
            category == RenderingCategory::Occlusion    ? VisibilityFieldGroup::Occlusion
            : category == RenderingCategory::Submission ? VisibilityFieldGroup::Submission
                                                        : VisibilityFieldGroup::Visibility;
        const auto fields = visibilityFields(visibility, {});
        const auto diagnostic = [](const VisibilityField& row) {
            return row.group == VisibilityFieldGroup::Frame ||
                   row.label.find("allocation") != std::string::npos ||
                   row.label.find("storage") != std::string::npos ||
                   row.label.find("payload") != std::string::npos ||
                   row.label.find("preparation") != std::string::npos ||
                   row.label == "HZB source frame";
        };
        const auto drawRows = [&](bool details) {
            if (!editor_style::beginPropertyGrid(details ? "visibilityDiagnostics"
                                                         : "visibilityReadings"))
                return;
            if (currentVisibility) {
                for (const auto& row : fields) {
                    if (row.label.starts_with("lmx.pass.") ||
                        row.label == "GPU visibility timings" ||
                        row.label == "CPU classify / prepare")
                        continue;
                    if (diagnostic(row) != details ||
                        (row.group != group && row.group != VisibilityFieldGroup::Frame))
                        continue;
                    valueRow(row.label.c_str(), row.value);
                }
            } else
                valueRow("Visibility", "Waiting for this scene's rendered frame");
            editor_style::endFields();
        };
        drawRows(false);
        drawPerformanceDetails(context);
        if (editor_style::beginDiagnostics()) {
            drawRows(true);
            editor_style::endDiagnostics();
        }
    }
    if (category == RenderingCategory::Display) {
        if (editor_style::beginPropertyGrid("displayEditFields")) {
            editor_style::colorRgb("Clear color (sRGB)", "clearColor", renderer.clearColor, true);
            checkbox("Wireframe", "##wireframe", &settings.wireframe);
            editorTooltip("Draw scene mesh triangle edges with the wireframe raster pipeline. "
                          "This changes the rendered scene image.");
            checkbox("Transient pooling", "##pooling", &settings.poolingEnabled);
            editorTooltip("Reuse GPU heap memory for transient graph resources whose lifetimes "
                          "do not overlap. Inspect assignments and memory totals in Render Graph.");
            editor_style::endFields();
        }
        if (editor_style::beginDiagnostics()) {
            drawDisplaySection(context);
            editor_style::endDiagnostics();
        }
    }
    if (category == RenderingCategory::Lighting) {
        drawLightingTopic(context);
    }
    if (category == RenderingCategory::SceneTables) {
        if (editor_style::beginDiagnostics()) {
            if (editor_style::beginPropertyGrid("sceneTableFields")) {
                for (const auto& row : sceneTableFields(context.session.tableStats()))
                    valueRow(row.label.data(), row.value);
                editor_style::endFields();
            }
            editor_style::endDiagnostics();
        }
    }
}

} // namespace lmx::app
