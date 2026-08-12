//----------------------------------------------------------------------------------------------------------------------
/// @file EditorSelection.h
/// @brief Declares the Scene panel's editor-local selection value, resolver, transitions, and rows.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Engine/Scene.h"
#include "Engine/SceneLibrary.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::app {

/// What the Scene panel's single selection currently names.
enum class EditorSubject {
    None,             ///< Nothing selected -- also the healed value for a stale reference.
    Camera,           ///< The scene's one editor camera.
    Rendering,        ///< The rendering-settings context; carries no scene data of its own.
    DirectionalLight, ///< One of `Scene::lights`, named by `EditorSelection::index`.
    Object,           ///< One of `Scene::objects`, named by `EditorSelection::index`.
};

/// The editor's single selected subject: a value, not an owning or borrowed pointer, so it survives
/// scene switches and vector mutation without dangling. `index` is meaningful only for
/// `DirectionalLight` and `Object`; other subjects ignore it. Editor-local navigation state --
/// never serialized, never passed to Render or the RHI (spec section 5).
struct EditorSelection {
    engine::SceneId sceneId;                     ///< The scene the selection was made against.
    EditorSubject subject = EditorSubject::None; ///< What is selected.
    size_t index = 0;                            ///< Row index for DirectionalLight/Object only.
};

/// Compares `current` against `activeScene`/`scene` and returns the value the caller should store:
/// `current` unchanged if it still resolves, or a healed `{activeScene, EditorSubject::None, 0}` if
/// `current.sceneId` no longer names the active scene, or its `DirectionalLight`/`Object` index is
/// out of range for `scene`. `Camera`, `Rendering`, and `None` never fail range validation. Call
/// this on every use before drawing the Inspector; it never mutates `scene`, and the caller stores
/// the returned value rather than caching the argument.
EditorSelection resolveSelection(const EditorSelection& current, engine::SceneId activeScene,
                                 const engine::Scene& scene);

/// The selection stored at startup, or immediately after activating `sceneId`: every scene provides
/// a `Camera`, so it is always resolvable without consulting scene contents.
EditorSelection initialSelection(engine::SceneId sceneId);

/// Selection and Scene-panel filter to store together, since a successful scene switch changes both
/// at once (spec section 5).
struct SceneSwitchOutcome {
    EditorSelection selection; ///< The selection to store.
    std::string filter;        ///< The Scene-panel filter text to store.
};

/// The selection and filter to store after requesting a switch to `requestedScene`, given whether
/// the switch actually applied. A successful switch (`switchSucceeded`) selects `requestedScene`'s
/// `Camera` and clears the filter. A failed switch returns `currentSelection` and `currentFilter`
/// unchanged, which is the spec's "failed scene switch retains selection and filter exactly" --
/// callers do not need a second function to express the failure path. This function does not decide
/// whether a same-scene reselection counts as a switch; a caller that already knows the requested
/// scene is the active one should not call it at all (spec: "changes nothing").
SceneSwitchOutcome sceneSwitchOutcome(bool switchSucceeded, engine::SceneId requestedScene,
                                      const EditorSelection& currentSelection,
                                      const std::string& currentFilter);

/// Presentational grouping the Scene panel draws as separate headers. Does not alter scene ordering
/// or introduce a tree into Engine (spec section 6).
enum class EditorSelectionGroup {
    Workspace,         ///< Editor Camera, Rendering.
    DirectionalLights, ///< The three fixed lights, in index order.
    Objects,           ///< `Scene::objects`, in scene order.
};

/// One row the Scene panel can display or select. Carries just enough to build an `EditorSelection`
/// and a label; it borrows nothing from `Scene`, so it outlives the call that built it.
struct EditorSelectionRow {
    EditorSubject subject = EditorSubject::None; ///< What selecting this row selects.
    size_t index = 0;                            ///< DirectionalLight/Object index; else 0.
    std::string displayLabel;                    ///< Text the panel draws for the row.
    EditorSelectionGroup group = EditorSelectionGroup::Workspace; ///< Which header it draws under.
};

/// Builds the Scene panel's rows for `scene` in the spec's fixed order -- Editor Camera, Rendering,
/// the three directional lights in index order, then `scene.objects` in scene order -- then drops
/// rows whose `displayLabel` does not contain `filter` case-insensitively. An empty `filter` keeps
/// every row; a filter matching nothing returns an empty vector rather than an error state.
/// Duplicate object names still produce distinct rows: subject kind plus index, not label text,
/// identifies a row. Never mutates `scene`.
std::vector<EditorSelectionRow> buildSceneSelectionRows(const engine::Scene& scene,
                                                        std::string_view filter);

/// The row keyboard Down should select, continuing from `current` over `rows` (already filtered, in
/// display order). When `current` names no visible row -- filtered out, or nothing selected yet --
/// navigation starts at the first visible row rather than searching for a lost position (spec
/// section 6). Clamps at the last row rather than wrapping past it. Returns `std::nullopt` only
/// when `rows` is empty.
std::optional<EditorSelectionRow> nextVisibleRow(std::span<const EditorSelectionRow> rows,
                                                 const EditorSelection& current);

/// The row keyboard Up should select, continuing from `current` over `rows`, with the same
/// filtered-out start rule as `nextVisibleRow`. Clamps at the first row rather than wrapping past
/// it. Returns `std::nullopt` only when `rows` is empty.
std::optional<EditorSelectionRow> previousVisibleRow(std::span<const EditorSelectionRow> rows,
                                                     const EditorSelection& current);

} // namespace lmx::app
