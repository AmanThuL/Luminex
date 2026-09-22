//----------------------------------------------------------------------------------------------------------------------
/// @file InspectorInternal.h
/// @brief Declares private section and row boundaries for the Inspector panel.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Panels/Inspector/InspectorPanel.h"

#include "App/Model/Rendering/Settings/EditorRenderDefaults.h"

#include <cstddef>
#include <string>

namespace lmx::app {

void beginFieldRow(const char* label);
void valueRow(const char* label, const std::string& value);
bool beginReadings(const char* id);
void drawRenderingReset(const InspectorPanelContext& context, EditorRenderGroup group);
void drawCameraSection(const InspectorPanelContext& context);
void drawRenderingSection(const InspectorPanelContext& context);
void drawTemporalSection(const InspectorPanelContext& context);
void drawDisplaySection(const InspectorPanelContext& context);
void drawDirectionalLightSection(const InspectorPanelContext& context, size_t index);
void drawObjectSection(const InspectorPanelContext& context, size_t index);

} // namespace lmx::app
