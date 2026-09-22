//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorCamera.cpp
/// @brief Implements the Inspector panel's camera controls.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorInternal.h"

#include "App/Panels/Shared/EditorStyle.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <limits>

namespace lmx::app {

namespace {

// Never let interactive editing collapse the clip range to nothing.
constexpr float kMinClipGap = 0.01f;

} // namespace

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

} // namespace lmx::app
