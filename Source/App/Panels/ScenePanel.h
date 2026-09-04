//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePanel.h
/// @brief Declares the Scene panel's drawing entry point and the state it edits.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/EditorSelection.h"
#include "Engine/Scene.h"
#include "Engine/SceneLibrary.h"

#include <optional>
#include <string>

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The shell's dock builder places the window under
/// exactly this name, so both sides read it from here.
inline constexpr const char* kScenePanelWindowName = "Scene";

/// The editor state the Scene panel draws and edits, borrowed for the duration of one draw call.
///
/// Every member is a reference to storage the shell owns, so the panel edits the shell's values in
/// place and holds nothing past the call that resolved them, matching `InspectorPanelContext`'s
/// pattern.
struct ScenePanelContext {
    const engine::SceneLibrary& library; ///< The scene catalog the top selector draws from.
    engine::SceneId activeSceneId;       ///< The scene currently loaded and rendered.
    const engine::Scene& activeScene;    ///< The active scene, whose subjects the rows list.
    EditorSelection& selection;          ///< The single selected subject; edited in place.
    std::string& filter;                 ///< The case-insensitive display-name filter text.
};

/// Draws the Scene panel, in the spec's fixed order: the scene-catalog selector (unavailable
/// entries disabled with their availability hints shown inline, since a disabled entry cannot be
/// hovered reliably), the filter text box, then the Workspace / Directional Lights / Objects
/// groups of selectable rows built by `buildSceneSelectionRows`. `open` follows the window's close
/// button, exactly as `ImGui::Begin` writes it.
///
/// Mouse click and Up/Down keyboard navigation (while the Scene window is focused) update
/// `context.selection` over the rows currently visible under `context.filter`; grouping and
/// filtering are presentational only and never mutate `context.activeScene`.
///
/// Returns the scene the user picked this frame, or `std::nullopt` when nothing was picked.
/// Switching scenes drains the GPU because in-flight frames may still reference the current
/// scene's meshes and textures, so the panel reports the choice and the shell applies it through
/// its own scene-switch boundary rather than loading a scene from inside a widget.
std::optional<engine::SceneId> drawScenePanel(bool& open, const ScenePanelContext& context);

} // namespace lmx::app
