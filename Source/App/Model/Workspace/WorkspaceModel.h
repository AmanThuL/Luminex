//----------------------------------------------------------------------------------------------------------------------
/// @file WorkspaceModel.h
/// @brief Declares panel visibility and the workspace persistence schema decision.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Workspace/EditorTheme.h"

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

/// One of the editor's independently visible top-level panels (spec section 3).
enum class EditorPanel {
    Scene,              ///< Scene-only Hierarchy: filtering, grouped subjects, single selection.
    Viewport,           ///< Rendered image, camera input and the debug-view legend chip.
    Rendering,          ///< Rendering controls, readings and diagnostics.
    Inspector,          ///< Properties of the selected subject only.
    PerformanceSummary, ///< Compact dockable performance readings.
    Performance,        ///< Rolling frame-interval and GPU-pass observations.
    RenderGraph,        ///< Coherent published compiled-frame record and dump.
    Console,            ///< Bounded project log viewer.
    Session,            ///< Reviewed proposals and session action history.
};

/// One app-owned visibility value per panel. Performance and Render Graph start closed; the
/// docked panels start open. A Window-menu checkbox and a panel's window close button both read and
/// write the same storage through `isVisible`/`setVisible`, so close, reopen, and menu toggle
/// cannot disagree.
struct WorkspaceVisibility {
    bool scene = true;              ///< `EditorPanel::Scene`.
    bool viewport = true;           ///< `EditorPanel::Viewport`.
    bool rendering = true;          ///< `EditorPanel::Rendering`.
    bool inspector = true;          ///< `EditorPanel::Inspector`.
    bool performanceSummary = true; ///< `EditorPanel::PerformanceSummary`.
    bool performance = false;       ///< `EditorPanel::Performance`; a detached diagnostic window.
    bool console = true;      ///< `EditorPanel::Console`; selected beside the summary by default.
    bool renderGraph = false; ///< `EditorPanel::RenderGraph`.
    bool session = false;     ///< `EditorPanel::Session`; docked and initially hidden.

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
/// Version 6 adds a hidden Session panel. Version 5 restores without rebuilding, preserving
/// appearance, density and every earlier visibility. Version 4 restores and uses
/// Auto appearance and Comfortable density. Version 4 persists Rendering and the docked Performance
/// summary. Version 3 rebuilds main docks once while preserving its six visibilities, UI scale and
/// detached window placement. Version 2 rebuilds default visibility and docks while preserving
/// scale. Unknown schemas use defaults. Visibility and UI scale keys remain optional within the
/// current schema.
inline constexpr uint32_t kWorkspaceSchemaVersion = 6;

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
    Appearance appearance = Appearance::Auto; ///< Stored editor appearance preference.
    Density density = Density::Comfortable;   ///< Stored editor spacing preference.
};

/// Parses the body of Luminex's workspace settings section: line-oriented `Key=Value` text, the
/// form a (later, separately registered) `ImGuiSettingsHandler` hands over once it has located the
/// section within `imgui.ini`. This function never sees `ImGuiSettingsHandler`, ImGui, or SDL
/// types, and performs no file I/O -- callers own locating the section and reading the file.
///
/// Unknown keys are ignored. A panel key absent from `sectionText` keeps its `WorkspaceVisibility`
/// default rather than failing the parse; a boolean value other than `0` or `1` is likewise
/// ignored, leaving that panel's default in place. UiScalePercent accepts integers in [75,150];
/// missing, malformed or out-of-range values use kDefaultUiScalePercent. Appearance and Density
/// accept lowercase storage names; missing or unknown values keep Auto and Comfortable.
ParsedWorkspaceSettings parseWorkspaceSettings(std::string_view sectionText);

/// Encodes schema, visibility, appearance, density and normalized UI scale as a deterministic
/// Key=Value section body. Parsing the result preserves the schema and visibility exactly and
/// returns the normalized scale.
std::string writeWorkspaceSettings(uint32_t schemaVersion, const WorkspaceVisibility& visibility,
                                   uint32_t uiScalePercent = kDefaultUiScalePercent,
                                   Appearance appearance = Appearance::Auto,
                                   Density density = Density::Comfortable);

/// What the workspace shell must do with a parsed settings section (or its absence) at startup.
enum class WorkspaceDecisionKind {
    BuildDefault, ///< Rebuild the main dock topology once and apply the returned visibility.
    Restore,      ///< Apply the stored visibility; the shell restores dock data as ImGui parsed it.
};

/// A startup decision the shell can act on directly, with no further branching over schema state.
struct WorkspaceDecision {
    /// See `WorkspaceDecisionKind`.
    WorkspaceDecisionKind kind = WorkspaceDecisionKind::BuildDefault;
    WorkspaceVisibility visibility; ///< Visibility to apply either way.
    /// Restored scale for the current schema or known version 2/3 migration; default otherwise.
    uint32_t uiScalePercent = kDefaultUiScalePercent;
    Appearance appearance = Appearance::Auto; ///< Restored editor appearance preference.
    Density density = Density::Comfortable;   ///< Restored editor spacing preference.
    /// Re-center Performance on its next open only for default recovery, never schema 3 migration.
    bool resetPerformancePlacement = true;
};

/// Decides `BuildDefault` vs `Restore` from a parsed settings section, or from `std::nullopt` when
/// the ini had no Luminex workspace section at all -- a clean run, or an M5.2-era ini with no such
/// section, are both legacy (spec section 4).
///
/// The current schema and version 5 restore docks and all earlier values, with Session hidden for
/// version 5. Version 4 restores docks, visibility and scale with default appearance and density.
/// Version 3 rebuilds main docking once while
/// preserving old visibilities/scale and enabling both new docked panels. Its detached window
/// settings remain valid. Version 2 keeps only scale; unknown schemas reset every preference.
WorkspaceDecision decideWorkspace(const std::optional<ParsedWorkspaceSettings>& parsed);

/// The default visibility (spec section 3), used both for `WorkspaceDecision::BuildDefault` and to
/// answer Reset Default Layout. This is a pure function of no input, so repeated calls -- and
/// repeated Reset Default Layout requests -- always produce the same value.
WorkspaceVisibility resetWorkspaceVisibility();

/// One-shot placement for Session's first opening. A live floating Console (dock zero) is
/// authoritative over a stale saved dock, and an absent dock leaves Session where ImGui opens it.
class SessionDockPlacement {
public:
    /// Records whether the ini already placed Session, including as a floating window.
    void setSavedPlacement(bool saved) { m_savedPlacement = saved; }
    /// Returns Console's current dock if it has a live window, otherwise its saved dock. Once
    /// called, later Console movements cannot relocate Session, even if no dock was available.
    std::optional<uint32_t> onFirstOpen(std::optional<uint32_t> liveConsoleDockId,
                                        uint32_t savedConsoleDockId);
    /// Reports whether the first opening has made its placement decision.
    bool initialized() const { return m_initialized; }

private:
    bool m_savedPlacement = false;
    bool m_initialized = false;
};

} // namespace lmx::app
