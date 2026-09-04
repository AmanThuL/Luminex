//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorPanel.cpp
/// @brief Implements the Inspector panel's per-subject Camera, Rendering, Light, Object sections.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/InspectorPanel.h"

#include "App/DirectionalLightRole.h"
#include "App/EditorShell.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <cstddef>
#include <iterator>
#include <limits>

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
void drawRenderingSection(render::Renderer& renderer, EditorRenderSettings& settings,
                          ExposureResetContext& exposureContext, bool& exposureResetPending) {
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
                                 context.exposureResetPending);
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
