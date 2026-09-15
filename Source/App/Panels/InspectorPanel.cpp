//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorPanel.cpp
/// @brief Implements the Inspector panel's per-subject Camera, Rendering, Light, Object sections.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/InspectorPanel.h"

#include "App/EditorShell.h"
#include "App/Model/DirectionalLightRole.h"
#include "App/Model/EditorRenderDefaults.h"
#include "App/Model/SceneTableDisplay.h"
#include "App/Panels/EditorStyle.h"
#include "Render/Temporal.h"
#include "Render/TemporalHistory.h"

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
    const auto initial = scene::cameraFromScene(context.session.scene().initialCamera);
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
    if (ImGui::Button("Reset group")) {
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
    ImGui::SameLine();
    editor_style::message(changed ? "Changed from defaults" : "Editor defaults");
    ImGui::PopID();
}

//======================================================================================================================
void drawTemporalSection(const InspectorPanelContext& context) {
    auto& settings = context.settings;
    const auto status = context.renderer.temporalStatus();
    const auto presentation =
        temporalPresentation(context.temporalState, settings, status, context.temporalSupport,
                             context.renderer.width(), context.renderer.height());
    if (ImGui::CollapsingHeader("Reconstruction", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawRenderingReset(context, EditorRenderGroup::Reconstruction);
        if (editor_style::beginFields("reconstructionFields")) {
            checkbox("Temporal inputs", "##temporal", &settings.temporalEnabled);
            editorTooltip("Enable motion and history inputs for reconstruction and diagnostics. "
                          "Turning this off renders at full resolution; the algorithm, scale and "
                          "diagnostic requests are retained.");
            ImGui::BeginDisabled(!settings.temporalEnabled);
            checkbox("Jitter", "##jitter", &settings.jitterEnabled);
            editorTooltip(
                "Offset raster samples each frame for temporal reconstruction. Motion "
                "vectors stay unjittered; turning jitter off does not stop the sequence.");
            field("Requested algorithm");
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
            const auto effectiveMode =
                render::resolveReconstruction(settings.reconstruction, context.temporalSupport,
                                              status.vendorFallback ==
                                                  render::VendorFallback::CreationFailed)
                    .mode;
            settings.temporalDebugView =
                clampTemporalDebugView(settings.temporalDebugView, effectiveMode);
            constexpr const char* kDebugViewNames[] = {
                "Final",          "Motion vectors", "Reprojection error", "Reprojected history",
                "Rejection mask", "Blend weight",   "History age"};
            field("Diagnostic view");
            if (ImGui::BeginCombo(
                    "##debugView",
                    kDebugViewNames[static_cast<size_t>(settings.temporalDebugView)])) {
                for (size_t index = 0; index < std::size(kDebugViewNames); ++index) {
                    const auto view = static_cast<render::TemporalDebugView>(index);
                    const bool unavailable = clampTemporalDebugView(view, effectiveMode) != view;
                    ImGui::BeginDisabled(unavailable);
                    if (ImGui::Selectable(kDebugViewNames[index],
                                          settings.temporalDebugView == view)) {
                        settings.temporalDebugView = view;
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
            ImGui::EndDisabled();
            editor_style::endFields();
            if (!settings.temporalEnabled) {
                editor_style::message("Temporal inputs are off. Jitter, reconstruction and "
                                      "diagnostics are inactive; requests are retained.");
            } else if (effectiveMode == render::ReconstructionMode::VendorTemporal) {
                editor_style::message(
                    "Rejection, blend weight and history age require Native TAA accumulation.");
            }
        }
        if (ImGui::Button("Camera cut")) {
            requestCameraCut(context.temporalState);
        }
        editorTooltip("Discard temporal history on the next frame without moving the camera. "
                      "Use after a camera teleport or to inspect history warmup.");
        if (ImGui::TreeNode("History & vendor details")) {
            if (editor_style::beginFields("historyFields")) {
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
            ImGui::TreePop();
        }
    }
    if (ImGui::CollapsingHeader("Resolution", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawRenderingReset(context, EditorRenderGroup::Resolution);
        if (editor_style::beginFields("resolutionFields")) {
            ImGui::BeginDisabled(!settings.temporalEnabled || settings.dynamicResolutionEnabled);
            slider("Requested scale", "##scale", &settings.renderScale, render::kMinRenderScale,
                   1.0f);
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
            ImGui::BeginDisabled(!dynamicResolutionActive(settings));
            slider("Timed-pass budget (ms)", "##budget", &settings.gpuBudgetMilliseconds, 2.0f,
                   33.0f);
            editorTooltip("Budget for the sum of measured GPU passes. The controller keeps "
                          "headroom and waits for repeated samples before changing scale. "
                          "This is not a total frame-time or FPS limit.");
            ImGui::EndDisabled();
            valueRow("Effective scale", std::format("{:.2f}", presentation.effectiveScale));
            valueRow("Live retired timed-pass sum",
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
            valueRow("Last controller observation",
                     controller.lastMeasurementFrame == 0
                         ? "N/A"
                         : std::format("{:.2f} ms · frame {}", controller.lastObservedMilliseconds,
                                       controller.lastMeasurementFrame));
            editorTooltip("Most recent sample offered while dynamic resolution was active. "
                          "It may be skipped during settling or if its scale is obsolete; this "
                          "value stays unchanged while the controller is inactive.");
            editor_style::endFields();
        }
        if (!settings.temporalEnabled) {
            editor_style::message("Temporal inputs are off: full-resolution rendering uses scale "
                                  "1.00. Scale and dynamic-resolution requests are retained.");
        } else if (settings.dynamicResolutionEnabled) {
            editor_style::message(
                "Dynamic resolution controls scale. Disable it to set scale manually.");
        } else {
            editor_style::message(
                "Manual scale is active. Enable dynamic resolution to edit its timed-pass budget.");
        }
        editor_style::message("Timed-pass sum excludes presentation, driver and untimed GPU work. "
                              "Inspector measurements remain live when Performance is frozen.");
    }
}

//======================================================================================================================
void drawRenderingSection(const InspectorPanelContext& context) {
    auto& settings = context.settings;
    auto& renderer = context.renderer;
    const auto status = renderer.temporalStatus();
    const auto presentation =
        temporalPresentation(context.temporalState, settings, status, context.temporalSupport,
                             renderer.width(), renderer.height());
    ImGui::TextColored(editor_style::kAccent, "%s",
                       std::string(presentation.effectiveName).c_str());
    ImGui::TextWrapped("Render %u × %u px  /  Output %u × %u px", presentation.extents.renderWidth,
                       presentation.extents.renderHeight, presentation.extents.outputWidth,
                       presentation.extents.outputHeight);
    ImGui::TextWrapped("Requested: %s", std::string(presentation.requestedName).c_str());
    if (!presentation.fallbackReason.empty()) {
        editor_style::message(std::string(presentation.fallbackReason).c_str(), true);
    }
    if (ImGui::CollapsingHeader("Visibility", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Frustum culling", &settings.visibilityEnabled);
        editorTooltip("Conservative camera-frustum test. Shadow candidates stay unculled.");
        if (editor_style::beginFields("visibilityControls")) {
            editor_style::field("Classifier");
            int classifier = static_cast<int>(settings.classifyMode);
            if (ImGui::Combo("##classifier", &classifier, "CPU\0GPU\0")) {
                settings.classifyMode = static_cast<render::ClassifyMode>(classifier);
                if (settings.classifyMode == render::ClassifyMode::Gpu &&
                    settings.submission == render::SubmissionMode::Direct)
                    settings.submission = render::SubmissionMode::Indirect;
                if (settings.classifyMode == render::ClassifyMode::Cpu)
                    settings.classifyCheck = false;
            }
            editorTooltip(
                "GPU results arrive after retirement. CPU remains the default reference.");
            if (settings.classifyMode == render::ClassifyMode::Gpu) {
                editor_style::field("CPU oracle check");
                ImGui::Checkbox("##classifyCheck", &settings.classifyCheck);
                editorTooltip("Compares retired states, ordered rows, counts and arguments; "
                              "unscored diagnostics.");
            }
            editor_style::field("Submission");
            int mode = static_cast<int>(settings.submission);
            if (ImGui::Combo("##submission", &mode, "Direct\0Indirect\0Batched\0")) {
                settings.submission = static_cast<render::SubmissionMode>(mode);
                if (settings.submission == render::SubmissionMode::Direct) {
                    settings.classifyMode = render::ClassifyMode::Cpu;
                    settings.classifyCheck = false;
                }
            }
            editorTooltip(
                "CPU indirect issues one command per retained object. GPU indirect issues one "
                "command per candidate slot; GPU batched issues one per run, including empty "
                "runs.");
            const auto& visibility = context.visibilityDisplay ? context.visibilityDisplay->status()
                                                               : renderer.visibilityStatus();
            if (visibility.frameNumber != 0 &&
                visibility.sceneGeneration == context.temporalState.sceneGeneration) {
                for (const auto& field :
                     visibilityFields(visibility, context.visibilityDisplay
                                                      ? context.visibilityDisplay->timings()
                                                      : std::span<const rhi::PassTiming>{}))
                    valueRow(field.label.c_str(), field.value);
            } else {
                valueRow("Visibility", "Waiting for this scene's rendered frame");
            }
            editor_style::endFields();
        }
    }
    if (ImGui::CollapsingHeader("Exposure")) {
        drawRenderingReset(context, EditorRenderGroup::Exposure);
        if (editor_style::beginFields("exposureFields")) {
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
        if (ImGui::TreeNode("Metering details")) {
            ImGui::BeginDisabled(!settings.autoExposureEnabled);
            if (editor_style::beginFields("meteringFields")) {
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
            ImGui::TreePop();
        }
    }
    if (ImGui::CollapsingHeader("Bloom")) {
        drawRenderingReset(context, EditorRenderGroup::Bloom);
        if (editor_style::beginFields("bloomFields")) {
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
    if (ImGui::CollapsingHeader("Shadows")) {
        drawRenderingReset(context, EditorRenderGroup::Shadows);
        if (editor_style::beginFields("shadowFields")) {
            field("Shadow filter");
            int filter = static_cast<int>(settings.shadowFilter);
            constexpr const char* kFilterNames[] = {"PCF", "PCSS"};
            if (ImGui::Combo("##shadowFilter", &filter, kFilterNames, 2))
                settings.shadowFilter = static_cast<render::ShadowFilter>(filter);
            editor_style::endFields();
        }
    }
    drawTemporalSection(context);
    if (ImGui::CollapsingHeader("Display & Details")) {
        drawRenderingReset(context, EditorRenderGroup::Display);
        if (editor_style::beginFields("displayEditFields")) {
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
        ImGui::SeparatorText("Scene tables");
        if (editor_style::beginFields("sceneTableFields")) {
            for (const auto& row : sceneTableFields(context.session.tableStats())) {
                field(row.label.data());
                ImGui::TextWrapped("%s", row.value.c_str());
            }
            editor_style::endFields();
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
        asset::DecomposedTransform transform{object.position, object.eulerDegrees, object.scale};
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
        } else if (subject == EditorSubject::Camera) {
            ImGui::TextUnformatted("Editor Camera");
        } else if (subject == EditorSubject::Rendering) {
            ImGui::TextUnformatted("Rendering");
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
        if (ImGui::BeginChild("InspectorFields", ImVec2(0, 0))) {
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
                if (ImGui::TreeNode("Display details")) {
                    drawDisplaySection(context);
                    ImGui::TreePop();
                }
                break;
            case EditorSubject::DirectionalLight:
                drawDirectionalLightSection(context, context.selection.index);
                break;
            case EditorSubject::Object:
                drawObjectSection(context, context.selection.index);
                break;
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace lmx::app
