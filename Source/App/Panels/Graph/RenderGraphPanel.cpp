//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphPanel.cpp
/// @brief Coordinates the detached Render Graph window, canvas, details and dump actions.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Graph/RenderGraphPanel.h"

#include "App/Panels/Graph/RenderGraphPanelInternal.h"
#include "App/Panels/Shared/EditorStyle.h"

#include <imgui.h>

#include <algorithm>
#include <format>

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
    const float uiScale = ImGui::GetStyle().FontScaleMain;
    const float canvasWidth = availableWidth * kCanvasWidthFraction;
    if (availableWidth - canvasWidth - spacing < kDetailsMinWidth * uiScale) {
        return std::max(kCanvasMinWidth * uiScale,
                        availableWidth - kDetailsMinWidth * uiScale - spacing);
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
    state.appliedUiScale = 0.0f;
    state.ownsPlatformWindow = false;
    state.snapshot.resume();
    state.selectedKey.clear();
    state.selectionNotice.clear();
    state.dumpPending.reset();
}

//======================================================================================================================
void drawRenderGraphPanel(bool& open, RenderGraphPanelState& state,
                          const FrameRecordRing& frameRecords, NoticeQueue& notices) {
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

    if (state.dumpPending) {
        notices.post(graph_panel::dumpFrame(*state.dumpPending, state.dumpPending->frameId),
                     ImGui::GetTime());
        state.dumpPending.reset();
    }
    state.snapshot.update(ImGui::GetTime(), frameRecords.newestTimedFrame());
    const RetainedFrame* newest = state.snapshot.displayed();
    if (newest == nullptr) {
        // Nothing has retired yet -- true for the first few frames of a run, and not an error. No
        // editor context is created until there is a graph to draw into it.
        ImGui::TextUnformatted("no retired frame yet");
        ImGui::End();
        return;
    }

    editor_style::beginHeaderRow();
    const bool frozen = state.snapshot.frozen();
    const std::string freezeHelp = std::format(
        "{}\nFrame {}. Exact-frame timings, published at 4 Hz. Freeze does not pause the scene. "
        "Pooling {}. Memory totals are in Details.",
        frozen ? "Resume graph: publish the newest retired frame."
               : "Freeze this frame and its exact GPU timings. Scene playback continues.",
        newest->record.frameId, newest->record.debug.poolingEnabled ? "on" : "off");
    if (editor_style::iconButton("GraphFreeze", frozen ? EditorIcon::Unlock : EditorIcon::Lock,
                                 true, freezeHelp.c_str())) {
        if (frozen)
            state.snapshot.resume();
        else
            state.snapshot.freeze();
        state.snapshot.update(ImGui::GetTime(), frameRecords.newestTimedFrame());
        newest = state.snapshot.displayed();
    }
    if (!newest) {
        editor_style::endHeaderRow();
        ImGui::TextUnformatted("Waiting for a retired frame.");
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (editor_style::iconButton("FitGraph", EditorIcon::FitGraph, true,
                                 "Fit all visible graph cards, including culled passes."))
        state.navigation = 1;
    ImGui::SameLine();
    if (editor_style::iconButton("FitSelection", EditorIcon::FitSelection,
                                 state.selectedItem.has_value(),
                                 "Center and fit the selected card. Select a node or stage first."))
        state.navigation = 2;
    ImGui::SameLine();
    if (ImGui::Button("1:1"))
        state.navigation = 3;
    editorTooltip("Restore one canvas unit per logical screen point, keeping the view center.");
    ImGui::SameLine();
    bool resetLayout = false;
    const GraphNodeModel model = buildGraphNodeModel(newest->record, newest->timings);
    if (editor_style::overflowMenu("GraphMore")) {
        if (ImGui::MenuItem("Reset layout"))
            resetLayout = true;
        editorTooltip("Arrange cards again, replacing dragged positions. Expansion and columns are "
                      "preserved.");
        ImGui::SetNextItemWidth(kColumnsControlWidth * ImGui::GetStyle().FontScaleMain);
        ImGui::InputInt("Columns", &state.columnsEdit);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            state.columnsEdit = std::clamp(state.columnsEdit, kMinColumns, kMaxColumns);
            state.layoutOptions.columnsPerRow = static_cast<uint32_t>(state.columnsEdit);
        }
        editorTooltip(
            "Maximum columns per row. Zero keeps one long row. Applied when editing finishes.");
        if (ImGui::MenuItem("Dump frame")) {
            state.dumpPending = newest->record;
            notices.post({ActionStatus::Pending, "Writing the displayed frame...", ""},
                         ImGui::GetTime());
        }
        editorTooltip("Write this displayed compiled frame beside the app binary. Frozen and live "
                      "export the shown frame.");
        ImGui::EndPopup();
    }
    if (state.snapshot.frozen() || state.snapshot.stale(ImGui::GetTime())) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s | frame %llu", state.snapshot.frozen() ? "Frozen" : "Stale",
                            static_cast<unsigned long long>(newest->record.frameId));
        editorTooltip(state.snapshot.frozen()
                          ? "The displayed frame stays fixed until Resume."
                          : "No newer retired frame has arrived for at least one second.");
    }
    editor_style::endHeaderRow();
    if (!state.selectionNotice.empty())
        ImGui::TextWrapped("%s", state.selectionNotice.c_str());
    ImGui::Separator();

    graph_panel::ensureCanvas(state);

    const GraphLayout layout = layoutGraph(model, state.layoutOptions);
    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float uiScale = ImGui::GetStyle().FontScaleMain;
    const bool stacked = availableWidth < 760.0f * uiScale;
    const float canvasWidth = stacked ? availableWidth : canvasWidthFor(availableWidth);
    const float canvasHeight =
        stacked ? std::max(100.0f * uiScale, availableHeight * 0.58f) : availableHeight;
    if (ImGui::BeginChild("RenderGraphCanvas", ImVec2(canvasWidth, canvasHeight),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        graph_panel::drawCanvas(model, layout, state, resetLayout);
    }
    ImGui::EndChild();

    if (!stacked) {
        ImGui::SameLine();
    }
    if (ImGui::BeginChild("RenderGraphDetails", ImVec2(0.0f, stacked ? 0.0f : availableHeight),
                          ImGuiChildFlags_Borders)) {
        ImGui::PushTextWrapPos(0.0f);
        graph_panel::drawDetails(model, layout, state);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace lmx::app
