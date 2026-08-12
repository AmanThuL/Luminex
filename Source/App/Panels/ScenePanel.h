//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePanel.h
/// @brief Declares the Scene panel's drawing entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Engine/SceneLibrary.h"

#include <optional>

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The shell's dock builder places the window under
/// exactly this name, so both sides read it from here.
inline constexpr const char* kScenePanelWindowName = "Scene";

/// Draws the Scene panel: the scene-catalog selector, with unavailable entries disabled and their
/// availability hints shown inline because a disabled entry cannot be hovered reliably. `open`
/// follows the window's close button, exactly as `ImGui::Begin` writes it.
///
/// Returns the scene the user picked this frame, or `std::nullopt` when nothing was picked.
/// Switching scenes drains the GPU because in-flight frames may still reference the current
/// scene's meshes and textures, so the panel reports the choice and the shell applies it through
/// its own scene-switch boundary rather than loading a scene from inside a widget.
std::optional<engine::SceneId> drawScenePanel(bool& open, const engine::SceneLibrary& library,
                                              engine::SceneId activeSceneId);

} // namespace lmx::app
