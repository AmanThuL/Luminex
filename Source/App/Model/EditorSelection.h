//----------------------------------------------------------------------------------------------------------------------
/// @file EditorSelection.h
/// @brief Declares the Scene panel's editor-local selection value, resolver, transitions, and rows.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Scene/Scene.h"
#include "Scene/SceneLibrary.h"

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
    Rendering,        ///< A rendering category, named by `EditorSelection::index`.
    DirectionalLight, ///< One of `Scene::lights`, named by `EditorSelection::index`.
    Object,           ///< One of `Scene::objects`, named by `EditorSelection::index`.
};

/// Inspector topics under Rendering; Overview preserves the root selection at index zero.
enum class RenderingCategory {
    Overview,       ///< Summary and navigation for rendering settings.
    Reconstruction, ///< Temporal reconstruction controls and diagnostics.
    Resolution,     ///< Render scale and dynamic-resolution feedback.
    Visibility,     ///< Frustum classification and visible-instance counts.
    Occlusion,      ///< Previous-frame occlusion controls and validation.
    Submission,     ///< Draw submission and batching counts.
    Exposure,       ///< Exposure controls and adaptation feedback.
    Bloom,          ///< Bloom controls.
    Shadows,        ///< Shadow controls and status.
    Display,        ///< Display and output-domain information.
    SceneTables,    ///< Scene-table allocation and update information.
    Count,          ///< Exclusive upper bound; not a selectable category.
};

/// Stable visible topic label; Overview is `Rendering`, invalid values return `Unavailable`.
std::string_view renderingCategoryLabel(RenderingCategory category);

/// The editor's single selected subject: a value, not an owning or borrowed pointer, so it survives
/// scene switches and vector mutation without dangling. `index` is meaningful only for
/// `Rendering`, `DirectionalLight` and `Object`; other subjects ignore it. Editor-local navigation
/// state -- never serialized, never passed to Render or the RHI (spec section 5).
struct EditorSelection {
    scene::SceneId sceneId;                      ///< The scene the selection was made against.
    EditorSubject subject = EditorSubject::None; ///< What is selected.
    size_t index = 0;                            ///< Category or DirectionalLight/Object row index.
};

/// Compares `current` against `activeScene`/`scene` and returns the value the caller should store:
/// `current` unchanged if it still resolves, or a healed `{activeScene, EditorSubject::None, 0}` if
/// `current.sceneId` no longer names the active scene, or its category, `DirectionalLight` or
/// `Object` index is out of range. `Camera` and `None` never fail range validation. Call
/// this on every use before drawing the Inspector; it never mutates `scene`, and the caller stores
/// the returned value rather than caching the argument.
EditorSelection resolveSelection(const EditorSelection& current, scene::SceneId activeScene,
                                 const scene::Scene& scene);

/// The selection stored at startup, or immediately after activating `sceneId`: every scene provides
/// a `Camera`, so it is always resolvable without consulting scene contents.
EditorSelection initialSelection(scene::SceneId sceneId);

/// Selection and Scene-panel filter to store together, since a successful scene switch changes both
/// at once (spec section 5).
struct SceneSwitchOutcome {
    EditorSelection selection; ///< The selection to store.
    std::string filter;        ///< The Scene-panel filter text to store.
};

/// The selection and filter to store after requesting a switch from `activeScene` to
/// `requestedScene`, given whether the switch actually applied. Selecting the already-active scene
/// changes nothing: `requestedScene == activeScene` returns `currentSelection` and `currentFilter`
/// unchanged regardless of `switchSucceeded`, matching the spec's "selecting the already-active
/// catalog scene changes nothing" -- callers do not need to guard this themselves before calling
/// (a combo box that fires on re-clicking the active row is safe to wire directly to this
/// function). Otherwise, a successful switch (`switchSucceeded`) selects `requestedScene`'s
/// `Camera` and clears the filter; a failed switch returns `currentSelection` and `currentFilter`
/// unchanged, which is the spec's "failed scene switch retains selection and filter exactly" --
/// callers do not need a second function to express the failure path.
SceneSwitchOutcome sceneSwitchOutcome(bool switchSucceeded, scene::SceneId activeScene,
                                      scene::SceneId requestedScene,
                                      const EditorSelection& currentSelection,
                                      const std::string& currentFilter);

/// Presentational grouping the Scene panel draws as separate headers. Does not alter scene ordering
/// or introduce a tree into Scene (spec section 6).
enum class EditorSelectionGroup {
    Workspace,         ///< Editor Camera and Rendering categories.
    DirectionalLights, ///< The three fixed lights, in index order.
    Objects,           ///< `Scene::objects`, in scene order.
};

/// One row the Scene panel can display or select. Carries just enough to build an `EditorSelection`
/// and a label; it borrows nothing from `Scene`, so it outlives the call that built it.
struct EditorSelectionRow {
    EditorSubject subject = EditorSubject::None; ///< What selecting this row selects.
    size_t index = 0;         ///< Rendering category or DirectionalLight/Object index.
    std::string displayLabel; ///< Text the panel draws for the row.
    EditorSelectionGroup group = EditorSelectionGroup::Workspace; ///< Which header it draws under.
    /// Full source-qualified name for search, hover and copy; empty when the short label suffices.
    std::string detailLabel;
    std::string sourceGroup; ///< Optional imported source navigation group; no parent transform.
};

/// Authored name with a scene-local object suffix for duplicate names; unnamed objects always get
/// a deterministic `Unnamed object [N]` label. An out-of-range index returns `Unavailable object`.
std::string sceneObjectLabel(const scene::Scene& scene, size_t index);

/// Whether a resolved selected subject is hidden by the current case-insensitive name filter.
/// Filtering never clears the selection; Inspector can keep showing it with an explicit notice.
bool selectionHiddenByFilter(const scene::Scene& scene, const EditorSelection& selection,
                             std::string_view filter);

/// Builds rows in fixed order: Editor Camera, Rendering categories in enum order, three lights,
/// then scene objects. Filters visible/full source names case-insensitively; matching `Rendering`
/// retains every category. Unmatched navigation parents are not included in selectable rows.
/// An empty `filter` keeps every row; a filter matching nothing returns an empty vector rather than
/// an error state. Duplicate object names still produce distinct rows: subject kind plus index, not
/// label text, identifies a row. Never mutates `scene`.
std::vector<EditorSelectionRow> buildSceneSelectionRows(const scene::Scene& scene,
                                                        std::string_view filter);

/// Objects grouped for navigation in first-source encounter order. An empty source label means
/// leaves can draw directly under Objects. Rows preserve their scene-local selection indices.
struct EditorObjectGroup {
    std::string sourceName;               ///< Authored source label, or empty for ungrouped leaves.
    std::vector<EditorSelectionRow> rows; ///< Object leaves in original encounter order.
};

/// Groups only object rows, merging repeated source groups without merging their subjects. Group
/// order follows first encounter and is deterministic; filtered rows retain their source group.
std::vector<EditorObjectGroup> groupSceneObjectRows(std::span<const EditorSelectionRow> rows);

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
