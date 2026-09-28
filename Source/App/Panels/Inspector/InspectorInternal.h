//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorInternal.h
/// @brief Declares private section and row boundaries for the Inspector panel.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Panels/Inspector/InspectorPanel.h"

#include <cstddef>
#include <string>

namespace lmx::app {

/// Draws the subject name, kind, optional enable checkbox and reset icon; the icon is enabled only
/// when `changed` reports something to restore. Returns whether reset was clicked.
bool drawInspectorHeader(const char* name, const char* kind, const char* resetTooltip, bool changed,
                         bool* enabled = nullptr);
void beginFieldRow(const char* label);
void valueRow(const char* label, const std::string& value);
void drawCameraSection(const InspectorPanelContext& context);
void drawDirectionalLightSection(const InspectorPanelContext& context, size_t index);
void drawObjectSection(const InspectorPanelContext& context, size_t index);
void drawGroupSection(const InspectorPanelContext& context);
void drawEnvironmentSection(const InspectorPanelContext& context);

} // namespace lmx::app
