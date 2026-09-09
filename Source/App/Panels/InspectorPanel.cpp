//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorPanel.cpp
/// @brief Implements the Inspector panel's per-subject Camera, Rendering, Light, Object sections.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/InspectorPanel.h"

#include "App/DirectionalLightRole.h"
#include "App/EditorShell.h"
#include "Engine/SceneAnimation.h"
#include "Render/Temporal.h"
#include "Render/TemporalHistory.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <cstddef>
#include <iterator>
#include <limits>
#include <string_view>

namespace lmx::app {

namespace {

constexpr float kMinLightDirectionLength = 1e-5f;

// A non-empty percentile window is required by ExposureResolve.slang's weighted average.
constexpr float kMinExposurePercentileGap = 1.0f;

// Never let interactive editing collapse the clip range to nothing.
constexpr float kMinClipGap = 0.01f;

constexpr ImGuiTableFlags kFieldTableFlags = ImGuiTableFlags_SizingStretchProp;

//======================================================================================================================
// Left column holds the label, right column holds a full-width widget with a hidden ("##") ImGui
// label -- the pairing Unreal's Details panel uses -- so every row in the enclosing table shares
// one aligned label/value boundary (spec section 7's "aligned field tables").
void beginFieldRow(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

//======================================================================================================================
void drawCameraSection(render::Camera& camera, const engine::Scene& scene) {
    if (ImGui::BeginTable("cameraFields", 2, kFieldTableFlags)) {
        beginFieldRow("Position (world)");
        ImGui::DragFloat3("##position", &camera.position.x, 0.05f);

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

    if (ImGui::Button("Reset Camera")) {
        camera = cameraFromScene(scene.initialCamera);
    }
}

//======================================================================================================================
// Spec section 9's Temporal block: the toggles, the debug-view combo, playback transport, the
// camera-cut button, and read-only status pulled from the last declared frame. `scene` is the
// active scene (for the animation clock and whether a camera track exists to follow), not the
// renderer's own state -- Renderer::temporalStatus() is the only renderer-owned read here.
void drawTemporalSection(render::Renderer& renderer, EditorRenderSettings& settings,
                         engine::Scene& scene, TemporalEditorState& temporalState,
                         const DynamicResolutionState& dynamicResolutionState) {
    ImGui::Checkbox("Temporal inputs", &settings.temporalEnabled);
    ImGui::BeginDisabled(!settings.temporalEnabled);
    ImGui::Checkbox("Jitter", &settings.jitterEnabled);

    int reconstructionIndex = static_cast<int>(settings.reconstruction);
    constexpr const char* kReconstructionNames[] = {"Raw", "Native TAA"};
    if (ImGui::Combo("Reconstruction", &reconstructionIndex, kReconstructionNames,
                     static_cast<int>(std::size(kReconstructionNames)))) {
        settings.reconstruction = static_cast<render::ReconstructionMode>(reconstructionIndex);
    }

    int debugViewIndex = static_cast<int>(settings.temporalDebugView);
    constexpr const char* kDebugViewNames[] = {"Off",
                                               "Motion vectors",
                                               "Reprojection error",
                                               "Reprojected history",
                                               "Rejection mask",
                                               "Blend weight",
                                               "History age"};
    if (ImGui::Combo("Debug view", &debugViewIndex, kDebugViewNames,
                     static_cast<int>(std::size(kDebugViewNames)))) {
        settings.temporalDebugView = static_cast<render::TemporalDebugView>(debugViewIndex);
    }
    ImGui::EndDisabled();

    // Disabled while dynamic resolution drives renderScale itself -- the slider still shows the
    // controller's value, it just cannot be dragged out from under it.
    ImGui::BeginDisabled(settings.dynamicResolutionEnabled);
    ImGui::SliderFloat("Render scale", &settings.renderScale, render::kMinRenderScale, 1.0f, "%.2f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    ImGui::Checkbox("Dynamic resolution", &settings.dynamicResolutionEnabled);
    ImGui::BeginDisabled(!settings.dynamicResolutionEnabled);
    ImGui::SliderFloat("GPU budget (ms)", &settings.gpuBudgetMilliseconds, 2.0f, 33.0f, "%.2f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();

    if (ImGui::Button(settings.animationPlaying ? "Pause" : "Play")) {
        settings.animationPlaying = !settings.animationPlaying;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(settings.animationPlaying);
    if (ImGui::Button("Step")) {
        // Matches EditorShell's own per-frame step -- a manual step while paused advances by the
        // same fixed amount play would have.
        scene.advanceAnimation(1.0 / engine::kAnimationBakeRate);
        scene.animate(scene.animationTime);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset time")) {
        scene.animationTime = 0.0;
        scene.animate(0.0);
        // Rewinding the clock is a discontinuity exactly like a scene switch: the object poses
        // this frame have nothing to do with what history recorded, so motion must not report a
        // jump and the renderer must not reproject across it. requestCameraCut() forces
        // HistoryResetReason::CameraCut on the next declared frame even when the camera itself did
        // not move -- resetMotion() alone would leave the reset reason at None, and reprojecting
        // history from before the rewind onto geometry now back at t = 0 is exactly the artifact
        // this guards against.
        scene.resetMotion();
        requestCameraCut(temporalState);
    }

    const bool hasCameraTrack = !scene.animation.cameraTrack.empty();
    ImGui::BeginDisabled(!hasCameraTrack);
    ImGui::Checkbox("Follow camera track", &settings.followCameraTrack);
    ImGui::EndDisabled();

    if (ImGui::Button("Camera cut")) {
        requestCameraCut(temporalState);
    }

    const render::TemporalStatus status = renderer.temporalStatus();
    const std::string_view resetReason = render::historyResetReasonName(status.lastReset);
    ImGui::Text("Last reset: %.*s (frame %llu)", static_cast<int>(resetReason.size()),
                resetReason.data(), static_cast<unsigned long long>(status.lastResetFrame));
    ImGui::Text("Jitter index: %u", status.jitterIndex);
    ImGui::Text("History: %s, %llu bytes", status.historyValid ? "valid" : "invalid",
                static_cast<unsigned long long>(status.historyBytes));
    ImGui::Text("History age: %u", status.historyAge);
    ImGui::Text("Warmup: %s", status.warmupComplete ? "complete" : "in progress");
    ImGui::Text("Depth history: %llu bytes",
                static_cast<unsigned long long>(status.depthHistoryBytes));
    ImGui::TextWrapped("Motion = uvCurrent - uvPrevious, UV of the render extent, +y down, "
                       "unjittered; +inf = invalid.");

    ImGui::Text("Render extent: %ux%u (scale %.2f)", status.extents.renderWidth,
                status.extents.renderHeight, static_cast<double>(status.renderScale));
    ImGui::Text("Frame GPU time: %.2f ms",
                static_cast<double>(dynamicResolutionState.lastObservedMilliseconds));
    // TemporalStatus carries the declared-frame count the render extent last changed at, not how
    // many frames ago that was (it keeps no running declared-frame count of its own to subtract
    // from), so this reports the same raw form lastResetFrame does above.
    ImGui::Text("Render extent changed: at frame %llu",
                static_cast<unsigned long long>(status.lastRenderExtentChangeFrame));
}

//======================================================================================================================
void drawRenderingSection(render::Renderer& renderer, EditorRenderSettings& settings,
                          ExposureResetContext& exposureContext, bool& exposureResetPending,
                          engine::Scene& scene, TemporalEditorState& temporalState,
                          const DynamicResolutionState& dynamicResolutionState) {
    // Display-authored; Renderer::declarePasses decodes it through the existing scene-linear
    // boundary. Relocated from the Camera section verbatim -- clear color is not a camera field.
    ImGui::ColorEdit4("Clear color", renderer.clearColor);

    ImGui::Checkbox("Wireframe", &settings.wireframe);
    // Six stops each way: enough to drive a scene to black or to the tone map's shoulder, which is
    // the whole range a manual exposure control is useful over here.
    ImGui::SliderFloat("Exposure (EV)", &settings.exposureEv, -6.0f, 6.0f, "%.2f",
                       ImGuiSliderFlags_AlwaysClamp);
    // Off->on is a reset trigger (spec 9): the feedback loop has produced nothing yet, so the first
    // auto frame has to start from the manual EV exactly like a fresh scene would. On->off is not:
    // setAutoExposureEnabled only fires the trigger on the false->true edge.
    bool autoExposureEnabled = settings.autoExposureEnabled;
    if (ImGui::Checkbox("Auto exposure", &autoExposureEnabled)) {
        setAutoExposureEnabled(settings, exposureContext, exposureResetPending,
                               autoExposureEnabled);
    }
    if (settings.autoExposureEnabled) {
        ImGui::SliderFloat("Low percentile", &settings.exposureLowPercentile, 0.0f,
                           settings.exposureHighPercentile - kMinExposurePercentileGap, "%.0f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("High percentile", &settings.exposureHighPercentile,
                           settings.exposureLowPercentile + kMinExposurePercentileGap, 100.0f,
                           "%.0f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Target grey", &settings.exposureTargetGrey, 0.01f, 1.0f, "%.3f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Auto EV min", &settings.exposureEvMin, -12.0f, settings.exposureEvMax,
                           "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Auto EV max", &settings.exposureEvMax, settings.exposureEvMin, 12.0f,
                           "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Exposure compensation", &settings.exposureCompensationEv, -6.0f, 6.0f,
                           "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Adapt up (stops/s)", &settings.exposureAdaptUpStopsPerSecond, 0.0f,
                           16.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Adapt down (stops/s)", &settings.exposureAdaptDownStopsPerSecond, 0.0f,
                           16.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    }
    ImGui::Checkbox("Bloom", &settings.bloomEnabled);
    if (settings.bloomEnabled) {
        ImGui::SliderFloat("Bloom threshold", &settings.bloomThreshold, 0.0f, 10.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Bloom intensity", &settings.bloomIntensity, 0.0f, 2.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
    }
    // Off gives every transient its own memory. Nothing about the image changes -- a transient
    // cannot be read before it is written -- so what this compares is cost.
    ImGui::Checkbox("Transient pooling", &settings.poolingEnabled);
    int filterIndex = static_cast<int>(settings.shadowFilter);
    constexpr const char* kFilterNames[] = {"PCF", "PCSS"};
    if (ImGui::Combo("Shadow filter", &filterIndex, kFilterNames,
                     static_cast<int>(std::size(kFilterNames)))) {
        settings.shadowFilter = static_cast<render::ShadowFilter>(filterIndex);
    }

    ImGui::SeparatorText("Temporal");
    drawTemporalSection(renderer, settings, scene, temporalState, dynamicResolutionState);
}

//======================================================================================================================
void drawDirectionalLightSection(engine::Scene& scene, size_t index) {
    render::DirectionalLight& light = scene.lights[index];
    ImGui::Text("Role: %.*s", static_cast<int>(directionalLightRoleLabel(index).size()),
                directionalLightRoleLabel(index).data());

    if (ImGui::BeginTable("directionalLightFields", 2, kFieldTableFlags)) {
        beginFieldRow("Direction (world)");
        glm::vec3 direction = light.direction;
        if (ImGui::DragFloat3("##direction", &direction.x, 0.01f)) {
            // Reject zero directions before normalization; they would poison shadow and N.L math.
            if (glm::length(direction) > kMinLightDirectionLength) {
                light.direction = glm::normalize(direction);
            }
        }

        // Non-negative scene-linear radiance; values above one are legal HDR intensities, so this
        // is a DragFloat3 with only a lower clamp rather than a [0,1]-clamped ColorEdit3.
        beginFieldRow("Radiance (scene-linear RGB)");
        glm::vec3 radiance = light.strength;
        if (ImGui::DragFloat3("##radiance", &radiance.x, 0.01f, 0.0f,
                              std::numeric_limits<float>::max(), "%.3f",
                              ImGuiSliderFlags_AlwaysClamp)) {
            light.strength = radiance;
        }

        ImGui::EndTable();
    }
}

//======================================================================================================================
void drawObjectSection(engine::Scene& scene, size_t index) {
    engine::SceneObject& object = scene.objects[index];

    if (ImGui::BeginTable("objectFields", 2, kFieldTableFlags)) {
        beginFieldRow("Position (world)");
        ImGui::DragFloat3("##position", &object.position.x, 0.05f);

        beginFieldRow("Rotation (XYZ deg)");
        ImGui::DragFloat3("##rotation", &object.eulerDegrees.x, 1.0f);

        beginFieldRow("Scale");
        ImGui::DragFloat3("##scale", &object.scale.x, 0.01f, 0.01f, 100.0f, "%.2f",
                          ImGuiSliderFlags_AlwaysClamp);

        beginFieldRow("Mesh index");
        ImGui::Text("%u", object.meshIndex);

        beginFieldRow("Material index");
        ImGui::Text("%u", object.materialIndex);

        ImGui::EndTable();
    }
}

} // namespace

//======================================================================================================================
void drawInspectorPanel(bool& open, const InspectorPanelContext& context) {
    if (ImGui::Begin(kInspectorPanelWindowName, &open)) {
        switch (context.selection.subject) {
        case EditorSubject::None:
            ImGui::TextDisabled("Select an item in Scene");
            break;
        case EditorSubject::Camera:
            ImGui::SeparatorText("Camera");
            ImGui::TextUnformatted("Editor Camera");
            drawCameraSection(context.camera, context.scene);
            break;
        case EditorSubject::Rendering:
            ImGui::SeparatorText("Rendering");
            ImGui::TextUnformatted("Rendering");
            drawRenderingSection(context.renderer, context.settings, context.exposureContext,
                                 context.exposureResetPending, context.scene, context.temporalState,
                                 context.dynamicResolutionState);
            break;
        case EditorSubject::DirectionalLight:
            ImGui::SeparatorText("Directional Light");
            ImGui::Text("Light %d", static_cast<int>(context.selection.index));
            drawDirectionalLightSection(context.scene, context.selection.index);
            break;
        case EditorSubject::Object:
            ImGui::SeparatorText("Object");
            ImGui::TextUnformatted(context.scene.objects[context.selection.index].name.c_str());
            drawObjectSection(context.scene, context.selection.index);
            break;
        }
    }
    ImGui::End();
}

} // namespace lmx::app
