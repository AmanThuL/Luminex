//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorPanel.cpp
/// @brief Implements the Inspector panel's per-subject Camera, Rendering, Light, Object sections.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/InspectorPanel.h"

#include "App/Model/Rendering/Lighting/DirectionalLightRole.h"
#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"
#include "App/Model/Rendering/Visibility/VisibilityDiagnostics.h"
#include "App/Model/Scene/SceneTableDisplay.h"
#include "App/Panels/EditorStyle.h"
#include "App/Panels/InspectorLighting.h"
#include "App/Shell/EditorShell.h"
#include "Render/Passes/Temporal/Temporal.h"
#include "Render/Passes/Temporal/TemporalHistory.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

namespace lmx::app {

namespace {

constexpr float kMinLightDirectionLength = 1e-5f;

// A non-empty percentile window is required by ExposureResolve.slang's weighted average.
constexpr float kMinExposurePercentileGap = 1.0f;

// Never let interactive editing collapse the clip range to nothing.
constexpr float kMinClipGap = 0.01f;

using editor_style::checkbox;
using editor_style::field;
using editor_style::slider;

//======================================================================================================================
void beginFieldRow(const char* label) {
    field(label);
}

//======================================================================================================================
void valueRow(const char* label, const std::string& value) {
    editor_style::readOnly(label, value.c_str());
}

//======================================================================================================================
void drawCameraSection(const InspectorPanelContext& context) {
    auto& camera = context.session.camera();
    const auto initial = engine::cameraFromScene(context.session.scene().initialCamera);
    const bool changed = camera.position != initial.position || camera.yaw != initial.yaw ||
                         camera.pitch != initial.pitch || camera.fovY != initial.fovY ||
                         camera.nearZ != initial.nearZ || camera.farZ != initial.farZ ||
                         camera.moveSpeed != initial.moveSpeed;
    if (ImGui::Button("Reset Camera")) {
        camera = initial;
        context.settings.followCameraTrack = false;
        requestCameraCut(context.temporalState);
    }
    editorTooltip("Restore this scene's initial camera, stop following its camera track, and reset "
                  "temporal history on the next frame.");
    ImGui::SameLine();
    editor_style::message(changed ? "Changed from scene default" : "Scene default");
    if (editor_style::beginFields("cameraFields")) {
        editor_style::vector3("Position (world)", "cameraPosition", &camera.position.x, 0.05f);

        // Camera stores radians; present degrees.
        beginFieldRow("Yaw (deg)");
        float yawDegrees = glm::degrees(camera.yaw);
        if (ImGui::DragFloat("##yaw", &yawDegrees, 0.5f)) {
            camera.yaw = glm::radians(yawDegrees);
        }

        beginFieldRow("Pitch (deg)");
        float pitchDegrees = glm::degrees(camera.pitch);
        // Avoid the poles where forward and world-up become parallel.
        if (ImGui::DragFloat("##pitch", &pitchDegrees, 0.5f, -89.0f, 89.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp)) {
            camera.pitch = glm::radians(pitchDegrees);
        }

        beginFieldRow("Fov Y (deg)");
        float fovDegrees = glm::degrees(camera.fovY);
        if (ImGui::DragFloat("##fovY", &fovDegrees, 0.5f, 30.0f, 110.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp)) {
            camera.fovY = glm::radians(fovDegrees);
        }

        // Each clamp reads the other field's live value, so near can never reach or pass far.
        beginFieldRow("Near (world)");
        ImGui::DragFloat("##near", &camera.nearZ, 0.01f, 0.001f, camera.farZ - kMinClipGap, "%.3f",
                         ImGuiSliderFlags_AlwaysClamp);

        beginFieldRow("Far (world)");
        ImGui::DragFloat("##far", &camera.farZ, 0.1f, camera.nearZ + kMinClipGap,
                         std::numeric_limits<float>::max(), "%.2f", ImGuiSliderFlags_AlwaysClamp);

        beginFieldRow("Fly speed (world/s)");
        ImGui::DragFloat("##flySpeed", &camera.moveSpeed, 0.1f, 0.5f, 50.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);

        ImGui::EndTable();
    }
}

//======================================================================================================================
void drawRenderingReset(const InspectorPanelContext& context, EditorRenderGroup group) {
    ImGui::PushID(static_cast<int>(group));
    const bool changed =
        renderingGroupChanged(context.settings, group) ||
        (group == EditorRenderGroup::Display &&
         (context.renderer.clearColor[0] != 0.05f || context.renderer.clearColor[1] != 0.07f ||
          context.renderer.clearColor[2] != 0.10f || context.renderer.clearColor[3] != 1.0f));
    if (ImGui::SmallButton("Reset group")) {
        resetRenderingGroup(context.settings, group);
        if (group == EditorRenderGroup::Exposure) {
            setAutoExposureEnabled(context.settings, context.exposureContext,
                                   context.exposureResetPending,
                                   context.settings.autoExposureEnabled);
        }
        if (group == EditorRenderGroup::Display) {
            constexpr std::array kClear{0.05f, 0.07f, 0.10f, 1.0f};
            std::copy(kClear.begin(), kClear.end(), context.renderer.clearColor);
        }
    }
    editorTooltip("Restore the editor defaults for this rendering group. Other groups, the camera "
                  "and scene playback keep their current settings.");
    if (changed) {
        ImGui::SameLine();
        editor_style::message("Modified");
    }
    ImGui::PopID();
}

//======================================================================================================================
bool beginReadings(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
        return false;
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.48f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.52f);
    return true;
}

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

//======================================================================================================================
void drawDirectionalLightSection(const InspectorPanelContext& context, size_t index) {
    auto& session = context.session;
    auto& light = session.scene().lights[index];
    ImGui::TextWrapped("Role: %s", std::string(directionalLightRoleLabel(index)).c_str());
    if (ImGui::Button("Reset light")) {
        session.resetLight(index);
    }
    editorTooltip("Restore this directional light's direction and scene-linear radiance from "
                  "the current scene defaults.");
    ImGui::SameLine();
    editor_style::message(session.lightChanged(index) ? "Changed from scene default"
                                                      : "Scene default");
    if (editor_style::beginFields("directionalLightFields")) {
        glm::vec3 direction = light.direction;
        if (editor_style::vector3("Direction (world)", "direction", &direction.x, 0.01f)) {
            if (glm::length(direction) > kMinLightDirectionLength) {
                light.direction = glm::normalize(direction);
            }
        }
        editor_style::vector3("Radiance (scene-linear RGB)", "radiance", &light.strength.x, 0.01f,
                              0.0f, std::numeric_limits<float>::max(), "%.3f",
                              ImGuiSliderFlags_AlwaysClamp, true);
        editor_style::endFields();
    }
    editor_style::message("Scene-linear radiance; values above 1 are valid HDR intensities.");
}

//======================================================================================================================
void drawObjectSection(const InspectorPanelContext& context, size_t index) {
    auto& session = context.session;
    auto& object = session.scene().objects[index];
    if (ImGui::Button("Reset transform")) {
        session.resetObject(index);
        requestCameraCut(context.temporalState);
    }
    editorTooltip("Restore this object's authored transform and reset temporal history. Animated "
                  "objects use their authored track at the current playback time.");
    ImGui::SameLine();
    editor_style::message(session.objectChanged(index) ? "Changed from authored pose"
                                                       : "Authored pose");
    if (editor_style::beginFields("objectFields")) {
        DecomposedTransform transform{object.position, object.eulerDegrees, object.scale};
        bool edited =
            editor_style::vector3("Position (world)", "position", &transform.position.x, 0.05f);
        edited |= editor_style::vector3("Rotation (XYZ degrees)", "rotation",
                                        &transform.eulerDegrees.x, 1.0f);
        edited |= editor_style::vector3("Scale", "scale", &transform.scale.x, 0.01f, 0.01f, 100.0f,
                                        "%.3f", ImGuiSliderFlags_AlwaysClamp);
        if (edited) {
            session.editObject(index, transform);
            requestCameraCut(context.temporalState);
        }
        valueRow("Mesh row", std::to_string(object.mesh.slot));
        valueRow("Material row", std::to_string(object.material.slot));
        const auto visibilityFields = context.visibilityDisplay == nullptr
                                          ? objectVisibilityFields(nullptr)
                                          : context.visibilityDisplay->objectFields(
                                                object.id, context.temporalState.sceneGeneration);
        for (const auto& field : visibilityFields)
            valueRow(field.label.c_str(), field.value);
        editor_style::endFields();
    }
    const bool animated =
        std::ranges::any_of(session.scene().animation.tracks,
                            [index](const auto& track) { return track.objectIndex == index; });
    if (animated) {
        editor_style::message(
            "Animated transform: pause playback to edit. Reset samples the authored track at the "
            "current time; playback replaces edits on its next sample.");
    }
}

} // namespace

//======================================================================================================================
void drawInspectorPanel(bool& open, const InspectorPanelContext& context) {
    if (ImGui::Begin(kInspectorPanelWindowName, &open)) {
        const auto subject = context.selection.subject;
        if (subject == EditorSubject::Object) {
            ImGui::TextWrapped(
                "%s", sceneObjectLabel(context.session.scene(), context.selection.index).c_str());
            editor_style::message("Object transform");
        } else if (subject == EditorSubject::DirectionalLight) {
            ImGui::TextWrapped("Light %zu", context.selection.index);
            editor_style::message("Directional light");
        } else if (subject == EditorSubject::LocalLight) {
            ImGui::TextWrapped(
                "%s",
                sceneLocalLightLabel(context.session.scene(), context.selection.lightId).c_str());
            editor_style::message("Local light");
        } else if (subject == EditorSubject::Camera) {
            ImGui::TextUnformatted("Editor Camera");
        } else if (subject == EditorSubject::Rendering) {
            ImGui::TextUnformatted(
                renderingCategoryLabel(static_cast<RenderingCategory>(context.selection.index))
                    .data());
        }
        if (context.selectionHiddenByFilter) {
            editor_style::message("Selection is hidden by the Scene search filter. Clear the "
                                  "filter to find it in the list.",
                                  true);
            if (context.sceneFilter && ImGui::Button("Clear filter")) {
                context.sceneFilter->clear();
            }
        }
        ImGui::Separator();
        const ImGuiID previousKey = ImGui::GetID("PreviousInspectorSubject");
        ImGuiStorage* storage = ImGui::GetStateStorage();
        ImGui::PushID(static_cast<int>(subject));
        ImGui::PushID(static_cast<int>(subject == EditorSubject::LocalLight
                                           ? context.selection.lightId.slot
                                           : context.selection.index));
        ImGui::PushID(static_cast<int>(context.selection.lightId.generation));
        ImGui::PushID(static_cast<int>(context.selection.lightId.store));
        const ImGuiID page = ImGui::GetID("InspectorFields");
        const bool changed = storage->GetInt(previousKey, -1) != static_cast<int>(page);
        storage->SetInt(previousKey, static_cast<int>(page));
        if (ImGui::BeginChild("InspectorFields", ImVec2(0, 0))) {
            if (changed)
                ImGui::SetScrollY(0.0f);
            switch (subject) {
            case EditorSubject::None:
                editor_style::message(
                    "Select a camera, rendering settings, light or object in Scene.");
                break;
            case EditorSubject::Camera:
                drawCameraSection(context);
                break;
            case EditorSubject::Rendering:
                drawRenderingSection(context);
                break;
            case EditorSubject::DirectionalLight:
                drawDirectionalLightSection(context, context.selection.index);
                break;
            case EditorSubject::LocalLight:
                drawLocalLightSection(context, context.selection.lightId);
                break;
            case EditorSubject::Object:
                drawObjectSection(context, context.selection.index);
                break;
            }
        }
        ImGui::EndChild();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
    }
    ImGui::End();
}

} // namespace lmx::app
