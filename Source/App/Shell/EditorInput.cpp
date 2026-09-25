//----------------------------------------------------------------------------------------------------------------------
/// @file EditorInput.cpp
/// @brief Implements viewport camera input and relative-mouse capture.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorShell.h"

#include "App/Model/Capture/EditorShortcuts.h"
#include "App/Model/Scene/SelectionBounds.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <imgui.h>

namespace lmx::app {

namespace {

// Tuned so a roughly screen-wide drag turns the camera 180 degrees.
constexpr float kLookRadiansPerPixel = 0.0025f;

} // namespace

//======================================================================================================================
void EditorShell::resetCamera() {
    m_session.camera() = engine::cameraFromScene(m_session.scene().initialCamera);
    m_settings.followCameraTrack = false;
    requestCameraCut(m_temporalState);
}

//======================================================================================================================
void EditorShell::frameSelected(const render::Renderer& renderer) {
    const auto bounds = selectedObjectBounds(m_session.scene(), m_selection);
    if (bounds && frameSelection(m_session.camera(), *bounds,
                                 static_cast<float>(renderer.width()) / renderer.height())) {
        m_settings.followCameraTrack = false;
        requestCameraCut(m_temporalState);
    }
}

//======================================================================================================================
void EditorShell::updateEditorShortcuts(const render::Renderer& renderer) {
    const auto& io = ImGui::GetIO();
    if (io.AppFocusLost || io.KeyCtrl || io.KeySuper || io.KeyAlt)
        return;
    const ShortcutContext context{
        .textInput = io.WantTextInput || ImGui::IsAnyItemActive(),
        .cameraLook = m_looking || ImGui::IsMouseDown(ImGuiMouseButton_Right),
        .popupOpen =
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
        .captureAvailable = m_actions.captureAvailable() &&
                            m_actions.captureResult().status != ActionStatus::Pending,
        .hasSelection = selectedObjectBounds(m_session.scene(), m_selection).has_value()};
    if (!m_measurement.active() && ImGui::IsKeyPressed(ImGuiKey_F, false) &&
        shortcutAllowed(EditorShortcut::FrameSelected, context))
        frameSelected(renderer);
    if (!m_measurement.active() && ImGui::IsKeyPressed(ImGuiKey_Home, false) &&
        shortcutAllowed(EditorShortcut::ResetCamera, context))
        resetCamera();
    if (ImGui::IsKeyPressed(ImGuiKey_C, false) && shortcutAllowed(EditorShortcut::Capture, context))
        m_actions.requestCapture();
}

//======================================================================================================================
void EditorShell::updateCameraInput(float deltaSeconds) {
    if (m_measurement.active()) {
        endMouseLook();
        return;
    }
    // Drain SDL motion every frame so pre-look cursor travel cannot accumulate into a jump.
    float relativeX = 0.0f;
    float relativeY = 0.0f;
    SDL_GetRelativeMouseState(&relativeX, &relativeY);

    if (ImGui::GetIO().WantTextInput ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        endMouseLook();
        return;
    }
    if (!m_looking) {
        if (m_viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_looking = true;
            SDL_SetWindowRelativeMouseMode(m_window, true);
        }
        // Ignore the entry-frame delta because it predates relative mode.
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        endMouseLook();
        return;
    }

    // Screen Y grows downward while camera pitch grows upward.
    m_session.camera().look(relativeX * kLookRadiansPerPixel, -relativeY * kLookRadiansPerPixel);

    const bool* keys = SDL_GetKeyboardState(nullptr);
    glm::vec3 move{0.0f};
    move.z += keys[SDL_SCANCODE_W] ? 1.0f : 0.0f;
    move.z -= keys[SDL_SCANCODE_S] ? 1.0f : 0.0f;
    move.x += keys[SDL_SCANCODE_D] ? 1.0f : 0.0f;
    move.x -= keys[SDL_SCANCODE_A] ? 1.0f : 0.0f;
    move.y += keys[SDL_SCANCODE_E] ? 1.0f : 0.0f;
    move.y -= keys[SDL_SCANCODE_Q] ? 1.0f : 0.0f;
    if (move != glm::vec3{0.0f}) {
        // Normalize diagonal movement to preserve speed.
        m_session.camera().move(glm::normalize(move) *
                                (m_session.camera().moveSpeed * deltaSeconds));
    }
}

//======================================================================================================================
void EditorShell::endMouseLook() {
    if (!m_looking) {
        return;
    }
    m_looking = false;
    SDL_SetWindowRelativeMouseMode(m_window, false);
    // Drop whatever relative motion SDL accumulated up to this point; carrying it into the next
    // look would turn the camera by everything the cursor did in between.
    SDL_GetRelativeMouseState(nullptr, nullptr);
}

} // namespace lmx::app
