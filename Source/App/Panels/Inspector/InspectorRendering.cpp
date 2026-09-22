//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorRendering.cpp
/// @brief Implements the Inspector panel's rendering, temporal and display controls.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorInternal.h"

#include "App/Model/Rendering/Visibility/VisibilityDiagnostics.h"
#include "App/Model/Scene/SceneTableDisplay.h"
#include "App/Panels/Inspector/InspectorLighting.h"
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

namespace {

// A non-empty percentile window is required by ExposureResolve.slang's weighted average.
constexpr float kMinExposurePercentileGap = 1.0f;

} // namespace

using editor_style::checkbox;
using editor_style::field;
using editor_style::slider;

//======================================================================================================================
void drawTemporalSection(const InspectorPanelContext& context) {
    auto& settings = context.settings;
    const auto status = context.renderer.temporalStatus();
    const auto presentation =
        temporalPresentation(context.temporalState, settings, status, context.temporalSupport,
                             context.renderer.width(), context.renderer.height());
    const auto category = static_cast<RenderingCategory>(context.selection.index);
    if (category == RenderingCategory::Reconstruction) {
        drawRenderingReset(context, EditorRenderGroup::Reconstruction);
        if (editor_style::beginFields("reconstructionFields", 300.0f)) {
            checkbox("Temporal inputs", "##temporal", &settings.temporalEnabled);
            editorTooltip("Enable motion and history inputs for reconstruction and diagnostics. "
                          "Turning this off renders at full resolution; the algorithm, scale and "
                          "diagnostic requests are retained.");
            ImGui::BeginDisabled(!settings.temporalEnabled);
            field("Algorithm");
            if (ImGui::BeginCombo("##reconstruction",
                                  std::string(presentation.requestedName).c_str())) {
                for (int index = 0; index < 3; ++index) {
                    const auto mode = static_cast<render::ReconstructionMode>(index);
                    const auto name = reconstructionName(mode, context.temporalSupport);
                    if (ImGui::Selectable(std::string(name).c_str(),
                                          settings.reconstruction == mode)) {
                        settings.reconstruction = mode;
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
        const auto effectiveMode =
            render::resolveReconstruction(settings.reconstruction, context.temporalSupport,
                                          status.vendorFallback ==
                                              render::VendorFallback::CreationFailed)
                .mode;
        settings.temporalDebugView =
            clampTemporalDebugView(settings.temporalDebugView, effectiveMode);
        if (!settings.temporalEnabled)
            editor_style::message("Off: full resolution; temporal settings are retained.");
        if (settings.temporalEnabled &&
            settings.temporalDebugView != render::TemporalDebugView::Off)
            editor_style::message("Diagnostic image active. Choose Final below.");
        {
            ImGui::SeparatorText("Advanced & diagnostics");
            ImGui::BeginDisabled(!settings.temporalEnabled);
            if (editor_style::beginFields("temporalDiagnostics", 300.0f)) {
                checkbox("Jitter", "##jitter", &settings.jitterEnabled);
                editorTooltip(
                    "Offset raster samples each frame for temporal reconstruction. Motion "
                    "vectors stay unjittered; turning jitter off does not stop the sequence.");
                constexpr const char* kDebugViewNames[] = {
                    "Final",          "Motion vectors", "Reprojection error", "Reprojected history",
                    "Rejection mask", "Blend weight",   "History age"};
                field("Diagnostic view");
                if (ImGui::BeginCombo(
                        "##debugView",
                        kDebugViewNames[static_cast<size_t>(settings.temporalDebugView)])) {
                    for (size_t index = 0; index < std::size(kDebugViewNames); ++index) {
                        const auto view = static_cast<render::TemporalDebugView>(index);
                        const bool unavailable =
                            clampTemporalDebugView(view, effectiveMode) != view;
                        ImGui::BeginDisabled(unavailable);
                        if (ImGui::Selectable(kDebugViewNames[index],
                                              settings.temporalDebugView == view)) {
                            settings.temporalDebugView = view;
                            if (view != render::TemporalDebugView::Off) {
                                settings.lightDebugView = engine::LightDebugView::Off;
                                settings.hzbDebugLevel = -1;
                            }
                        }
                        ImGui::EndDisabled();
                        if (unavailable) {
                            editorTooltip("Requires native accumulation; choose Native TAA to "
                                          "inspect this view.");
                        }
                    }
                    ImGui::EndCombo();
                }
                editorTooltip("Replace the final image with a temporal diagnostic. The Viewport "
                              "explains its colors and units; Final restores the rendered image.");
                editor_style::endFields();
            }
            ImGui::EndDisabled();
            if (ImGui::Button("Reset history")) {
                requestCameraCut(context.temporalState);
            }
            editorTooltip("Discard temporal history on the next frame without moving the camera. "
                          "Use after a camera teleport or to inspect history warmup.");
        }
        {
            ImGui::SeparatorText("History & vendor details");
            if (beginReadings("historyFields")) {
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
            editor_style::message("Motion = current UV - previous UV, unjittered render-extent UV, "
                                  "+Y down; infinity marks invalid motion.");
        }
    }
    if (category == RenderingCategory::Resolution) {
        drawRenderingReset(context, EditorRenderGroup::Resolution);
        if (editor_style::beginFields("resolutionFields", 300.0f)) {
            ImGui::BeginDisabled(!settings.temporalEnabled || settings.dynamicResolutionEnabled);
            slider("Render scale", "##scale", &settings.renderScale, render::kMinRenderScale, 1.0f);
            editorTooltip("Scale the render width and height relative to output; 0.50 uses one "
                          "quarter as many pixels. Reconstruction returns the image to output "
                          "size. Device limits may clamp the effective scale.");
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
        {
            ImGui::SeparatorText("Timing & scale details");
            if (beginReadings("resolutionDetails")) {
                valueRow("Effective scale", std::format("{:.2f}", presentation.effectiveScale));
                valueRow("Live GPU passes",
                         context.temporalState.liveTimedPassSumMilliseconds &&
                                 !presentation.waitingForDeclaration
                             ? std::format("{:.2f} ms · frame {}",
                                           *context.temporalState.liveTimedPassSumMilliseconds,
                                           context.temporalState.liveMeasurementFrame)
                             : "Waiting for compatible retired sample");
                editorTooltip("Newest retired timed-pass sum compatible with the active rendering "
                              "context. It remains live while Performance is frozen and excludes "
                              "presentation, driver and untimed GPU work.");
                const auto& controller = context.dynamicResolutionState;
                valueRow("Controller", dynamicResolutionActive(settings) ? "Active" : "Inactive");
                valueRow("Controller sample", controller.lastMeasurementFrame == 0
                                                  ? "N/A"
                                                  : std::format("{:.2f} ms · frame {}",
                                                                controller.lastObservedMilliseconds,
                                                                controller.lastMeasurementFrame));
                editorTooltip("Most recent sample offered while dynamic resolution was active. "
                              "It may be skipped during settling or if its scale is obsolete; this "
                              "value stays unchanged while the controller is inactive.");
                editor_style::endFields();
            }
            editor_style::message("GPU pass sum excludes presentation, driver and untimed work. "
                                  "These readings stay live when Performance is frozen.");
        }
    }
}

//======================================================================================================================
void drawDisplaySection(const InspectorPanelContext& context) {
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
void drawRenderingSection(const InspectorPanelContext& context) {
    const auto category = static_cast<RenderingCategory>(context.selection.index);
    auto& settings = context.settings;
    auto& renderer = context.renderer;
    const auto status = renderer.temporalStatus();
    const auto effectiveMode = render::resolveReconstruction(
                                   settings.reconstruction, context.temporalSupport,
                                   status.vendorFallback == render::VendorFallback::CreationFailed)
                                   .mode;
    settings.temporalDebugView = clampTemporalDebugView(settings.temporalDebugView, effectiveMode);
    const auto presentation =
        temporalPresentation(context.temporalState, settings, status, context.temporalSupport,
                             renderer.width(), renderer.height());
    if (category == RenderingCategory::Overview || category == RenderingCategory::Reconstruction ||
        category == RenderingCategory::Resolution) {
        ImGui::TextColored(editor_style::kAccent, "%s · %.0f%%",
                           std::string(presentation.effectiveName).c_str(),
                           presentation.effectiveScale * 100.0f);
        ImGui::TextWrapped("Render %u × %u px  /  Output %u × %u px",
                           presentation.extents.renderWidth, presentation.extents.renderHeight,
                           presentation.extents.outputWidth, presentation.extents.outputHeight);
    }
    if (presentation.requestedName != presentation.effectiveName)
        ImGui::TextWrapped("Requested: %s", std::string(presentation.requestedName).c_str());
    if (!presentation.fallbackReason.empty()) {
        editor_style::message(std::string(presentation.fallbackReason).c_str(), true);
    }
    const auto& visibility = context.visibilityDisplay ? context.visibilityDisplay->readingsStatus()
                                                       : renderer.visibilityStatus();
    const bool currentVisibility =
        visibility.frameNumber != 0 &&
        visibility.sceneGeneration == context.temporalState.sceneGeneration;
    const bool visibilityReady =
        currentVisibility &&
        (visibility.classifyMode == render::ClassifyMode::Cpu || visibility.isRetired);
    const auto& latestVisibility = context.visibilityDisplay ? context.visibilityDisplay->status()
                                                             : renderer.visibilityStatus();
    if (latestVisibility.frameNumber != 0 &&
        latestVisibility.sceneGeneration == context.temporalState.sceneGeneration &&
        (latestVisibility.classifyMode == render::ClassifyMode::Cpu ||
         latestVisibility.isRetired)) {
        const auto failure = visibilityFailure(latestVisibility);
        if (!failure.empty())
            editor_style::message(failure.c_str(), true);
    }
    if (category == RenderingCategory::Overview) {
        editor_style::message("Select a topic to tune its controls and watch its live readings.");
        ImGui::SeparatorText("Rendering topics");
        for (size_t index = 1; index < static_cast<size_t>(RenderingCategory::Count); ++index) {
            const auto topic = static_cast<RenderingCategory>(index);
            if (ImGui::Selectable(renderingCategoryLabel(topic).data()))
                context.selection.index = index;
        }
        return;
    }
    drawTemporalSection(context);
    if (category == RenderingCategory::Visibility) {
        if (ImGui::Checkbox("Frustum culling", &settings.visibilityEnabled) &&
            !settings.visibilityEnabled) {
            settings.occlusionEnabled = false;
            settings.occlusionCheck = false;
            settings.hzbDebugLevel = -1;
        }
        editorTooltip("Conservative camera-frustum test. Shadow candidates stay unculled.");
        if (editor_style::beginFields("visibilityControls", 300.0f)) {
            editor_style::field("Classifier");
            int classifier = static_cast<int>(settings.classifyMode);
            if (ImGui::Combo("##classifier", &classifier, "CPU\0GPU\0")) {
                settings.classifyMode = static_cast<render::ClassifyMode>(classifier);
                if (settings.classifyMode == render::ClassifyMode::Gpu &&
                    settings.submission == render::SubmissionMode::Direct)
                    settings.submission = render::SubmissionMode::Indirect;
                if (settings.classifyMode == render::ClassifyMode::Cpu) {
                    settings.classifyCheck = false;
                    settings.occlusionEnabled = false;
                    settings.occlusionCheck = false;
                    settings.hzbDebugLevel = -1;
                }
            }
            editorTooltip(
                "GPU results arrive after retirement. CPU remains the default reference.");
            if (settings.classifyMode == render::ClassifyMode::Gpu) {
                checkbox("Verify against CPU", "##classifyCheck", &settings.classifyCheck);
                editorTooltip(
                    "CPU oracle check: compare GPU states, ordered rows, counts and arguments "
                    "with the CPU reference. Diagnostic runs are unscored.");
            }
            editor_style::endFields();
        }
    }
    if (category == RenderingCategory::Occlusion) {
        if (editor_style::beginFields("occlusionControls", 300.0f)) {
            const bool occlusionAvailable =
                settings.classifyMode == render::ClassifyMode::Gpu && settings.visibilityEnabled;
            ImGui::BeginDisabled(!occlusionAvailable);
            checkbox("Occlusion", "##occlusion", &settings.occlusionEnabled);
            ImGui::EndDisabled();
            editorTooltip(occlusionAvailable ? "Previous-frame depth evidence; camera motion can "
                                               "delay newly visible geometry by one frame. "
                                               "Any coverage change retains all candidates."
                                             : "Requires GPU classification and frustum culling.");
            if (!settings.occlusionEnabled) {
                settings.occlusionCheck = false;
                settings.hzbDebugLevel = -1;
            } else {
                checkbox("Independent ID check", "##occlusionCheck", &settings.occlusionCheck);
                editorTooltip("Draw all candidates independently and check missing visible "
                              "instances. Unscored.");
                editor_style::field("HZB view");
                const std::string preview = settings.hzbDebugLevel < 0
                                                ? "Final"
                                                : std::format("Level {}", settings.hzbDebugLevel);
                if (ImGui::BeginCombo("##hzbLevel", preview.c_str())) {
                    if (ImGui::Selectable("Final", settings.hzbDebugLevel < 0))
                        settings.hzbDebugLevel = -1;
                    uint32_t width = (renderer.width() + 1) / 2;
                    uint32_t height = (renderer.height() + 1) / 2;
                    uint32_t levels = 1;
                    while (width > 16 || height > 16) {
                        width = (width + 1) / 2;
                        height = (height + 1) / 2;
                        ++levels;
                    }
                    for (uint32_t level = 0; level < levels; ++level) {
                        if (ImGui::Selectable(std::format("Level {}", level).c_str(),
                                              settings.hzbDebugLevel ==
                                                  static_cast<int32_t>(level))) {
                            settings.hzbDebugLevel = static_cast<int32_t>(level);
                            settings.lightDebugView = engine::LightDebugView::Off;
                            settings.temporalDebugView = render::TemporalDebugView::Off;
                        }
                    }
                    ImGui::EndCombo();
                }
                checkbox("Rejected bounds (max 128)", "##occlusionBounds",
                         &settings.showOcclusionBounds);
            }
            editor_style::endFields();
        }
        if (settings.classifyMode != render::ClassifyMode::Gpu || !settings.visibilityEnabled)
            editor_style::message("Enable GPU classification and frustum culling in Visibility.");
    }
    if (category == RenderingCategory::Submission) {
        if (editor_style::beginFields("submissionControls", 300.0f)) {
            editor_style::field("Submission");
            int mode = static_cast<int>(settings.submission);
            if (ImGui::Combo("##submission", &mode, "Direct\0Indirect\0Batched\0")) {
                settings.submission = static_cast<render::SubmissionMode>(mode);
                if (settings.submission == render::SubmissionMode::Direct) {
                    settings.classifyMode = render::ClassifyMode::Cpu;
                    settings.classifyCheck = false;
                    settings.occlusionEnabled = false;
                    settings.occlusionCheck = false;
                    settings.hzbDebugLevel = -1;
                }
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
            editor_style::message(
                std::format("{} frame {}{}", visibility.isRetired ? "GPU retired" : "CPU declared",
                            visibility.frameNumber,
                            visibility.checkEnabled
                                ? (visibility.checkPassed() ? " · Check passed" : " · Check FAILED")
                                : "")
                    .c_str());
        } else {
            editor_style::message("Waiting for this scene's visibility result.");
        }
        ImGui::SeparatorText("Live readings");
        editorTooltip(
            "Updates every 250 ms. Counters and GPU times belong to the displayed frame.");
        if (beginReadings("visibilityDetails")) {
            if (currentVisibility) {
                const auto group =
                    category == RenderingCategory::Occlusion    ? VisibilityFieldGroup::Occlusion
                    : category == RenderingCategory::Submission ? VisibilityFieldGroup::Submission
                                                                : VisibilityFieldGroup::Visibility;
                for (const auto& row :
                     visibilityFields(visibility, context.visibilityDisplay
                                                      ? context.visibilityDisplay->readingsTimings()
                                                      : std::span<const rojoRHI::PassTiming>{})) {
                    if (row.group != group &&
                        (visibilityReady || row.group != VisibilityFieldGroup::Frame))
                        continue;
                    std::string label = row.label;
                    for (const std::string_view prefix :
                         {"lmx.pass.visibility.", "lmx.pass.hzb."}) {
                        if (label.starts_with(prefix))
                            label = std::format("GPU {}", label.substr(prefix.size()));
                    }
                    valueRow(label.c_str(), row.value);
                    editorTooltip(row.label.c_str());
                }
            } else {
                valueRow("Visibility", "Waiting for this scene's rendered frame");
            }
            editor_style::endFields();
        }
    }
    if (category == RenderingCategory::Exposure) {
        drawRenderingReset(context, EditorRenderGroup::Exposure);
        if (editor_style::beginFields("exposureFields", 300.0f)) {
            slider("Manual exposure (EV)", "##exposure", &settings.exposureEv, -6.0f, 6.0f);
            editorTooltip("Each +1 EV doubles manual exposure. With auto exposure enabled, this "
                          "value seeds exposure when it resets; Compensation adjusts metering.");
            bool automatic = settings.autoExposureEnabled;
            if (checkbox("Auto exposure", "##autoExposure", &automatic)) {
                setAutoExposureEnabled(settings, context.exposureContext,
                                       context.exposureResetPending, automatic);
            }
            editorTooltip("Meter scene luminance and apply the result on the following frame. "
                          "Enabling starts from Manual exposure, then adapts toward the target.");
            editor_style::endFields();
        }
        {
            ImGui::SeparatorText("Metering details");
            ImGui::BeginDisabled(!settings.autoExposureEnabled);
            if (editor_style::beginFields("meteringFields", 300.0f)) {
                slider("Low percentile (%)", "##low", &settings.exposureLowPercentile, 0.0f,
                       settings.exposureHighPercentile - kMinExposurePercentileGap, "%.0f");
                editorTooltip(
                    "Exclude the darkest part of the pixel population from metering. "
                    "50% discards the darkest half; the retained range must stay nonempty.");
                slider("High percentile (%)", "##high", &settings.exposureHighPercentile,
                       settings.exposureLowPercentile + kMinExposurePercentileGap, 100.0f, "%.0f");
                editorTooltip("Upper edge of the retained pixel population. 95% discards the "
                              "brightest 5%; the remaining log luminance determines exposure.");
                slider("Target grey", "##grey", &settings.exposureTargetGrey, 0.01f, 1.0f, "%.3f");
                editorTooltip("Linear luminance the metered average should reach before the "
                              "display transform. Higher values request a brighter exposure.");
                slider("Minimum (EV)", "##minimum", &settings.exposureEvMin, -12.0f,
                       settings.exposureEvMax);
                editorTooltip("Lower limit on the applied automatic exposure, in stops.");
                slider("Maximum (EV)", "##maximum", &settings.exposureEvMax, settings.exposureEvMin,
                       12.0f);
                editorTooltip("Upper limit on the applied automatic exposure, in stops.");
                slider("Compensation (EV)", "##compensation", &settings.exposureCompensationEv,
                       -6.0f, 6.0f);
                editorTooltip("Bias the automatic metering target before its exposure limits. "
                              "+1 EV requests twice the exposure.");
                slider("Adapt up (stops/s)", "##adaptUp", &settings.exposureAdaptUpStopsPerSecond,
                       0.0f, 16.0f);
                editorTooltip("Maximum brightening rate, using the fixed 1/60-second step per "
                              "rendered frame. Zero snaps immediately in this direction.");
                slider("Adapt down (stops/s)", "##adaptDown",
                       &settings.exposureAdaptDownStopsPerSecond, 0.0f, 16.0f);
                editorTooltip("Maximum darkening rate, using the fixed 1/60-second step per "
                              "rendered frame. Zero snaps immediately in this direction.");
                editor_style::endFields();
            }
            ImGui::EndDisabled();
            if (!settings.autoExposureEnabled)
                editor_style::message("Enable auto exposure to edit metering and adaptation.");
        }
    }
    if (category == RenderingCategory::Bloom) {
        drawRenderingReset(context, EditorRenderGroup::Bloom);
        if (editor_style::beginFields("bloomFields", 300.0f)) {
            checkbox("Bloom", "##bloom", &settings.bloomEnabled);
            ImGui::BeginDisabled(!settings.bloomEnabled);
            slider("Threshold (linear)", "##threshold", &settings.bloomThreshold, 0.0f, 10.0f);
            editorTooltip("Bloom extracts highlights above this pre-exposed linear luminance. "
                          "Changing exposure also changes which highlights cross the threshold.");
            slider("Intensity", "##intensity", &settings.bloomIntensity, 0.0f, 2.0f);
            ImGui::EndDisabled();
            editor_style::endFields();
        }
        if (!settings.bloomEnabled)
            editor_style::message("Enable bloom to edit its threshold and intensity.");
    }
    if (category == RenderingCategory::Shadows) {
        drawRenderingReset(context, EditorRenderGroup::Shadows);
        if (editor_style::beginFields("shadowFields", 300.0f)) {
            field("Shadow filter");
            int filter = static_cast<int>(settings.shadowFilter);
            constexpr const char* kFilterNames[] = {"PCF", "PCSS"};
            if (ImGui::Combo("##shadowFilter", &filter, kFilterNames, 2))
                settings.shadowFilter = static_cast<render::ShadowFilter>(filter);
            editor_style::endFields();
        }
    }
    if (category == RenderingCategory::Display) {
        drawRenderingReset(context, EditorRenderGroup::Display);
        if (editor_style::beginFields("displayEditFields", 300.0f)) {
            field("Clear color (sRGB)");
            ImGui::ColorEdit4("##clearColor", renderer.clearColor);
            checkbox("Wireframe", "##wireframe", &settings.wireframe);
            editorTooltip("Draw scene mesh triangle edges with the wireframe raster pipeline. "
                          "This changes the rendered scene image.");
            checkbox("Transient pooling", "##pooling", &settings.poolingEnabled);
            editorTooltip("Reuse GPU heap memory for transient graph resources whose lifetimes "
                          "do not overlap. Inspect assignments and memory totals in Render Graph.");
            editor_style::endFields();
        }
        drawDisplaySection(context);
    }
    if (category == RenderingCategory::Lighting) {
        drawLightingSection(context);
    }
    if (category == RenderingCategory::SceneTables) {
        editor_style::message("Current scene storage and the most recent table upload.");
        if (beginReadings("sceneTableFields")) {
            for (const auto& row : sceneTableFields(context.session.tableStats()))
                valueRow(row.label.data(), row.value);
            editor_style::endFields();
        }
    }
}

} // namespace lmx::app
