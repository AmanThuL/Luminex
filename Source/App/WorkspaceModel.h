//----------------------------------------------------------------------------------------------------------------------
/// @file WorkspaceModel.h
/// @brief Declares panel visibility and the workspace persistence schema decision.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lmx::app {

/// One of the editor's five independently visible top-level panels (spec section 3).
enum class EditorPanel {
    Scene,       ///< Active-scene choice, filtering, grouped subjects, single selection.
    Viewport,    ///< Rendered image, camera input, compact rendering toolbar.
    Inspector,   ///< Properties of the selected subject only.
    Performance, ///< Rolling frame-interval and GPU-pass observations.
    RenderGraph, ///< Exact newest-retired compiled-frame record and dump.
};

/// One app-owned visibility value per panel, with spec section 3's defaults (every panel open
/// except Render Graph). A Window-menu checkbox and a panel's window close button both read and
/// write the same storage through `isVisible`/`setVisible`, so close, reopen, and menu toggle
/// cannot disagree.
struct WorkspaceVisibility {
    bool scene = true;        ///< `EditorPanel::Scene`.
    bool viewport = true;     ///< `EditorPanel::Viewport`.
    bool inspector = true;    ///< `EditorPanel::Inspector`.
    bool performance = true;  ///< `EditorPanel::Performance`.
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
/// Version 2 is that kind of change: the Render Graph panel left the dockspace for a window of its
/// own, so a version 1 ini's dock data still carries a node holding it. Restoring that data would
/// bring the panel back as a tab, and dock data is only ever restored wholesale, so every version
/// 1 ini is legacy and rebuilds the default layout once.
inline constexpr uint32_t kWorkspaceSchemaVersion = 2;

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
};

/// Parses the body of Luminex's workspace settings section: line-oriented `Key=Value` text, the
/// form a (later, separately registered) `ImGuiSettingsHandler` hands over once it has located the
/// section within `imgui.ini`. This function never sees `ImGuiSettingsHandler`, ImGui, or SDL
/// types, and performs no file I/O -- callers own locating the section and reading the file.
///
/// Unknown keys are ignored. A panel key absent from `sectionText` keeps its `WorkspaceVisibility`
/// default rather than failing the parse; a boolean value other than `0` or `1` is likewise
/// ignored, leaving that panel's default in place.
ParsedWorkspaceSettings parseWorkspaceSettings(std::string_view sectionText);

/// Encodes `schemaVersion` and `visibility` as the deterministic `Key=Value` section body that
/// `parseWorkspaceSettings` round-trips exactly: for any `schemaVersion` and `visibility`,
/// `parseWorkspaceSettings(writeWorkspaceSettings(schemaVersion, visibility))` reports
/// `schemaState == Present` with that same `schemaVersion` and `visibility`.
std::string writeWorkspaceSettings(uint32_t schemaVersion, const WorkspaceVisibility& visibility);

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
};

/// Decides `BuildDefault` vs `Restore` from a parsed settings section, or from `std::nullopt` when
/// the ini had no Luminex workspace section at all -- a clean run, or an M5.2-era ini with no such
/// section, are both legacy (spec section 4).
///
/// A schema that does not exactly equal `kWorkspaceSchemaVersion` -- absent, unparseable, older, or
/// newer -- is also legacy. Migration never guesses how stored visibility fits a topology that does
/// not match the current schema, so every `BuildDefault` result carries default visibility rather
/// than whatever a legacy section happened to contain. Only an exact schema match restores the
/// parsed visibility unchanged.
WorkspaceDecision decideWorkspace(const std::optional<ParsedWorkspaceSettings>& parsed);

/// The default visibility (spec section 3), used both for `WorkspaceDecision::BuildDefault` and to
/// answer Reset Default Layout. This is a pure function of no input, so repeated calls -- and
/// repeated Reset Default Layout requests -- always produce the same value.
WorkspaceVisibility resetWorkspaceVisibility();

} // namespace lmx::app
