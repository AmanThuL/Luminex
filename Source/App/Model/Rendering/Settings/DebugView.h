//----------------------------------------------------------------------------------------------------------------------
/// @file DebugView.h
/// @brief Declares the editor's mutually exclusive diagnostic selector and reconciliation.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Rendering/Settings/EditorRenderSettings.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lmx::app {

/// Groups diagnostic entries in the View menu and viewport legend.
enum class DebugViewTopic {
    Temporal,  ///< Motion, reprojection and accumulation diagnostics.
    Lighting,  ///< Clustered local-light diagnostics.
    Occlusion, ///< Hierarchical depth pyramid levels.
};

/// One diagnostic request; Final is represented by an empty optional.
struct DebugView {
    DebugViewTopic topic; ///< Selects the interpretation of value.
    /// Non-Off TemporalDebugView or LightDebugView enumerator, or an HZB level in [0, 30].
    uint8_t value;
};

/// One owned menu entry with an actionable explanation when its prerequisites are absent.
struct DebugViewEntry {
    DebugView view;     ///< Diagnostic selected by this entry.
    std::string label;  ///< Human-readable view name within its topic.
    bool available;     ///< Whether selecting the view can satisfy the current mode constraints.
    std::string reason; ///< Empty exactly when available is true.
};

/// Lists temporal, lighting, then HZB entries. Other active diagnostics do not block selection,
/// which replaces them. HZB lists at most 31 levels; zero levels retains a disabled Level 0 entry.
std::vector<DebugViewEntry> debugViewEntries(const EditorRenderSettings& settings,
                                             uint32_t hzbLevels);

/// Returns the sole structurally valid diagnostic, even when its mode prerequisites changed.
/// Final, conflicting diagnostics and out-of-range values return null; reconcile before drawing.
std::optional<DebugView> activeDebugView(const EditorRenderSettings& settings);

/// Replaces all diagnostic fields, leaving rendering modes and checks intact; null selects Final.
/// A non-null request must have a valid topic and value. Callers offer available entries only;
/// reconcileDebugView clears a request whose prerequisites are no longer met.
void selectDebugView(EditorRenderSettings& settings, std::optional<DebugView> view);

/// Clears invalid or conflicting diagnostics and returns a one-time notice naming the reason.
/// Final and valid requests return null. Extent-dependent HZB clamping remains renderer-owned.
std::optional<std::string> reconcileDebugView(EditorRenderSettings& settings);

} // namespace lmx::app
