//----------------------------------------------------------------------------------------------------------------------
/// @file EditorSelection.h
/// @brief Declares the Scene panel's editor-local selection value, resolver, transitions, and rows.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneInstantiate.h"
#include "Render/Passes/Visibility/Visibility.h"
#include "Scenes/SceneLibrary.h"

#include <cstddef>
#include <cstdint>
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
    DirectionalLight, ///< One of `Scene::lights`, named by `EditorSelection::index`.
    LocalLight,       ///< One generational local light, named by `lightId`.
    Object,           ///< One of `Scene::objects`, named by `EditorSelection::index`.
    Group,            ///< Document or imported source node with no direct leaf subject.
    Environment,      ///< The document's scene look.
};

/// Ordered Rendering panel topics; independent from the selected scene subject.
enum class RenderingCategory {
    Overview,       ///< Summary and navigation for rendering settings.
    Reconstruction, ///< Temporal reconstruction controls and diagnostics.
    Resolution,     ///< Render scale and dynamic-resolution feedback.
    Visibility,     ///< Frustum classification and visible-instance counts.
    Occlusion,      ///< Previous-frame occlusion controls and validation.
    Submission,     ///< Draw submission and batching counts.
    Lighting,       ///< Local-light modes, cluster diagnostics and rig controls.
    Display,        ///< Display and output-domain information.
    SceneTables,    ///< Scene-table allocation and update information.
    Count,          ///< Exclusive upper bound; not a selectable category.
};

/// Shared object status: Disabled for authored off, Culled for frustum/HZB rejection,
/// otherwise Visible or Bypassed. A missing frame remains a caller-owned waiting state.
std::string_view visibilityStatusLabel(render::VisibilityState state,
                                       render::VisibilityReason reason);

/// Stable visible topic label; Overview is `Rendering`, invalid values return `Unavailable`.
std::string_view renderingCategoryLabel(RenderingCategory category);

/// The editor's single selected subject: a value, not an owning or borrowed pointer, so it survives
/// scene switches and vector mutation without dangling. `index` is meaningful only for
/// `DirectionalLight` and `Object`; other subjects ignore it. Editor-local navigation
/// state -- never serialized, never passed to Render or the RHI (spec section 5).
struct EditorSelection {
    scenes::SceneId sceneId;                     ///< The scene the selection was made against.
    EditorSubject subject = EditorSubject::None; ///< What is selected.
    size_t index = 0;                            ///< DirectionalLight/Object row index.
    engine::LightId lightId{};              ///< Complete local-light identity; otherwise unused.
    uint32_t node = engine::kGeneratedNode; ///< Document node, or sentinel for generated/look.
    uint32_t importedNode = engine::kGeneratedNode; ///< Imported binding, distinct from `node`.
};

/// Compares `current` against `activeScene`/`scene` and returns the value the caller should store:
/// `current` unchanged if it still resolves, or a healed `{activeScene, EditorSubject::None, 0}` if
/// `current.sceneId` no longer names the active scene, or its `DirectionalLight` or
/// `Object` index is out of range, or its complete `LocalLight` identity is stale.
/// `Camera` and `None` never fail range validation. Call
/// this on every use before drawing the Inspector; it never mutates `scene`, and the caller stores
/// the returned value rather than caching the argument.
EditorSelection resolveSelection(const EditorSelection& current, scenes::SceneId activeScene,
                                 const engine::Scene& scene);

/// The selection stored at startup, or immediately after activating `sceneId`: every scene provides
/// a `Camera`, so it is always resolvable without consulting scene contents.
EditorSelection initialSelection(scenes::SceneId sceneId);

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
SceneSwitchOutcome sceneSwitchOutcome(bool switchSucceeded, scenes::SceneId activeScene,
                                      scenes::SceneId requestedScene,
                                      const EditorSelection& currentSelection,
                                      const std::string& currentFilter);

/// Presentational grouping the Scene panel draws as separate headers. Does not alter scene ordering
/// or introduce a tree into Scene (spec section 6).
enum class EditorSelectionGroup {
    DirectionalLights, ///< The three fixed lights, in index order.
    LocalLights,       ///< Live local-light identities in stable row order.
    Objects,           ///< `Scene::objects`, in scene order.
};

/// One row the Scene panel can display or select. Carries just enough to build an `EditorSelection`
/// and a label; it borrows nothing from `Scene`, so it outlives the call that built it.
struct EditorSelectionRow {
    EditorSubject subject = EditorSubject::None; ///< What selecting this row selects.
    size_t index = 0;                            ///< DirectionalLight/Object index.
    std::string displayLabel;                    ///< Text the panel draws for the row.
    EditorSelectionGroup group = EditorSelectionGroup::Objects; ///< Which header it draws under.
    /// Full source-qualified name for search, hover and copy; empty when the short label suffices.
    std::string detailLabel;
    std::string sourceGroup;   ///< Optional imported source navigation group; no parent transform.
    engine::LightId lightId{}; ///< Complete identity for LocalLight rows.
};

/// Stable point/spot label carrying its row slot, or Unavailable light for stale identities.
std::string sceneLocalLightLabel(const engine::Scene& scene, engine::LightId id);

/// Authored name with a scene-local object suffix for duplicate names; unnamed objects always get
/// a deterministic `Unnamed object [N]` label. An out-of-range index returns `Unavailable object`.
std::string sceneObjectLabel(const engine::Scene& scene, size_t index);

/// Whether a resolved selected subject is hidden by the current case-insensitive name filter.
/// Filtering never clears the selection; Inspector can keep showing it with an explicit notice.
/// Camera is outside the Hierarchy and is never hidden by its search.
bool selectionHiddenByFilter(const engine::Scene& scene, const EditorSelection& selection,
                             std::string_view filter);

/// Builds scene-only rows in fixed order: directional lights, live local-light identities, then
/// scene objects. Disabled local lights keep their rows. Filters visible/full source names
/// case-insensitively. Camera remains selectable outside the Hierarchy but emits no
/// rows here. An empty filter keeps every row; unmatched filters return an empty vector. Duplicate
/// names keep distinct identities. Never mutates `scene`.
std::vector<EditorSelectionRow> buildSceneSelectionRows(const engine::Scene& scene,
                                                        std::string_view filter);

/// Counts matching and total selectable scene subjects, independent of collapsed groups.
struct HierarchyCount {
    size_t shown = 0; ///< Rows matching the current filter.
    size_t total = 0; ///< The same row population with an empty filter, including disabled lights.
};

/// Counts the filtered rows and the unfiltered scene-only population under the same row contract.
HierarchyCount hierarchyCount(const engine::Scene& scene, std::string_view filter);

/// The unfiltered row count (directional lights, local lights including disabled ones, objects),
/// counted without building rows; equals `buildSceneSelectionRows(scene, "").size()`.
size_t hierarchyTotal(const engine::Scene& scene);

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
