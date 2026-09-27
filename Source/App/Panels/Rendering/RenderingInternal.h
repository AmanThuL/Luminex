//----------------------------------------------------------------------------------------------------------------------
/// @file RenderingInternal.h
/// @brief Declares private topic drawing boundaries for the Rendering panel.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "App/Panels/Inspector/InspectorPanel.h"
namespace lmx::app {
void drawRenderingTopic(const InspectorPanelContext& context, RenderingCategory category);
void drawLightingTopic(const InspectorPanelContext& context);
void drawPerformanceDetails(const InspectorPanelContext& context);
} // namespace lmx::app
