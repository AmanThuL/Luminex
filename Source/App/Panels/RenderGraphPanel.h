//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphPanel.h
/// @brief Declares the Render Graph panel's canvas state and drawing entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/FrameRecordRing.h"
#include "App/GraphLayout.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The shell's dock builder places the window under
/// exactly this name, so both sides read it from here.
inline constexpr const char* kRenderGraphPanelWindowName = "Render Graph";

/// The node-editor context behind the canvas, declared but never defined outside the panel's
/// implementation file. It exists so the shell can own the context's lifetime without any
/// `ax::NodeEditor` type -- and so without `imgui_internal.h` -- reaching a Luminex header.
struct NodeEditorHandle;

/// Destroys a NodeEditorHandle. Defined beside the handle itself, which is what lets a
/// `std::unique_ptr` to the incomplete type above be declared here and destroyed anywhere.
struct NodeEditorHandleDeleter {
    /// Destroys the node-editor context the handle owns, then the handle. Null is a no-op. Must run
    /// while the Dear ImGui context that the canvas draws into is still alive.
    void operator()(NodeEditorHandle* handle) const;
};

/// Everything the Render Graph canvas keeps between frames: the node-editor context, what the user
/// asked the layout for, the picture the canvas is currently laid out for, and the selected item.
///
/// The owner is `EditorShell`, not the panel, because the context outlives any one draw -- it holds
/// the node positions the user dragged, the zoom, and the pan -- and because it has to be released
/// before `ImGui::DestroyContext()`, a boundary only the shell can see. Closing and reopening the
/// panel does not touch it.
struct RenderGraphPanelState {
    /// The node-editor context, created lazily on the first draw that has a retired frame and null
    /// until then. Session state only: no settings file is configured, so nothing is persisted.
    std::unique_ptr<NodeEditorHandle, NodeEditorHandleDeleter> editor;
    /// What the user asked of the layout: the column count and the groups they opened. Expansion
    /// keys are label-derived, so the set outlives a shape change; the column count is seeded from
    /// the canvas width on the first draw and by Reset Layout.
    GraphLayoutOptions layoutOptions;
    /// The `GraphLayout::signature` the current node positions were laid out from. Empty before the
    /// first layout. While it matches the drawn picture the canvas keeps whatever the user dragged;
    /// when it differs the layout's positions are reapplied.
    std::string appliedSignature;
    /// Index into `GraphLayout::items` of the single selected item, or empty when the selection is
    /// empty or covers more than one item. Panel-local: it is never a scene subject.
    std::optional<uint32_t> selectedItem;
};

/// Destroys the node-editor context and clears the canvas state, leaving `state` reusable.
///
/// Call it while the Dear ImGui context is still alive -- the node editor unregisters from it --
/// which for the shell means immediately before `ImGui::DestroyContext()`.
void releaseRenderGraphPanelState(RenderGraphPanelState& state);

/// Draws the Render Graph panel over the newest retained frame whose GPU timings have retired and
/// joined by frame ID: the compiled frame as a node canvas on the left, and a details pane scoped
/// to the selected item on the right, under a header row carrying the frame identity, its transient
/// totals, a button that dumps that same record to a file next to the binary, a layout reset, and
/// the column count the layout wraps at. `open` follows the window's close button, exactly as
/// `ImGui::Begin` writes it.
///
/// A stage of passes draws as one group node until it is double-clicked open, and a pin carries a
/// short label until it is hovered or its item is selected, so what the canvas shows is the frame's
/// exact shape at the level of detail the user asked for.
///
/// This is the exact compiled shape of one frame, not a rolling summary, and it says so when no
/// frame has retired yet -- true for the first few frames of a run, and not an error. No editor
/// context is created until a frame has retired.
///
/// This panel's time domain is the exact newest retired frame, independent of Performance pause.
void drawRenderGraphPanel(bool& open, RenderGraphPanelState& state,
                          const FrameRecordRing& frameRecords);

} // namespace lmx::app
