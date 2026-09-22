//----------------------------------------------------------------------------------------------------------------------
/// @file EditorInput.cpp
/// @brief Implements viewport camera input and relative-mouse capture.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorShell.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <imgui.h>

namespace lmx::app {

namespace {

// Tuned so a roughly screen-wide drag turns the camera 180 degrees.
constexpr float kLookRadiansPerPixel = 0.0025f;

} // namespace

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

    if (ImGui::GetIO().WantTextInput) {
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
