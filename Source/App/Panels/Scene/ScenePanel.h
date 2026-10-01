//----------------------------------------------------------------------------------------------------------------------
/// @file ScenePanel.h
/// @brief Declares the subject Hierarchy and its selection navigation.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/Rendering/Visibility/VisibilityDisplay.h"
#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Scene/SceneTreeState.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneInstantiate.h"
#include "Scenes/SceneLibrary.h"

#include <optional>
#include <string>

namespace lmx::app {

class SceneSession;
struct SessionProposal;

/// Visible Hierarchy title with the original Scene window ID retained for saved docking. The
/// pinned ImGui hashes a ### suffix exactly as the suffix alone, including window settings.
inline constexpr const char* kScenePanelWindowName = "Hierarchy###Scene";

/// Borrowed state for the compact subject Hierarchy; selection and search remain shell-owned.
struct ScenePanelContext {
    scenes::SceneId activeSceneId;    ///< Catalog identity scoping this scene's tree state.
    const engine::Scene& activeScene; ///< Flat scene whose subjects are grouped for navigation.
    EditorSelection& selection;       ///< Selected leaf, edited in place.
    std::string& filter;              ///< Case-insensitive short/full name filter, edited in place.
    bool& frameSelectionRequested; ///< Set by the context menu for the shell's shared frame action.
    const VisibilityDisplay& visibilityDisplay;       ///< Identity mapping for the displayed image.
    const render::VisibilityStatus& visibilityStatus; ///< Last declared frame.
    uint64_t sceneGeneration = 0;                     ///< Active scene identity revision.
    const engine::LoadedScene* loadedScene = nullptr; ///< Document and source-node bindings.
    const SceneSession* session = nullptr;            ///< Current own/effective enabled state.
    bool dirty = false;                               ///< Canonical document differs from load.
    const SessionProposal* proposal = nullptr;        ///< Pending external file proposal, if any.
    SceneTreeState& treeState;                        ///< Collapse choices and the cached tree.
};

/// Draws an indented active-scene tree over a flat scene, with a fixed search/count
/// header. Groups collapse independently; keyboard Up/Down visits only drawn leaves. Filtering
/// never clears selection. `open` follows the native ImGui window close button. When the document
/// tree is drawn, returns its final selected-row visibility for the Inspector in the same frame.
std::optional<bool> drawScenePanel(bool& open, const ScenePanelContext& context);

} // namespace lmx::app
