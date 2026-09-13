//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphPanel.cpp
/// @brief Coordinates the detached Render Graph window, canvas, details and dump actions.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/RenderGraphPanel.h"

#include "App/Panels/RenderGraphPanelInternal.h"

#include <imgui.h>

#include <algorithm>

namespace lmx::app {
namespace {

/// Share of the panel's client width the canvas takes; the details pane gets the rest.
constexpr float kCanvasWidthFraction = 0.65f;

/// Width below which the details pane stops shrinking and the canvas gives up space instead.
constexpr float kDetailsMinWidth = 220.0f;

/// Floor on the canvas child's width. A zero width would read as "take everything left" to
/// ImGui::BeginChild, which is the opposite of what a panel too narrow to split should do.
constexpr float kCanvasMinWidth = 80.0f;

/// Size the detached Render Graph window takes the first time it is ever opened, in ImGui points.
constexpr float kDetachedWidth = 1280.0f;
constexpr float kDetachedHeight = 800.0f;

/// Docking class of the Render Graph window. Any non-zero value distinguishes it from the unclassed
/// panels; it is a literal rather than ImHashStr("...") because that lives in imgui_internal.h,
/// which this panel deliberately does not include.
constexpr ImGuiID kRenderGraphWindowClassId = 0x6C6D7867u; // 'lmxg'

/// Bounds the column-count control accepts, and the width it is drawn at. Zero is the default and
/// means no wrap: one row, left to right, which is the only arrangement a chain reads cleanly in.
constexpr int kMinColumns = 0;
constexpr int kMaxColumns = 32;
constexpr float kColumnsControlWidth = 110.0f;

//======================================================================================================================
// The canvas takes the larger share of the panel and yields it back only when the details pane
// would otherwise be unreadable.
float canvasWidthFor(float availableWidth) {
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float canvasWidth = availableWidth * kCanvasWidthFraction;
    if (availableWidth - canvasWidth - spacing < kDetailsMinWidth) {
        return std::max(kCanvasMinWidth, availableWidth - kDetailsMinWidth - spacing);
    }
    return canvasWidth;
}

} // namespace

//======================================================================================================================
void releaseRenderGraphPanelState(RenderGraphPanelState& state) {
    state.editor.reset();
    state.layoutOptions = {};
    state.appliedSignature.clear();
    state.selectedItem.reset();
    state.columnsEdit = 0;
    state.layoutPhase = GraphLayoutPhase::Provisional;
    state.ownsPlatformWindow = false;
}

//======================================================================================================================
void drawRenderGraphPanel(bool& open, RenderGraphPanelState& state,
                          const FrameRecordRing& frameRecords) {
    // A class of its own, refusing unclassed dock targets and overriding the viewport into
    // NoAutoMerge: together those keep this panel out of the main dockspace and out of the main
    // OS window, so an open Render Graph always has its own window to be large in. Clearing
    // NoDecoration overrides io.ConfigViewportsNoDecoration for this window alone, which is what
    // gives it a real macOS title bar with close, minimise, and zoom.
    static const ImGuiWindowClass windowClass = [] {
        ImGuiWindowClass created;
        created.ClassId = kRenderGraphWindowClassId;
        created.DockingAllowUnclassed = false;
        created.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        created.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
        return created;
    }();
    ImGui::SetNextWindowClass(&windowClass);
    // First open only; afterwards imgui.ini carries whatever the user left. Centred over the main
    // window's work area keeps the detached window reachable whether the main window is maximized
    // (where "just right of it" would land off-screen) or --windowed's fixed size; the clamp to
    // the work area's origin covers a detached window taller or wider than that area.
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const ImVec2 centeredPos = {
        mainViewport->WorkPos.x + (mainViewport->WorkSize.x - kDetachedWidth) * 0.5f,
        mainViewport->WorkPos.y + (mainViewport->WorkSize.y - kDetachedHeight) * 0.5f};
    ImGui::SetNextWindowPos({std::max(centeredPos.x, mainViewport->WorkPos.x),
                             std::max(centeredPos.y, mainViewport->WorkPos.y)},
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({kDetachedWidth, kDetachedHeight}, ImGuiCond_FirstUseEver);

    // NoDocking is the half of the rule a window class cannot state: DockingAllowUnclassed only
    // refuses drop targets, while a DockId persisted by a pre-M5.5 imgui.ini would otherwise be
    // rebound on Begin without any class being consulted. On this flag Begin undocks instead.
    //
    // NoTitleBar follows the previous frame's answer to "does this window own an OS window?",
    // because the flags have to be decided before Begin can say. While it does, the OS title bar is
    // the only one: two stacked title bars is what drawing both would give.
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;
    if (state.ownsPlatformWindow) {
        windowFlags |= ImGuiWindowFlags_NoTitleBar;
    }
    const bool visible = ImGui::Begin(kRenderGraphPanelWindowName, &open, windowFlags);
    state.ownsPlatformWindow = ImGui::GetWindowViewport() != ImGui::GetMainViewport();
    if (!visible) {
        ImGui::End();
        return;
    }

    const RetainedFrame* newest = frameRecords.newestTimedFrame();
    if (newest == nullptr) {
        // Nothing has retired yet -- true for the first few frames of a run, and not an error. No
        // editor context is created until there is a graph to draw into it.
        ImGui::TextUnformatted("no retired frame yet");
        ImGui::End();
        return;
    }

    const GraphNodeModel model = buildGraphNodeModel(newest->record, newest->timings);
    // Measured before the header row, which changes the height left for the children but not the
    // width they are given.
    const float canvasWidth = canvasWidthFor(ImGui::GetContentRegionAvail().x);

    ImGui::Text("frame %llu -- pooling %s", static_cast<unsigned long long>(model.frameId),
                model.poolingEnabled ? "on" : "off");
    ImGui::SameLine();
    if (ImGui::Button("Dump frame")) {
        graph_panel::dumpFrame(newest->record, model.frameId);
    }
    ImGui::SameLine();
    // Reset Layout re-measures the cards and places them again, which is also what drops whatever
    // the user dragged. It seeds no column count: the default is one row, left to right.
    const bool resetLayout = ImGui::Button("Reset Layout");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kColumnsControlWidth);
    // The control edits its own value and the layout adopts it only once the edit is finished:
    // every intermediate value is a different picture, and reapplying a picture drops the node
    // positions and the selection with it. A held step button is one gesture, not one per repeat.
    ImGui::InputInt("columns", &state.columnsEdit);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        state.columnsEdit = std::clamp(state.columnsEdit, kMinColumns, kMaxColumns);
        state.layoutOptions.columnsPerRow = static_cast<uint32_t>(state.columnsEdit);
    }
    ImGui::SameLine();
    ImGui::TextDisabled(state.layoutOptions.columnsPerRow == 0 ? "no wrap" : "0 = no wrap");
    ImGui::Text("transients: requested %llu B, high-water %llu B, saved %llu B",
                static_cast<unsigned long long>(model.memory.requested),
                static_cast<unsigned long long>(model.memory.highWater),
                static_cast<unsigned long long>(model.memory.aliasSavings));
    ImGui::Separator();

    graph_panel::ensureCanvas(state);

    const GraphLayout layout = layoutGraph(model, state.layoutOptions);
    const float availableHeight = ImGui::GetContentRegionAvail().y;

    if (ImGui::BeginChild("RenderGraphCanvas", ImVec2(canvasWidth, availableHeight),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        graph_panel::drawCanvas(model, layout, state, resetLayout);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("RenderGraphDetails", ImVec2(0.0f, availableHeight),
                          ImGuiChildFlags_Borders)) {
        graph_panel::drawDetails(model, layout, state);
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace lmx::app
