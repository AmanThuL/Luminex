//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorInternal.h
/// @brief Declares private section and row boundaries for the Inspector panel.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Panels/Inspector/InspectorPanel.h"

#include <cstddef>
#include <string>

namespace lmx::app {

bool drawInspectorHeader(const char* name, const char* kind, const char* resetTooltip,
                         bool* enabled = nullptr);
void beginFieldRow(const char* label);
void valueRow(const char* label, const std::string& value);
bool beginReadings(const char* id);
void drawCameraSection(const InspectorPanelContext& context);
void drawDirectionalLightSection(const InspectorPanelContext& context, size_t index);
void drawObjectSection(const InspectorPanelContext& context, size_t index);

} // namespace lmx::app
