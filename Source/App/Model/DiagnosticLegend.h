//----------------------------------------------------------------------------------------------------------------------
/// @file DiagnosticLegend.h
/// @brief Declares shader-derived diagnostic legends and scene context for the editor.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Engine/Catalog/SceneLibrary.h"
#include "Engine/Types/LocalLight.h"
#include "Render/Temporal.h"

#include <string_view>

namespace lmx::app {

/// Static display text describing the signal the selected diagnostic actually writes.
struct DiagnosticLegend {
    std::string_view name;        ///< Concise viewport mode badge.
    std::string_view description; ///< Encoding, units, ranges and special values.
};

/// Returns static strings derived from TemporalDebugView.slang and its producer kernels.
DiagnosticLegend diagnosticLegend(render::TemporalDebugView view);

/// Returns the clustered lighting overlay encoding, independent of exposure and temporal history.
DiagnosticLegend diagnosticLegend(engine::LightDebugView view);

/// Returns an actionable explanation when this view cannot execute, otherwise empty. Uses the
/// effective reconstruction, so a vendor request running Native TAA fallback retains native views.
std::string_view diagnosticUnavailableReason(render::TemporalDebugView view, bool temporalEnabled,
                                             render::ReconstructionMode effective);

/// Explains mode-specific placeholder or coverage signals; empty where the general legend applies.
std::string_view diagnosticModeNote(render::TemporalDebugView view,
                                    render::ReconstructionMode effective);

/// Returns concise purpose and authored axes for diagnostic labs; empty for other catalog scenes.
/// The identifier must belong to the current scene catalog. Returned text has static lifetime.
std::string_view labDescription(engine::SceneId sceneId);

} // namespace lmx::app
