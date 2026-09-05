//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphPanel.cpp
/// @brief Implements the Render Graph panel as a node canvas over one exact retired compiled frame.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/RenderGraphPanel.h"

#include "App/GraphInspectorModel.h"
#include "App/GraphNodeModel.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "Render/GraphDump.h"

#include <imgui.h>
// imgui_node_editor.h pulls in imgui_internal.h. It is included here and nowhere else, so no
// Luminex header ever exposes an ax::NodeEditor type or Dear ImGui's internal surface.
#include <imgui_node_editor.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace ed = ax::NodeEditor;

namespace lmx::app {

/// The node-editor context, owned through the opaque handle the panel's header declares.
struct NodeEditorHandle {
    ed::EditorContext* context = nullptr; ///< Never null once the panel has created a handle.
};

namespace {

/// Share of the panel's client width the canvas takes; the details pane gets the rest.
constexpr float kCanvasWidthFraction = 0.65f;

/// Width below which the details pane stops shrinking and the canvas gives up space instead.
constexpr float kDetailsMinWidth = 220.0f;

/// Floor on the canvas child's width. A zero width would read as "take everything left" to
/// ImGui::BeginChild, which is the opposite of what a panel too narrow to split should do.
constexpr float kCanvasMinWidth = 80.0f;

/// Narrowest a node body may be, so a short-labelled pass still reads as a box.
constexpr float kNodeMinWidth = 150.0f;

/// Horizontal gap held between a node's input and output pin columns.
constexpr float kPinColumnGap = 24.0f;

/// Drawn and skipped run lengths of a dashed segment, in canvas units.
constexpr float kDashLength = 6.0f;
constexpr float kDashGap = 5.0f;

/// Half-height and length of the arrowhead that marks a sink node as an endpoint.
constexpr float kSinkMarkerHalfHeight = 9.0f;
constexpr float kSinkMarkerLength = 11.0f;

/// Pin ordinals are packed into a node's id block of this size, which caps the pins one node may
/// declare before two nodes would collide in the id space.
constexpr uint32_t kPinIdStride = 1000;

/// Solid execution edges, and the dashed overlays drawn outside the node editor's link path.
constexpr float kEdgeThickness = 1.7f;
constexpr float kDashThickness = 1.4f;
const ImVec4 kEdgeColor{0.62f, 0.70f, 0.80f, 1.0f};
constexpr ImU32 kAliasLinkColor = IM_COL32(214, 122, 196, 220);
constexpr ImU32 kCulledBorderColor = IM_COL32(140, 140, 146, 220);

//======================================================================================================================
std::string_view passKindLabel(render::PassKind kind) {
    switch (kind) {
    case render::PassKind::Raster:
        return "raster";
    case render::PassKind::Compute:
        return "compute";
    case render::PassKind::Copy:
        return "copy";
    }
    return "raster";
}

//======================================================================================================================
std::string_view cullReasonLabel(render::CullReason reason) {
    switch (reason) {
    case render::CullReason::ProducesNothing:
        return "produces nothing";
    case render::CullReason::NoSinkReachesIt:
        return "no sink reaches it";
    }
    return "no sink reaches it";
}

//======================================================================================================================
ed::NodeId nodeIdOf(uint32_t nodeIndex) {
    return ed::NodeId(static_cast<uintptr_t>(nodeIndex) + 1);
}

//======================================================================================================================
// Inputs occupy the low ordinals of a node's block and outputs the rest, so a pin's identity is
// stable for as long as the shape signature that produced it is.
ed::PinId pinIdOf(uint32_t nodeIndex, uint32_t pinOrdinal) {
    return ed::PinId(static_cast<uintptr_t>(nodeIndex + 1) * kPinIdStride + pinOrdinal + 1);
}

//======================================================================================================================
// The node body's fill, chosen so the declaration path a pass took is readable at a glance and a
// culled pass reads as inert next to the graph that was proved.
ImVec4 nodeBackgroundColor(const GraphNode& node) {
    if (node.cullReason) {
        return ImVec4(0.17f, 0.17f, 0.18f, 1.0f);
    }
    if (node.kind == GraphNodeKind::Sink) {
        return ImVec4(0.31f, 0.24f, 0.10f, 1.0f);
    }
    switch (node.passKind) {
    case render::PassKind::Raster:
        return ImVec4(0.13f, 0.21f, 0.33f, 1.0f);
    case render::PassKind::Compute:
        return ImVec4(0.22f, 0.16f, 0.33f, 1.0f);
    case render::PassKind::Copy:
        return ImVec4(0.13f, 0.28f, 0.24f, 1.0f);
    }
    return ImVec4(0.13f, 0.21f, 0.33f, 1.0f);
}

//======================================================================================================================
ImVec4 nodeAccentColor(const GraphNode& node) {
    if (node.cullReason) {
        return ImVec4(0.58f, 0.58f, 0.60f, 1.0f);
    }
    if (node.kind == GraphNodeKind::Sink) {
        return ImVec4(1.00f, 0.83f, 0.45f, 1.0f);
    }
    switch (node.passKind) {
    case render::PassKind::Raster:
        return ImVec4(0.58f, 0.76f, 1.00f, 1.0f);
    case render::PassKind::Compute:
        return ImVec4(0.80f, 0.66f, 1.00f, 1.0f);
    case render::PassKind::Copy:
        return ImVec4(0.55f, 0.94f, 0.80f, 1.0f);
    }
    return ImVec4(0.58f, 0.76f, 1.00f, 1.0f);
}

//======================================================================================================================
// A culled node's border is drawn dashed by hand, so the editor's own solid border is suppressed
// rather than left to sit underneath it.
ImVec4 nodeBorderColor(const GraphNode& node) {
    if (node.cullReason) {
        return ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const ImVec4 accent = nodeAccentColor(node);
    return ImVec4(accent.x * 0.7f, accent.y * 0.7f, accent.z * 0.7f, 0.9f);
}

//======================================================================================================================
std::string nodeHeaderText(const GraphNode& node) {
    if (node.kind == GraphNodeKind::Sink) {
        return std::format("sink -- {}", node.label);
    }
    std::string header = node.scheduleOrder ? std::format("#{} {}", *node.scheduleOrder, node.label)
                                            : std::format("{}", node.label);
    header += std::format(" -- {}", passKindLabel(node.passKind));
    if (node.gpuMilliseconds) {
        header += std::format(" -- {:.3f} ms", *node.gpuMilliseconds);
    }
    return header;
}

//======================================================================================================================
std::string pinText(const GraphNodePin& pin, bool isInput) {
    return isInput ? std::format("-> {}", pin.label) : std::format("{} ->", pin.label);
}

//======================================================================================================================
float widestPinColumn(const std::vector<GraphNodePin>& pins, bool isInput) {
    float widest = 0.0f;
    for (const GraphNodePin& pin : pins) {
        widest = std::max(widest, ImGui::CalcTextSize(pinText(pin, isInput).c_str()).x);
    }
    return widest;
}

//======================================================================================================================
// Emits `from` -> `to` as a run of short segments. The node editor draws links solid through its
// own path, so dashing -- what separates a memory handover and a dead node from a proved
// dependency -- has to be composed here.
void addDashedLine(ImDrawList* drawList, const ImVec2& from, const ImVec2& to, ImU32 color) {
    const ImVec2 delta{to.x - from.x, to.y - from.y};
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length <= 0.0f) {
        return;
    }
    const ImVec2 step{delta.x / length, delta.y / length};
    for (float travelled = 0.0f; travelled < length; travelled += kDashLength + kDashGap) {
        const float end = std::min(travelled + kDashLength, length);
        drawList->AddLine(ImVec2(from.x + step.x * travelled, from.y + step.y * travelled),
                          ImVec2(from.x + step.x * end, from.y + step.y * end), color,
                          kDashThickness);
    }
}

//======================================================================================================================
void addDashedRect(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 color) {
    addDashedLine(drawList, min, ImVec2(max.x, min.y), color);
    addDashedLine(drawList, ImVec2(max.x, min.y), max, color);
    addDashedLine(drawList, max, ImVec2(min.x, max.y), color);
    addDashedLine(drawList, ImVec2(min.x, max.y), min, color);
}

//======================================================================================================================
void drawNode(const GraphNode& node, uint32_t nodeIndex) {
    LMX_ASSERT(node.inputs.size() + node.outputs.size() < kPinIdStride,
               "a graph node declares more pins than its id block can hold");

    const std::string header = nodeHeaderText(node);
    const std::string cullLine = node.cullReason
                                     ? std::format("culled: {}", cullReasonLabel(*node.cullReason))
                                     : std::string();
    const float inputsWidth = widestPinColumn(node.inputs, true);
    const float outputsWidth = widestPinColumn(node.outputs, false);
    float bodyWidth = std::max(kNodeMinWidth, ImGui::CalcTextSize(header.c_str()).x);
    if (!cullLine.empty()) {
        bodyWidth = std::max(bodyWidth, ImGui::CalcTextSize(cullLine.c_str()).x);
    }
    if (outputsWidth > 0.0f) {
        bodyWidth = std::max(bodyWidth, inputsWidth + kPinColumnGap + outputsWidth);
    } else {
        bodyWidth = std::max(bodyWidth, inputsWidth);
    }

    ed::PushStyleColor(ed::StyleColor_NodeBg, nodeBackgroundColor(node));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, nodeBorderColor(node));
    ed::BeginNode(nodeIdOf(nodeIndex));

    // Inside the canvas, ImGui screen space and the editor's canvas space are the same plane, so
    // this origin is what lets the output column be placed against the node's right edge below.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(bodyWidth, 0.0f));
    ImGui::TextColored(nodeAccentColor(node), "%s", header.c_str());
    if (!cullLine.empty()) {
        ImGui::TextDisabled("%s", cullLine.c_str());
    }

    if (!node.inputs.empty() || !node.outputs.empty()) {
        const float pinsTop = ImGui::GetCursorScreenPos().y;
        ImGui::BeginGroup();
        for (uint32_t pin = 0; pin < node.inputs.size(); ++pin) {
            ed::BeginPin(pinIdOf(nodeIndex, pin), ed::PinKind::Input);
            ImGui::TextUnformatted(pinText(node.inputs[pin], true).c_str());
            ed::EndPin();
        }
        ImGui::EndGroup();

        if (!node.outputs.empty()) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + bodyWidth - outputsWidth, pinsTop));
            ImGui::BeginGroup();
            for (uint32_t pin = 0; pin < node.outputs.size(); ++pin) {
                const uint32_t ordinal = static_cast<uint32_t>(node.inputs.size()) + pin;
                ed::BeginPin(pinIdOf(nodeIndex, ordinal), ed::PinKind::Output);
                ImGui::TextUnformatted(pinText(node.outputs[pin], false).c_str());
                ed::EndPin();
            }
            ImGui::EndGroup();
        }
    }

    ed::EndNode();
    ed::PopStyleColor(2);
}

//======================================================================================================================
// The overlays the node editor has no path for: the arrowhead that gives a sink its endpoint
// silhouette, the dashed border of a culled node, and the dashed alias links. All three need node
// bounds, so they run once every node has been submitted this frame.
void drawNodeOverlays(const GraphNodeModel& model) {
    for (uint32_t index = 0; index < model.nodes.size(); ++index) {
        const GraphNode& node = model.nodes[index];
        if (node.kind != GraphNodeKind::Sink && !node.cullReason) {
            continue;
        }
        ImDrawList* drawList = ed::GetNodeBackgroundDrawList(nodeIdOf(index));
        if (drawList == nullptr) {
            continue;
        }
        const ImVec2 position = ed::GetNodePosition(nodeIdOf(index));
        const ImVec2 size = ed::GetNodeSize(nodeIdOf(index));
        if (node.cullReason) {
            addDashedRect(drawList, position, ImVec2(position.x + size.x, position.y + size.y),
                          kCulledBorderColor);
            continue;
        }
        const float middle = position.y + size.y * 0.5f;
        const ImVec4 accent = nodeAccentColor(node);
        drawList->AddTriangleFilled(ImVec2(position.x + size.x, middle - kSinkMarkerHalfHeight),
                                    ImVec2(position.x + size.x + kSinkMarkerLength, middle),
                                    ImVec2(position.x + size.x, middle + kSinkMarkerHalfHeight),
                                    ImColor(accent));
    }

    for (const GraphAliasLink& link : model.aliasLinks) {
        ImDrawList* drawList = ed::GetNodeBackgroundDrawList(nodeIdOf(link.fromNode));
        if (drawList == nullptr) {
            continue;
        }
        const ImVec2 fromPosition = ed::GetNodePosition(nodeIdOf(link.fromNode));
        const ImVec2 fromSize = ed::GetNodeSize(nodeIdOf(link.fromNode));
        const ImVec2 toPosition = ed::GetNodePosition(nodeIdOf(link.toNode));
        const ImVec2 toSize = ed::GetNodeSize(nodeIdOf(link.toNode));
        addDashedLine(
            drawList, ImVec2(fromPosition.x + fromSize.x * 0.5f, fromPosition.y + fromSize.y),
            ImVec2(toPosition.x + toSize.x * 0.5f, toPosition.y + toSize.y), kAliasLinkColor);
    }
}

//======================================================================================================================
void drawCanvas(const GraphNodeModel& model, RenderGraphPanelState& state, bool resetLayout) {
    ed::SetCurrentEditor(state.editor->context);
    ed::Begin("lmx.renderGraph", ImVec2(0.0f, 0.0f));

    // Declaration indices only mean something within one shape, so the positions the user dragged
    // and the node they selected are both surrendered the moment the drawn graph changes.
    const bool applyLayout = resetLayout || state.appliedSignature != model.shapeSignature;
    if (applyLayout) {
        for (uint32_t index = 0; index < model.nodes.size(); ++index) {
            ed::SetNodePosition(nodeIdOf(index),
                                ImVec2(model.nodes[index].x, model.nodes[index].y));
        }
    }

    for (uint32_t index = 0; index < model.nodes.size(); ++index) {
        drawNode(model.nodes[index], index);
    }

    for (uint32_t index = 0; index < model.edges.size(); ++index) {
        const GraphNodeEdge& edge = model.edges[index];
        const uint32_t fromOrdinal =
            static_cast<uint32_t>(model.nodes[edge.fromNode].inputs.size()) + edge.fromPin;
        ed::Link(ed::LinkId(static_cast<uintptr_t>(index) + 1), pinIdOf(edge.fromNode, fromOrdinal),
                 pinIdOf(edge.toNode, edge.toPin), kEdgeColor, kEdgeThickness);
    }

    drawNodeOverlays(model);

    if (applyLayout) {
        // Node bounds are current by now, which is what makes fitting to content meaningful on the
        // very first frame a shape is drawn.
        ed::ClearSelection();
        ed::NavigateToContent(0.0f);
        state.appliedSignature = model.shapeSignature;
        state.selectedNode.reset();
    }

    ed::End();

    // Read after End(): that is where this frame's input is turned into the editor's selection.
    ed::NodeId selected[2] = {};
    const int selectedCount = ed::GetSelectedNodes(selected, 2);
    if (selectedCount == 1 && selected[0].Get() >= 1 &&
        selected[0].Get() - 1 < model.nodes.size()) {
        state.selectedNode = static_cast<uint32_t>(selected[0].Get() - 1);
    } else {
        state.selectedNode.reset();
    }

    ed::SetCurrentEditor(nullptr);
}

//======================================================================================================================
void drawUseRows(const std::vector<GraphInspectorUseRow>& uses) {
    if (uses.empty()) {
        ImGui::TextDisabled("none");
        return;
    }
    for (const GraphInspectorUseRow& use : uses) {
        std::string line = std::format("{} r{} \"{}\" v{}", render::roleName(use.role),
                                       use.resource, use.resourceName, use.version);
        if (!use.rangeText.empty()) {
            line += std::format(" {}", use.rangeText);
        }
        ImGui::TextUnformatted(line.c_str());
    }
}

//======================================================================================================================
void drawTransientTotals(const render::TransientMemory& memory) {
    ImGui::Text("requested %llu B", static_cast<unsigned long long>(memory.requested));
    ImGui::Text("high-water %llu B", static_cast<unsigned long long>(memory.highWater));
    ImGui::Text("saved %llu B", static_cast<unsigned long long>(memory.aliasSavings));
}

//======================================================================================================================
void drawSinkDetails(const GraphNodeModel& model, const GraphNode& node) {
    ImGui::SeparatorText("Sink");
    ImGui::TextUnformatted(std::format("s{} {}", node.index, node.label).c_str());
    if (!node.inputs.empty()) {
        const GraphNodePin& rooted = node.inputs.front();
        ImGui::TextUnformatted(std::format("roots r{} \"{}\" v{}", rooted.resource,
                                           rooted.resourceName, rooted.version)
                                   .c_str());
    }
    ImGui::SeparatorText("Produced by");
    if (!node.producerPass || *node.producerPass >= model.nodes.size()) {
        ImGui::TextDisabled("none");
        return;
    }
    const GraphNode& producer = model.nodes[*node.producerPass];
    std::string line =
        producer.scheduleOrder
            ? std::format("#{} p{} {} \"{}\"", *producer.scheduleOrder, producer.index,
                          passKindLabel(producer.passKind), producer.label)
            : std::format("p{} {} \"{}\"", producer.index, passKindLabel(producer.passKind),
                          producer.label);
    ImGui::TextUnformatted(line.c_str());
}

//======================================================================================================================
void drawPassDetails(const GraphNode& node) {
    if (node.cullReason) {
        ImGui::SeparatorText("Culled pass");
        ImGui::TextUnformatted(
            std::format("p{} {} \"{}\"", node.index, passKindLabel(node.passKind), node.label)
                .c_str());
        ImGui::TextDisabled(
            "%s",
            std::format("culled: {} -- never executed", cullReasonLabel(*node.cullReason)).c_str());
        ImGui::SeparatorText("Declared uses");
        drawUseRows(node.uses);
        return;
    }

    ImGui::SeparatorText("Pass");
    std::string header =
        node.scheduleOrder
            ? std::format("#{} p{} {} \"{}\"", *node.scheduleOrder, node.index,
                          passKindLabel(node.passKind), node.label)
            : std::format("p{} {} \"{}\"", node.index, passKindLabel(node.passKind), node.label);
    ImGui::TextUnformatted(header.c_str());
    if (node.gpuMilliseconds) {
        ImGui::Text("GPU %.3f ms", *node.gpuMilliseconds);
    } else {
        ImGui::TextDisabled("GPU unmeasured");
    }

    ImGui::SeparatorText("Uses");
    drawUseRows(node.uses);

    ImGui::SeparatorText("Barriers before");
    if (node.barriersBefore.empty()) {
        ImGui::TextDisabled("none");
    }
    for (const GraphInspectorTransitionRow& barrier : node.barriersBefore) {
        if (barrier.aliasedFrom) {
            ImGui::Text("%s alias-of r%u", barrier.description.c_str(), *barrier.aliasedFrom);
        } else {
            ImGui::TextUnformatted(barrier.description.c_str());
        }
    }

    ImGui::SeparatorText("Transients alive");
    if (node.transientsAlive.empty()) {
        ImGui::TextDisabled("none");
    }
    for (const GraphNodeTransientSpan& transient : node.transientsAlive) {
        ImGui::Text("r%u \"%s\" offset %llu size %llu align %llu%s", transient.resource,
                    transient.resourceName.c_str(),
                    static_cast<unsigned long long>(transient.offset),
                    static_cast<unsigned long long>(transient.size),
                    static_cast<unsigned long long>(transient.alignment),
                    transient.aliases ? " aliased" : "");
    }
}

//======================================================================================================================
void drawDetails(const GraphNodeModel& model, std::optional<uint32_t> selectedNode) {
    if (!selectedNode || *selectedNode >= model.nodes.size()) {
        ImGui::TextDisabled("no node selected -- click a node to inspect what it declared");
        ImGui::SeparatorText("Transients");
        drawTransientTotals(model.memory);
        return;
    }
    const GraphNode& node = model.nodes[*selectedNode];
    if (node.kind == GraphNodeKind::Sink) {
        drawSinkDetails(model, node);
    } else {
        drawPassDetails(node);
    }
}

//======================================================================================================================
void dumpFrame(const render::CompiledFrameRecord& record, uint64_t frameId) {
    const std::string filename = std::format("graph-dump-frame-{}.txt", frameId);
    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    if (file) {
        file << render::dumpCompiledFrame(record);
        LMX_LOG_INFO("render-graph frame {} dumped to '{}'", frameId,
                     std::filesystem::absolute(filename).string());
    } else {
        LMX_LOG_ERROR("Render Graph panel: cannot open '{}' for writing", filename);
    }
}

} // namespace

//======================================================================================================================
void NodeEditorHandleDeleter::operator()(NodeEditorHandle* handle) const {
    if (handle == nullptr) {
        return;
    }
    if (handle->context != nullptr) {
        ed::DestroyEditor(handle->context);
    }
    delete handle;
}

//======================================================================================================================
void releaseRenderGraphPanelState(RenderGraphPanelState& state) {
    state.editor.reset();
    state.appliedSignature.clear();
    state.selectedNode.reset();
}

//======================================================================================================================
void drawRenderGraphPanel(bool& open, RenderGraphPanelState& state,
                          const FrameRecordRing& frameRecords) {
    if (!ImGui::Begin(kRenderGraphPanelWindowName, &open)) {
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

    ImGui::Text("frame %llu -- pooling %s", static_cast<unsigned long long>(model.frameId),
                model.poolingEnabled ? "on" : "off");
    ImGui::SameLine();
    if (ImGui::Button("Dump frame")) {
        dumpFrame(newest->record, model.frameId);
    }
    ImGui::SameLine();
    const bool resetLayout = ImGui::Button("Reset Layout");
    ImGui::Text("transients: requested %llu B, high-water %llu B, saved %llu B",
                static_cast<unsigned long long>(model.memory.requested),
                static_cast<unsigned long long>(model.memory.highWater),
                static_cast<unsigned long long>(model.memory.aliasSavings));
    ImGui::Separator();

    if (!state.editor) {
        ed::Config config;
        // No settings file and no save callbacks: node positions, zoom, and pan are session state,
        // and Luminex's own imgui.ini workspace schema stays the panel's only persisted layout.
        config.SettingsFile = nullptr;
        state.editor.reset(new NodeEditorHandle{ed::CreateEditor(&config)});
        LMX_ASSERT(state.editor->context != nullptr, "ed::CreateEditor returned a null context");
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float canvasWidth = available.x * kCanvasWidthFraction;
    if (available.x - canvasWidth - spacing < kDetailsMinWidth) {
        canvasWidth = std::max(kCanvasMinWidth, available.x - kDetailsMinWidth - spacing);
    }

    if (ImGui::BeginChild("RenderGraphCanvas", ImVec2(canvasWidth, available.y),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        drawCanvas(model, state, resetLayout);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("RenderGraphDetails", ImVec2(0.0f, available.y),
                          ImGuiChildFlags_Borders)) {
        drawDetails(model, state.selectedNode);
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace lmx::app
