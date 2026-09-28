//----------------------------------------------------------------------------------------------------------------------
/// @file EditorRenderDefaults.h
/// @brief Declares group-scoped editor rendering defaults and reset operations.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Rendering/Settings/EditorRenderSettings.h"
#include "App/Model/Scene/EditorSelection.h"
#include "App/Model/Scene/SceneSession.h"

#include <optional>

namespace lmx::app {

/// Independent reset scopes; playback, debug views and other rendering groups remain unchanged.
enum class EditorRenderGroup {
    Lighting,       ///< Local-light mode and CPU list check.
    Exposure,       ///< Manual exposure and automatic metering/adaptation.
    Bloom,          ///< Bloom enable, threshold and intensity.
    Shadows,        ///< Shadow filter.
    Reconstruction, ///< Temporal inputs, jitter and requested algorithm.
    Resolution,     ///< Manual scale and dynamic-resolution policy settings.
    Display,        ///< Wireframe and transient-pooling switches.
};

/// Restores only the named group: Exposure/Bloom/Shadows use the loaded or saved scene look;
/// renderer configuration uses EditorRenderSettings defaults. Debug views belong to
/// View > Debug View and are never reset here; the defaults (Clustered, temporal on, Native TAA)
/// keep an active view valid. The caller reconciles exposure and temporal event latches through
/// its ordinary action path after this operation.
void resetRenderingGroup(EditorRenderSettings& settings, SceneSession& session,
                         EditorRenderGroup group);

/// True when a group differs from its scene reset baseline or renderer-configuration defaults.
bool renderingGroupChanged(const EditorRenderSettings& settings, const SceneSession& session,
                           EditorRenderGroup group);

/// True when any saved look field differs from the active document reset baseline.
bool sceneLookChanged(const SceneSession& session);

/// Restores exposure, bloom and shadow filtering together from the loaded/saved document.
void resetSceneLook(SceneSession& session);

/// The reset scope a Rendering panel topic restores, or nullopt for topics without one (Overview,
/// Visibility, Occlusion, Submission, Scene tables and out-of-range values).
std::optional<EditorRenderGroup> renderingTopicResetGroup(RenderingCategory topic);

} // namespace lmx::app
