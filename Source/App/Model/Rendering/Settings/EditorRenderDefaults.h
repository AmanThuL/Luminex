//----------------------------------------------------------------------------------------------------------------------
/// @file EditorRenderDefaults.h
/// @brief Declares group-scoped editor rendering defaults and reset operations.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Rendering/Settings/EditorRenderSettings.h"
#include "App/Model/Scene/EditorSelection.h"

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

/// Restores only the named group's fields to EditorRenderSettings defaults. Debug views belong to
/// View > Debug View and are never reset here; the defaults (Clustered, temporal on, Native TAA)
/// keep an active view valid. The caller reconciles exposure and temporal event latches through
/// its ordinary action path after this operation.
void resetRenderingGroup(EditorRenderSettings& settings, EditorRenderGroup group);

/// True when at least one field in this group differs from its documented editor default.
bool renderingGroupChanged(const EditorRenderSettings& settings, EditorRenderGroup group);

/// The reset scope a Rendering panel topic restores, or nullopt for topics without one (Overview,
/// Visibility, Occlusion, Submission, Scene tables and out-of-range values).
std::optional<EditorRenderGroup> renderingTopicResetGroup(RenderingCategory topic);

} // namespace lmx::app
