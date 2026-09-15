//----------------------------------------------------------------------------------------------------------------------
/// @file WorkspaceModel.h
/// @brief Declares panel visibility and the workspace persistence schema decision.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lmx::app {

/// Editor text/control scale used when no supported preference was saved.
inline constexpr uint32_t kDefaultUiScalePercent = 100;
/// Ordered menu and shortcut steps; intermediate persisted percentages remain valid.
inline constexpr std::array<uint32_t, 7> kUiScalePresets{75, 80, 90, 100, 110, 125, 150};

/// Keeps percentages in the inclusive preset bounds; unsupported values return the default.
uint32_t normalizedUiScalePercent(uint32_t percent);

/// Selects the next strictly greater/lesser preset, saturating at the endpoints. Unsupported
/// input is normalized to the default before stepping; zoomIn selects the greater direction.
uint32_t stepUiScalePercent(uint32_t percent, bool zoomIn);

/// One of the editor's six independently visible top-level panels (spec section 3).
enum class EditorPanel {
    Scene,       ///< Active-scene choice, filtering, grouped subjects, single selection.
    Viewport,    ///< Rendered image, camera input, compact rendering toolbar.
    Inspector,   ///< Properties of the selected subject only.
    Performance, ///< Rolling frame-interval and GPU-pass observations.
    RenderGraph, ///< Coherent published compiled-frame record and dump.
    Console,     ///< Bounded project log viewer.
};

/// One app-owned visibility value per panel. Performance and Render Graph start closed; the
/// docked panels start open. A Window-menu checkbox and a panel's window close button both read and
/// write the same storage through `isVisible`/`setVisible`, so close, reopen, and menu toggle
/// cannot disagree.
struct WorkspaceVisibility {
    bool scene = true;        ///< `EditorPanel::Scene`.
    bool viewport = true;     ///< `EditorPanel::Viewport`.
    bool inspector = true;    ///< `EditorPanel::Inspector`.
    bool performance = false; ///< `EditorPanel::Performance`; a detached diagnostic window.
    bool console = true;      ///< `EditorPanel::Console`; the bottom dock's only default panel.
    bool renderGraph = false; ///< `EditorPanel::RenderGraph`.

    /// Reads the stored value for `panel`.
    bool isVisible(EditorPanel panel) const;

    /// Writes the stored value for `panel`. Whether the caller is the Window menu or the panel's
    /// own close button, this is the single storage both act on.
    void setVisible(EditorPanel panel, bool visible);
};

/// The current workspace persistence schema (spec section 4). Changes only when a persisted
/// workspace contract or the required default topology changes -- never for cosmetic spacing or
/// labels.
///
/// Version 3 detaches Performance as well as Render Graph, leaving Console alone in the bottom
/// dock. A version 2 workspace rebuilds its default layout and visibility once, preserving its
/// valid UI scale. Older, invalid and future schemas reset every preference to defaults.
/// Visibility and UI scale keys remain optional within the current schema.
inline constexpr uint32_t kWorkspaceSchemaVersion = 3;

/// Whether a settings-section body named a schema version, and if so, whether it was a parseable
/// non-negative integer.
enum class WorkspaceSchemaState {
    Absent,      ///< The section had no `Schema` key.
    Unparseable, ///< A `Schema` key was present but its value did not parse as an integer.
    Present,     ///< A `Schema` key parsed cleanly; the value is in `schemaVersion`.
};

/// The result of parsing one Luminex workspace settings-section body.
struct ParsedWorkspaceSettings {
    /// See `WorkspaceSchemaState`.
    WorkspaceSchemaState schemaState = WorkspaceSchemaState::Absent;
    uint32_t schemaVersion = 0;     ///< Meaningful only when `schemaState == Present`.
    WorkspaceVisibility visibility; ///< Panel keys found in the text; a missing key keeps its
                                    ///< `WorkspaceVisibility` default.
    /// Parsed scale, or default if missing or invalid.
    uint32_t uiScalePercent = kDefaultUiScalePercent;
};

/// Parses the body of Luminex's workspace settings section: line-oriented `Key=Value` text, the
/// form a (later, separately registered) `ImGuiSettingsHandler` hands over once it has located the
/// section within `imgui.ini`. This function never sees `ImGuiSettingsHandler`, ImGui, or SDL
/// types, and performs no file I/O -- callers own locating the section and reading the file.
///
/// Unknown keys are ignored. A panel key absent from `sectionText` keeps its `WorkspaceVisibility`
/// default rather than failing the parse; a boolean value other than `0` or `1` is likewise
/// ignored, leaving that panel's default in place. UiScalePercent accepts integers in [75,150];
/// missing, malformed or out-of-range values use kDefaultUiScalePercent.
ParsedWorkspaceSettings parseWorkspaceSettings(std::string_view sectionText);

/// Encodes schema, visibility and normalized UI scale as a deterministic Key=Value section body.
/// Parsing the result preserves the schema and visibility exactly and returns the normalized scale.
std::string writeWorkspaceSettings(uint32_t schemaVersion, const WorkspaceVisibility& visibility,
                                   uint32_t uiScalePercent = kDefaultUiScalePercent);

/// What the workspace shell must do with a parsed settings section (or its absence) at startup.
enum class WorkspaceDecisionKind {
    BuildDefault, ///< Build the default dock topology once and apply default visibility.
    Restore,      ///< Apply the stored visibility; the shell restores dock data as ImGui parsed it.
};

/// A startup decision the shell can act on directly, with no further branching over schema state.
struct WorkspaceDecision {
    /// See `WorkspaceDecisionKind`.
    WorkspaceDecisionKind kind = WorkspaceDecisionKind::BuildDefault;
    WorkspaceVisibility visibility; ///< Visibility to apply either way.
    /// Restored scale for the current schema or known version 2 migration; default otherwise.
    uint32_t uiScalePercent = kDefaultUiScalePercent;
};

/// Decides `BuildDefault` vs `Restore` from a parsed settings section, or from `std::nullopt` when
/// the ini had no Luminex workspace section at all -- a clean run, or an M5.2-era ini with no such
/// section, are both legacy (spec section 4).
///
/// Only an exact schema match restores parsed visibility and normalized scale. Every other schema
/// rebuilds default topology and visibility, so old dock data cannot reattach a detached panel.
/// The known version 2 migration retains normalized UI scale; absent, unparseable, older and future
/// schemas reset scale to its default as well.
WorkspaceDecision decideWorkspace(const std::optional<ParsedWorkspaceSettings>& parsed);

/// The default visibility (spec section 3), used both for `WorkspaceDecision::BuildDefault` and to
/// answer Reset Default Layout. This is a pure function of no input, so repeated calls -- and
/// repeated Reset Default Layout requests -- always produce the same value.
WorkspaceVisibility resetWorkspaceVisibility();

} // namespace lmx::app
