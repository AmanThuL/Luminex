//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorPanel.cpp
/// @brief Implements the Inspector panel's camera, light, render-settings, and object sections.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/InspectorPanel.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <iterator>

namespace lmx::app {

namespace {

constexpr float kMinLightDirectionLength = 1e-5f;

// A non-empty percentile window is required by ExposureResolve.slang's weighted average.
constexpr float kMinExposurePercentileGap = 1.0f;

//======================================================================================================================
void drawCameraSection(render::Camera& camera, render::Renderer& renderer) {
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("position", &camera.position.x, 0.05f);
        // Present angles in degrees while Camera stores radians.
        float yawDegrees = glm::degrees(camera.yaw);
        if (ImGui::DragFloat("yaw", &yawDegrees, 0.5f)) {
            camera.yaw = glm::radians(yawDegrees);
        }
        float pitchDegrees = glm::degrees(camera.pitch);
        // Avoid the poles where forward and world-up become parallel.
        if (ImGui::DragFloat("pitch", &pitchDegrees, 0.5f, -89.0f, 89.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp)) {
            camera.pitch = glm::radians(pitchDegrees);
        }
        float fovDegrees = glm::degrees(camera.fovY);
        if (ImGui::DragFloat("fov Y", &fovDegrees, 0.5f, 30.0f, 110.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp)) {
            camera.fovY = glm::radians(fovDegrees);
        }
        ImGui::DragFloat("move speed", &camera.moveSpeed, 0.1f, 0.5f, 50.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::ColorEdit4("clear color", renderer.clearColor);
    }
}

//======================================================================================================================
void drawLightsSection(engine::Scene& scene) {
    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < std::size(scene.lights); ++i) {
            render::DirectionalLight& light = scene.lights[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Light %d", static_cast<int>(i));
            ImGui::ColorEdit3("strength", &light.strength.x);
            glm::vec3 direction = light.direction;
            if (ImGui::DragFloat3("direction", &direction.x, 0.01f)) {
                // Reject zero directions before normalization; they would poison shadow and N.L
                // math.
                if (glm::length(direction) > kMinLightDirectionLength) {
                    light.direction = glm::normalize(direction);
                }
            }
            ImGui::PopID();
        }
    }
}

//======================================================================================================================
void drawRenderSettingsSection(EditorRenderSettings& settings,
                               ExposureResetContext& exposureContext, bool& exposureResetPending) {
    if (ImGui::CollapsingHeader("Render Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Wireframe", &settings.wireframe);
        // Six stops each way: enough to drive a scene to black or to the tone map's shoulder,
        // which is the whole range a manual exposure control is useful over here.
        ImGui::SliderFloat("Exposure (EV)", &settings.exposureEv, -6.0f, 6.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
        // Off->on is a reset trigger (spec 9): the feedback loop has produced nothing yet, so the
        // first auto frame has to start from the manual EV exactly like a fresh scene would.
        // On->off is not: shouldResetExposure() only fires on the false->true edge.
        if (ImGui::Checkbox("Auto exposure", &settings.autoExposureEnabled)) {
            ExposureResetContext candidate = exposureContext;
            candidate.autoExposureEnabled = settings.autoExposureEnabled;
            if (shouldResetExposure(exposureContext, candidate)) {
                exposureResetPending = true;
            }
            exposureContext = candidate;
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
            ImGui::SliderFloat("Auto EV min", &settings.exposureEvMin, -12.0f,
                               settings.exposureEvMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Auto EV max", &settings.exposureEvMax, settings.exposureEvMin,
                               12.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Exposure compensation", &settings.exposureCompensationEv, -6.0f,
                               6.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::Checkbox("Bloom", &settings.bloomEnabled);
        if (settings.bloomEnabled) {
            ImGui::SliderFloat("Bloom threshold", &settings.bloomThreshold, 0.0f, 10.0f, "%.2f",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Bloom intensity", &settings.bloomIntensity, 0.0f, 2.0f, "%.2f",
                               ImGuiSliderFlags_AlwaysClamp);
        }
        // Off gives every transient its own memory. Nothing about the image changes -- a
        // transient cannot be read before it is written -- so what this compares is cost.
        ImGui::Checkbox("Transient pooling", &settings.poolingEnabled);
        int filterIndex = static_cast<int>(settings.shadowFilter);
        constexpr const char* kFilterNames[] = {"PCF", "PCSS"};
        if (ImGui::Combo("Shadow filter", &filterIndex, kFilterNames,
                         static_cast<int>(std::size(kFilterNames)))) {
            settings.shadowFilter = static_cast<render::ShadowFilter>(filterIndex);
        }
    }
}

//======================================================================================================================
void drawObjectsSection(engine::Scene& scene) {
    if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < scene.objects.size(); ++i) {
            engine::SceneObject& object = scene.objects[i];
            // Index IDs keep duplicate object names from sharing widget state.
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::TreeNodeEx(object.name.c_str())) {
                ImGui::DragFloat3("position", &object.position.x, 0.05f);
                ImGui::DragFloat3("rotation", &object.eulerDegrees.x, 1.0f);
                ImGui::DragFloat3("scale", &object.scale.x, 0.01f, 0.01f, 100.0f, "%.2f",
                                  ImGuiSliderFlags_AlwaysClamp);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
}

} // namespace

//======================================================================================================================
void drawInspectorPanel(bool& open, const InspectorPanelContext& context) {
    if (ImGui::Begin(kInspectorPanelWindowName, &open)) {
        drawCameraSection(context.camera, context.renderer);
        drawLightsSection(context.scene);
        drawRenderSettingsSection(context.settings, context.exposureContext,
                                  context.exposureResetPending);
        drawObjectsSection(context.scene);
    }
    ImGui::End();
}

} // namespace lmx::app
