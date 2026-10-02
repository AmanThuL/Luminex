//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorCamera.cpp
/// @brief Implements the Inspector panel's camera controls.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Inspector/InspectorInternal.h"

#include "App/Panels/Shared/EditorStyle.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <format>
#include <limits>
#include <utility>

namespace lmx::app {

namespace {

// Never let interactive editing collapse the clip range to nothing.
constexpr float kMinClipGap = 0.01f;

} // namespace

//======================================================================================================================
std::optional<ProvenanceMark> inspectorCameraProvenance(const SceneSession& session,
                                                        bool followRail, bool edited,
                                                        CameraProvenanceField field,
                                                        std::string_view label) {
    if (!edited)
        return std::nullopt;
    const auto& camera = session.camera();
    const auto* loaded = session.loadedScene();
    const auto saved = loaded ? session.documentState().sceneCamera : std::nullopt;
    bool requested = false;
    if (saved) {
        switch (field) {
        case CameraProvenanceField::Subject:
            requested = saved->position == camera.position && saved->yaw == camera.yaw &&
                        saved->pitch == camera.pitch && saved->fovY == camera.fovY &&
                        saved->nearZ == camera.nearZ && saved->farZ == camera.farZ &&
                        camera.moveSpeed ==
                            engine::cameraFromScene(session.scene().initialCamera).moveSpeed;
            break;
        case CameraProvenanceField::Position:
            requested = saved->position == camera.position;
            break;
        case CameraProvenanceField::Yaw:
            requested = saved->yaw == camera.yaw;
            break;
        case CameraProvenanceField::Pitch:
            requested = saved->pitch == camera.pitch;
            break;
        case CameraProvenanceField::FovY:
            requested = saved->fovY == camera.fovY;
            break;
        case CameraProvenanceField::NearZ:
            requested = saved->nearZ == camera.nearZ;
            break;
        case CameraProvenanceField::FarZ:
            requested = saved->farZ == camera.farZ;
            break;
        case CameraProvenanceField::Speed:
            break;
        }
    }
    const auto initial = engine::cameraFromScene(session.scene().initialCamera);
    const bool poseField = field == CameraProvenanceField::Position ||
                           field == CameraProvenanceField::Yaw ||
                           field == CameraProvenanceField::Pitch ||
                           (field == CameraProvenanceField::Subject &&
                            camera.fovY == initial.fovY && camera.nearZ == initial.nearZ &&
                            camera.farZ == initial.farZ && camera.moveSpeed == initial.moveSpeed);
    Actor actor = Actor::Operator;
    Provenance kind = requested ? Provenance::Edited : Provenance::SessionOnly;
    std::string source = requested   ? "Set Scene Camera from View"
                         : poseField ? "Editor camera navigation · not saved"
                                     : "Editor camera lens or speed edit · not saved";
    if (!requested && poseField && !session.scene().animation.cameraTrack.empty()) {
        if (!followRail)
            return std::nullopt;
        auto sampled = camera;
        session.scene().followCameraTrack(sampled);
        const bool railOwned = field == CameraProvenanceField::Position
                                   ? camera.position == sampled.position
                               : field == CameraProvenanceField::Yaw ? camera.yaw == sampled.yaw
                               : field == CameraProvenanceField::Pitch
                                   ? camera.pitch == sampled.pitch
                                   : camera.position == sampled.position &&
                                         camera.yaw == sampled.yaw && camera.pitch == sampled.pitch;
        if (railOwned) {
            actor = Actor::System;
            kind = Provenance::SystemApplied;
            source = "Scene camera rail · current sampled pose · not saved";
        }
    }
    if (loaded)
        source += " · " + loaded->path.string();
    source += " · " + std::string(label);
    return ProvenanceMark{kind, actor, std::move(source)};
}

//======================================================================================================================
void drawCameraSection(const InspectorPanelContext& context) {
    auto& camera = context.session.camera();
    const auto initial = engine::cameraFromScene(context.session.scene().initialCamera);
    const bool changed = camera.position != initial.position || camera.yaw != initial.yaw ||
                         camera.pitch != initial.pitch || camera.fovY != initial.fovY ||
                         camera.nearZ != initial.nearZ || camera.farZ != initial.farZ ||
                         camera.moveSpeed != initial.moveSpeed;
    const auto cameraMark = [&](bool edited, CameraProvenanceField field, std::string_view label) {
        return inspectorCameraProvenance(context.session, context.settings.followCameraTrack,
                                         edited, field, label);
    };
    if (drawInspectorHeader("Editor Camera", "Camera",
                            "Restore this scene's initial camera pose, lens and fly speed, stop "
                            "following its camera track, and reset temporal history.",
                            changed, nullptr,
                            cameraMark(changed, CameraProvenanceField::Subject, "Editor Camera"))) {
        camera = initial;
        context.settings.followCameraTrack = false;
        requestCameraCut(context.temporalState);
    }
    if (changed)
        editor_style::message(
            "Camera navigation and fly speed are session-only. Set Scene Camera from View "
            "explicitly saves pose and lens.");
    if (camera.moveSpeed != initial.moveSpeed)
        editor_style::message("Fly speed is not saved.");
    if (editor_style::beginPropertyGrid("cameraFields")) {
        editor_style::setNextFieldProvenance(cameraMark(camera.position != initial.position,
                                                        CameraProvenanceField::Position,
                                                        "Position (world)"));
        editor_style::vector3("Position (world)", "cameraPosition", &camera.position.x, 0.05f);

        // Camera stores radians; present degrees.
        editor_style::setNextFieldProvenance(
            cameraMark(camera.yaw != initial.yaw, CameraProvenanceField::Yaw, "Yaw (deg)"));
        beginFieldRow("Yaw (deg)");
        float yawDegrees = glm::degrees(camera.yaw);
        if (ImGui::DragFloat("##yaw", &yawDegrees, 0.5f)) {
            camera.yaw = glm::radians(yawDegrees);
        }

        editor_style::setNextFieldProvenance(
            cameraMark(camera.pitch != initial.pitch, CameraProvenanceField::Pitch, "Pitch (deg)"));
        beginFieldRow("Pitch (deg)");
        float pitchDegrees = glm::degrees(camera.pitch);
        // Avoid the poles where forward and world-up become parallel.
        if (ImGui::DragFloat("##pitch", &pitchDegrees, 0.5f, -89.0f, 89.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp)) {
            camera.pitch = glm::radians(pitchDegrees);
        }

        editor_style::setNextFieldProvenance(
            cameraMark(camera.fovY != initial.fovY, CameraProvenanceField::FovY, "Fov Y (deg)"));
        beginFieldRow("Fov Y (deg)");
        float fovDegrees = glm::degrees(camera.fovY);
        if (ImGui::DragFloat("##fovY", &fovDegrees, 0.5f, 30.0f, 110.0f, "%.1f",
                             ImGuiSliderFlags_AlwaysClamp)) {
            camera.fovY = glm::radians(fovDegrees);
        }

        // Each clamp reads the other field's live value, so near can never reach or pass far.
        editor_style::setNextFieldProvenance(cameraMark(
            camera.nearZ != initial.nearZ, CameraProvenanceField::NearZ, "Near (world)"));
        beginFieldRow("Near (world)");
        ImGui::DragFloat("##near", &camera.nearZ, 0.01f, 0.001f, camera.farZ - kMinClipGap, "%.3f",
                         ImGuiSliderFlags_AlwaysClamp);

        editor_style::setNextFieldProvenance(
            cameraMark(camera.farZ != initial.farZ, CameraProvenanceField::FarZ, "Far (world)"));
        beginFieldRow("Far (world)");
        ImGui::DragFloat("##far", &camera.farZ, 0.1f, camera.nearZ + kMinClipGap,
                         std::numeric_limits<float>::max(), "%.2f", ImGuiSliderFlags_AlwaysClamp);

        editor_style::setNextFieldProvenance(cameraMark(camera.moveSpeed != initial.moveSpeed,
                                                        CameraProvenanceField::Speed,
                                                        "Fly speed (world/s)"));
        beginFieldRow("Fly speed (world/s)");
        ImGui::DragFloat("##flySpeed", &camera.moveSpeed, 0.1f, 0.5f, 50.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);

        editor_style::endFields();
    }
    if (const auto* loaded = context.session.loadedScene()) {
        const auto saved = context.session.authoredSceneCamera();
        const auto key = std::format("node:{}", loaded->document.camera);
        const auto mark = [&](std::string_view field) -> std::optional<ProvenanceMark> {
            if (!context.attribution)
                return std::nullopt;
            const auto fieldKey = key + "/" + std::string(field);
            if (!context.attribution->has(fieldKey))
                return std::nullopt;
            return sessionAppliedProvenance(context.attribution->client(fieldKey));
        };
        ImGui::SeparatorText("Saved Scene Camera");
        if (editor_style::beginPropertyGrid("savedSceneCamera")) {
            valueRow("Position (world)",
                     std::format("{:.3f}, {:.3f}, {:.3f}", saved.position.x, saved.position.y,
                                 saved.position.z),
                     mark("position"));
            valueRow("Yaw (deg)", std::format("{:.1f}", glm::degrees(saved.yaw)), mark("yaw"));
            valueRow("Pitch (deg)", std::format("{:.1f}", glm::degrees(saved.pitch)),
                     mark("pitch"));
            editor_style::endFields();
        }
    }
}

} // namespace lmx::app
