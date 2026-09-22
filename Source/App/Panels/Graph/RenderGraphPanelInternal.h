//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphPanelInternal.h
/// @brief Declares private canvas, details and dump boundaries for the Render Graph panel.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Panels/Graph/RenderGraphPanel.h"

#include <string_view>

namespace lmx::app::graph_panel {

/// Creates the canvas context once, while the owning ImGui context is alive.
void ensureCanvas(RenderGraphPanelState& state);
/// Draws nodes and updates selection/layout for the current borrowed model.
void drawCanvas(const GraphNodeModel& model, const GraphLayout& layout,
                RenderGraphPanelState& state, bool resetLayout);
/// Draws the selected node or stage and applies expansion edits to the panel state.
void drawDetails(const GraphNodeModel& model, const GraphLayout& layout,
                 RenderGraphPanelState& state);
/// Writes the displayed compiled record to its frame-numbered file in the working directory.
ActionResult dumpFrame(const render::CompiledFrameRecord& record, uint64_t frameId);
/// Returns the panel's label for a pass kind.
std::string_view passKindLabel(render::PassKind kind);
/// Returns the panel's label for a culling reason.
std::string_view cullReasonLabel(render::CullReason reason);
/// Toggles a stage key in the shared canvas/details expansion state.
void toggleGroupExpansion(GraphLayoutOptions& options, const std::string& key);

} // namespace lmx::app::graph_panel
