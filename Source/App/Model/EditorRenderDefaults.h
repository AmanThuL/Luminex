//----------------------------------------------------------------------------------------------------------------------
/// @file EditorRenderDefaults.h
/// @brief Declares group-scoped editor rendering defaults and reset operations.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/EditorRenderSettings.h"

namespace lmx::app {

/// Independent reset scopes; playback and other rendering groups remain unchanged.
enum class EditorRenderGroup {
    Lighting,       ///< Local-light mode, diagnostic view and CPU list check.
    Exposure,       ///< Manual exposure and automatic metering/adaptation.
    Bloom,          ///< Bloom enable, threshold and intensity.
    Shadows,        ///< Shadow filter.
    Reconstruction, ///< Temporal inputs, jitter, requested algorithm and diagnostic.
    Resolution,     ///< Manual scale and dynamic-resolution policy settings.
    Display,        ///< Wireframe and transient-pooling switches.
};

/// Restores only the named group's fields to EditorRenderSettings defaults. The caller reconciles
/// exposure and temporal event latches through its ordinary action path after this operation.
void resetRenderingGroup(EditorRenderSettings& settings, EditorRenderGroup group);

/// True when at least one field in this group differs from its documented editor default.
bool renderingGroupChanged(const EditorRenderSettings& settings, EditorRenderGroup group);

} // namespace lmx::app
