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

//======================================================================================================================
// Keyboard events reach Dear ImGui from every platform window, so a detached tool window's own
// keys (the node editor's F, for instance) must not also drive the viewport's commands. Only the
// Render Graph and Performance windows never merge; an undocked editor panel still owns them.
bool detachedSurfaceFocused() {
    for (const ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports) {
        if ((viewport->Flags & ImGuiViewportFlags_IsFocused) &&
            (viewport->Flags & ImGuiViewportFlags_NoAutoMerge))
            return true;
    }
    return false;
}

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
void EditorShell::consumeFrameSelection(const render::Renderer& renderer) {
    if (!m_frameSelectionRequested)
        return;
    m_frameSelectionRequested = false;
    frameSelected(renderer);
}

//======================================================================================================================
void EditorShell::updateEditorShortcuts(const render::Renderer& renderer) {
    const auto& io = ImGui::GetIO();
    // ImGui maps physical Command to logical Ctrl under macOS keyboard behavior.
    const bool command = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeySuper;
    const bool control = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
    if (io.AppFocusLost || control || io.KeyAlt)
        return;
    const ShortcutContext context{
        .textInput = io.WantTextInput || ImGui::IsAnyItemActive(),
        .cameraLook = m_looking || ImGui::IsMouseDown(ImGuiMouseButton_Right),
        .popupOpen =
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
        .otherSurfaceFocused = detachedSurfaceFocused(),
        .hasSelection = selectedObjectBounds(m_session.scene(), m_selection).has_value()};
    if (command) {
        if (shortcutAllowed(EditorShortcut::Document, context)) {
            if (ImGui::IsKeyPressed(ImGuiKey_S, false))
                runMenuCommand(io.KeyShift ? MenuCommand::SaveAs : MenuCommand::Save);
            else if (ImGui::IsKeyPressed(ImGuiKey_O, false))
                runMenuCommand(MenuCommand::Open);
            else if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
                runMenuCommand(MenuCommand::Quit);
        }
        return;
    }
    if (m_documentWorkflow.step() != WorkflowStep::Idle)
        return;
    if (!m_measurement.active() && ImGui::IsKeyPressed(ImGuiKey_F, false) &&
        shortcutAllowed(EditorShortcut::FrameSelected, context)) {
        runMenuCommand(MenuCommand::FrameSelected);
        consumeFrameSelection(renderer);
    }
    if (!m_measurement.active() && ImGui::IsKeyPressed(ImGuiKey_Home, false) &&
        shortcutAllowed(EditorShortcut::ResetCamera, context))
        runMenuCommand(MenuCommand::ResetCamera);
    if (ImGui::IsKeyPressed(ImGuiKey_C, false) &&
        shortcutAllowed(EditorShortcut::Capture, context)) {
        runMenuCommand(MenuCommand::Capture);
    }
}

//======================================================================================================================
void EditorShell::updateCameraInput(float deltaSeconds) {
    if (m_measurement.active() || m_documentWorkflow.step() != WorkflowStep::Idle) {
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
