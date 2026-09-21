//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePanel.h
/// @brief Declares the subject Hierarchy and the File menu's scene loading entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/EditorSelection.h"
#include "App/Model/SceneLoadState.h"
#include "App/Model/SceneSession.h"
#include "App/Model/TemporalEditorState.h"
#include "App/Model/VisibilityDisplay.h"
#include "Engine/Catalog/SceneLibrary.h"
#include "Engine/Scene/Scene.h"

#include <optional>
#include <string>

namespace lmx::app {

/// Visible Hierarchy title with the original Scene window ID retained for saved docking. The
/// pinned ImGui hashes a ### suffix exactly as the suffix alone, including window settings.
inline constexpr const char* kScenePanelWindowName = "Hierarchy###Scene";

/// Borrowed state for the compact subject Hierarchy; selection and search remain shell-owned.
struct ScenePanelContext {
    engine::SceneId activeSceneId;    ///< Catalog identity scoping this scene's tree state.
    const engine::Scene& activeScene; ///< Flat scene whose subjects are grouped for navigation.
    SceneSession& session; ///< Applies light edits before prepareFrame and retains reset defaults.
    TemporalEditorState& temporalState; ///< Invalidates history when light enablement changes.
    EditorSelection& selection;         ///< Selected leaf, edited in place.
    std::string& filter; ///< Case-insensitive short/full name filter, edited in place.
    const VisibilityDisplay& visibilityDisplay;       ///< Identity mapping for the displayed image.
    const render::VisibilityStatus& visibilityStatus; ///< Last declared frame.
    uint64_t sceneGeneration = 0;                     ///< Active scene identity revision.
};

/// Borrowed scene-loading state for File > Open Scene. Loading remains a shell frame-boundary
/// action.
struct SceneMenuContext {
    const engine::SceneLibrary& library; ///< Catalog entries and availability explanations.
    engine::SceneId activeSceneId;       ///< The currently rendered scene.
    const SceneLoadState& loading;       ///< Persistent failure retained for an explicit Retry.
};

/// Draws File's Open Scene submenu and returns a newly selected or retried catalog entry. Keeps
/// the submenu open to present Loading before the shell consumes the request on the next frame.
/// Call inside an open File menu; failures and unavailable-entry reasons remain visible here.
std::optional<engine::SceneId> drawSceneMenu(const SceneMenuContext& context);

/// Draws an indented Workspace and active-scene tree over a flat scene, with a fixed search/count
/// header. Groups collapse independently; keyboard Up/Down visits only drawn leaves. Filtering
/// never clears selection. `open` follows the native ImGui window close button.
void drawScenePanel(bool& open, const ScenePanelContext& context);

} // namespace lmx::app
