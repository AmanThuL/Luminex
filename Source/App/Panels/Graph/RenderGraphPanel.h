//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphPanel.h
/// @brief Declares the Render Graph panel's canvas state and drawing entry point.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/Capture/ActionResult.h"
#include "App/Model/Graph/GraphLayout.h"
#include "App/Model/Graph/GraphSnapshot.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace lmx::app {

/// The Dear ImGui window name this panel submits. The Window menu's visibility checkbox names the
/// same panel, so both sides read the name from here rather than spelling it twice.
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

/// How far the canvas has got in turning the current picture's grid cells into pixels.
///
/// A card's size is known only after the node editor has laid it out once, so a picture is drawn a
/// first time on a coarse grid purely to be measured, placed from those measurements on the next
/// frame, and then left alone so the user's drags survive.
enum class GraphLayoutPhase {
    Provisional, ///< Drawn on the coarse grid; no card has reported a size yet.
    Measured,    ///< Measured positions were applied this frame; the view has yet to move to them.
    Settled      ///< Positions are final, and whatever the user drags from here is kept.
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
    /// keys are label-derived, so the set outlives a shape change; the column count defaults to
    /// zero, which is no wrap -- one row running left to right.
    GraphLayoutOptions layoutOptions;
    /// The `GraphLayout::signature` the current node positions were laid out from. Empty before the
    /// first layout. While it matches the drawn picture the canvas keeps whatever the user dragged;
    /// when it differs the layout's positions are reapplied.
    std::string appliedSignature;
    /// How far the current picture has got from grid cells to measured pixels. Reset to
    /// `Provisional` whenever the drawn picture changes, and by Reset Layout when the cards have
    /// yet to report a size.
    GraphLayoutPhase layoutPhase = GraphLayoutPhase::Provisional;
    /// Whether the panel's window owned an OS window of its own on the previous frame. The window
    /// flags have to be chosen before `ImGui::Begin` can answer that, so the answer is carried a
    /// frame: while it holds, the panel draws no title bar of its own and the OS draws the only
    /// one.
    bool ownsPlatformWindow = false;
    /// Index into `GraphLayout::items` of the single selected item, or empty when the selection is
    /// empty or covers more than one item. Panel-local: it is never a scene subject.
    std::optional<uint32_t> selectedItem;
    /// The value the column-count control is being edited toward. It equals
    /// `layoutOptions.columnsPerRow` except while an edit is in flight -- a held step button, or a
    /// number being typed -- because adopting each intermediate value would relayout the picture
    /// and drop the dragged positions and the selection with it. Zero, the default, is no wrap.
    int columnsEdit = 0;
    GraphSnapshot snapshot;  ///< Owns the 4 Hz publication or frozen record and matched timings.
    ActionResult dumpResult; ///< Last dump outcome, retained until another dump or dismissal.
    std::optional<render::CompiledFrameRecord>
        dumpPending;             ///< Exact clicked record awaiting write.
    std::string selectedKey;     ///< Logical selection identity across topology updates.
    std::string selectionNotice; ///< Explains a selection invalidated by a topology update.
    int navigation = 0; ///< One-shot request: 1 fits graph, 2 fits selection, 3 restores 100%.
    bool navigateAfterLayout = true; ///< Initial/reset layouts frame their leading columns.
    /// Last global font/control scale drawn. Changes remeasure cards once without changing zoom.
    float appliedUiScale = 0.0f;
};

/// Destroys the node-editor context and clears the canvas state, leaving `state` reusable.
///
/// Call it while the Dear ImGui context is still alive -- the node editor unregisters from it --
/// which for the shell means immediately before `ImGui::DestroyContext()`.
void releaseRenderGraphPanelState(RenderGraphPanelState& state);

/// Draws the Render Graph panel over its owned 4 Hz publication of the newest retained frame with
/// retired GPU timings. First data and Resume publish immediately. The record and timings are
/// joined by frame ID: the compiled frame as a node canvas on the left, and a details pane scoped
/// to the selected item on the right, under a header row carrying the frame identity, its transient
/// totals, a button that dumps that same record to a file next to the binary, a layout reset, and
/// the column count the layout wraps at -- zero, the default, being no wrap at all. `open` follows
/// the window's close button, and equally the OS window's, exactly as `ImGui::Begin` writes it.
///
/// A stage of passes draws as one group node until it is double-clicked open, and a pin carries a
/// compact label with its full name available on hover and in details, so the canvas shows the
/// frame's exact shape at the level of detail the user asked for.
///
/// This is the exact compiled shape of one frame, not a rolling summary, and it says so when no
/// frame has retired yet -- true for the first few frames of a run, and not an error. No editor
/// context is created until a frame has retired.
///
/// This panel's time domain is one published exact retired frame, independent of Performance pause.
void drawRenderGraphPanel(bool& open, RenderGraphPanelState& state,
                          const FrameRecordRing& frameRecords);

} // namespace lmx::app
