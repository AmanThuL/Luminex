//----------------------------------------------------------------------------------------------------------------------
/// @file RenderingPanel.h
/// @brief Declares the dockable rendering controls panel.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

namespace lmx::app {
struct InspectorPanelContext;
/// Stable window identity shared by the panel, Window menu and workspace docking.
inline constexpr const char* kRenderingPanelWindowName = "Rendering";
/// Draws rendering topics with scoped controls, readings and diagnostics; edits borrowed state.
void drawRenderingPanel(bool& open, const InspectorPanelContext& context);
} // namespace lmx::app
