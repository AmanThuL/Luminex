//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphPanel.cpp
/// @brief Implements the Render Graph panel as a node canvas over one exact retired compiled frame.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/RenderGraphPanel.h"

#include "App/GraphInspectorModel.h"
#include "App/GraphLayout.h"
#include "App/GraphNodeModel.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "Render/GraphDump.h"

#include <imgui.h>
// Included here and nowhere else, so no Luminex header ever exposes an ax::NodeEditor type. The
// node editor's public header does not pull in imgui_internal.h -- only its own internal headers
// do, and this file includes none of them -- so Dear ImGui's internal surface stays out of reach
// here too.
#include <imgui_node_editor.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
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

/// Size the detached Render Graph window takes the first time it is ever opened, in ImGui points.
constexpr float kDetachedWidth = 1280.0f;
constexpr float kDetachedHeight = 800.0f;

/// Gap left between the main window's work area and the detached window's first-open position.
constexpr float kDetachedMargin = 16.0f;

/// Docking class of the Render Graph window. Any non-zero value distinguishes it from the unclassed
/// panels; it is a literal rather than ImHashStr("...") because that lives in imgui_internal.h,
/// which this panel deliberately does not include.
constexpr ImGuiID kRenderGraphWindowClassId = 0x6C6D7867u; // 'lmxg'

/// Narrowest a node body may be, so a short-labelled pass still reads as a box.
constexpr float kNodeMinWidth = 150.0f;

/// Horizontal gap held between a node's input and output pin columns.
constexpr float kPinColumnGap = 24.0f;

/// Radius of the dot that stands for a pin, in canvas units.
constexpr float kPinDotRadius = 5.0f;

/// Bounds the column count control accepts, and the width it is drawn at.
constexpr int kMinColumns = 1;
constexpr int kMaxColumns = 32;
constexpr float kColumnsControlWidth = 110.0f;

/// Drawn and skipped run lengths of a dashed segment, in canvas units.
constexpr float kDashLength = 6.0f;
constexpr float kDashGap = 5.0f;

/// Half-height and length of the arrowhead that marks a sink node as an endpoint.
constexpr float kSinkMarkerHalfHeight = 9.0f;
constexpr float kSinkMarkerLength = 11.0f;

/// Pin ordinals are packed into an item's id block of this size, which caps the pins one item may
/// declare before two items would collide in the id space.
constexpr uint32_t kPinIdStride = 1000;

/// First id a group item may take. Node items keep their node index below it, so the two kinds
/// share one id space without either having to know the other's count.
constexpr uintptr_t kGroupIdBase = 100000;

/// Solid execution edges, and the dashed overlays drawn outside the node editor's link path.
constexpr float kEdgeThickness = 1.7f;
constexpr float kDashThickness = 1.4f;
const ImVec4 kEdgeColor{0.62f, 0.70f, 0.80f, 1.0f};
constexpr ImU32 kAliasLinkColor = IM_COL32(214, 122, 196, 220);
constexpr ImU32 kCulledBorderColor = IM_COL32(140, 140, 146, 220);

/// A collapsed stage's fill and accent, and the band drawn behind its header. They are deliberately
/// unlike any pass kind's colours: a group is not one declaration but several folded together.
const ImVec4 kGroupBackgroundColor{0.19f, 0.19f, 0.24f, 1.0f};
const ImVec4 kGroupAccentColor{0.92f, 0.86f, 0.66f, 1.0f};
constexpr ImU32 kGroupHeaderBandColor = IM_COL32(255, 255, 255, 28);

/// What a culled item is drawn in, whether it is one dead pass or a whole dead stage.
const ImVec4 kCulledBackgroundColor{0.17f, 0.17f, 0.18f, 1.0f};
const ImVec4 kCulledAccentColor{0.58f, 0.58f, 0.60f, 1.0f};

/// The three colours one canvas item is drawn in.
struct ItemStyle {
    ImVec4 background;
    ImVec4 accent;
    ImVec4 border;
};

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
// A node item keeps the identity of the node it draws and a group item takes an id above every node
// index, so one id space carries both and a group can never be mistaken for a node.
uintptr_t itemIdValue(const GraphLayout& layout, uint32_t itemIndex) {
    const GraphLayoutItem& item = layout.items[itemIndex];
    return item.kind == GraphLayoutItemKind::Group ? kGroupIdBase + item.index + 1 : item.index + 1;
}

//======================================================================================================================
ed::NodeId itemNodeId(const GraphLayout& layout, uint32_t itemIndex) {
    return ed::NodeId(itemIdValue(layout, itemIndex));
}

//======================================================================================================================
// Inputs occupy the low ordinals of an item's block and outputs the rest, so a pin's identity is
// stable for as long as the layout signature that produced it is.
ed::PinId pinIdOf(uintptr_t itemId, uint32_t pinOrdinal) {
    return ed::PinId(itemId * kPinIdStride + pinOrdinal + 1);
}

//======================================================================================================================
// The inverse of itemIdValue(): what the editor hands back is an id, and everything the panel does
// with a selection or a double-click is expressed over items.
std::optional<uint32_t> itemOfCanvasId(const GraphLayout& layout, uintptr_t canvasId) {
    if (canvasId == 0) {
        return std::nullopt;
    }
    if (canvasId <= kGroupIdBase) {
        const uintptr_t nodeIndex = canvasId - 1;
        if (nodeIndex >= layout.itemOfNode.size()) {
            return std::nullopt;
        }
        return layout.itemOfNode[static_cast<uint32_t>(nodeIndex)];
    }
    const uintptr_t groupIndex = canvasId - kGroupIdBase - 1;
    for (uint32_t index = 0; index < layout.items.size(); ++index) {
        const GraphLayoutItem& item = layout.items[index];
        if (item.kind == GraphLayoutItemKind::Group && item.index == groupIndex) {
            return index;
        }
    }
    return std::nullopt;
}

//======================================================================================================================
// The node body's fill, chosen so the declaration path a pass took is readable at a glance and a
// culled pass reads as inert next to the graph that was proved.
ImVec4 nodeBackgroundColor(const GraphNode& node) {
    if (node.cullReason) {
        return kCulledBackgroundColor;
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
        return kCulledAccentColor;
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
// A culled item's border is drawn dashed by hand, so the editor's own solid border is suppressed
// rather than left to sit underneath it.
ItemStyle itemStyleOf(const GraphNodeModel& model, const GraphLayoutItem& item) {
    ItemStyle style;
    if (item.kind == GraphLayoutItemKind::Group) {
        style.background = item.culled ? kCulledBackgroundColor : kGroupBackgroundColor;
        style.accent = item.culled ? kCulledAccentColor : kGroupAccentColor;
    } else {
        style.background = nodeBackgroundColor(model.nodes[item.index]);
        style.accent = nodeAccentColor(model.nodes[item.index]);
    }
    style.border = item.culled ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
                               : ImVec4(style.accent.x * 0.7f, style.accent.y * 0.7f,
                                        style.accent.z * 0.7f, 0.9f);
    return style;
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
// A collapsed stage says what it stands for and what it cost. The measured count appears only when
// it differs from the member count, so a fully measured stage is not made to look partial.
std::string groupHeaderText(const GraphLayoutGroup& group) {
    std::string header = std::format("[+] {} -- {} passes", group.stage, group.members.size());
    if (!group.gpuMillisecondsSum) {
        return header + " -- unmeasured";
    }
    header += std::format(" -- {:.3f} ms", *group.gpuMillisecondsSum);
    if (group.measuredMembers < group.members.size()) {
        header += std::format(" ({} measured)", group.measuredMembers);
    }
    return header;
}

//======================================================================================================================
std::string itemCullLine(const GraphNodeModel& model, const GraphLayoutItem& item) {
    if (!item.culled) {
        return {};
    }
    if (item.kind == GraphLayoutItemKind::Group) {
        return "culled -- never executed";
    }
    return std::format("culled: {}", cullReasonLabel(*model.nodes[item.index].cullReason));
}

//======================================================================================================================
const std::string& pinText(const GraphLayoutPin& pin, bool fullLabel) {
    return fullLabel ? pin.label : pin.shortLabel;
}

//======================================================================================================================
float widestPinColumn(const std::vector<GraphLayoutPin>& pins, bool fullLabels) {
    const float dotWidth = kPinDotRadius * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    float widest = 0.0f;
    for (const GraphLayoutPin& pin : pins) {
        widest =
            std::max(widest, dotWidth + ImGui::CalcTextSize(pinText(pin, fullLabels).c_str()).x);
    }
    return widest;
}

//======================================================================================================================
// The dot the compact pin style is built on: an item-sized blank the layout can align against, with
// the mark drawn into it by hand because a text glyph would not scale with the canvas.
void drawPinDot(const ImVec4& accent) {
    const float height = ImGui::GetTextLineHeight();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(cursor.x + kPinDotRadius, cursor.y + height * 0.5f), kPinDotRadius, ImColor(accent));
    ImGui::Dummy(ImVec2(kPinDotRadius * 2.0f, height));
}

//======================================================================================================================
// A pin reads outwards: an input's dot leads its label and an output's label leads its dot, so the
// two columns point the way the versions travel. The full label the short one abbreviates is what
// a hover reports; the caller shows it once, after every node has been submitted.
void drawPin(const GraphLayoutPin& pin, ed::PinId id, ed::PinKind kind, const ImVec4& accent,
             bool fullLabel, std::string& hoveredLabel) {
    ed::BeginPin(id, kind);
    if (kind == ed::PinKind::Input) {
        drawPinDot(accent);
        ImGui::SameLine();
        ImGui::TextUnformatted(pinText(pin, fullLabel).c_str());
    } else {
        ImGui::TextUnformatted(pinText(pin, fullLabel).c_str());
        ImGui::SameLine();
        drawPinDot(accent);
    }
    ed::EndPin();
    if (ImGui::IsItemHovered()) {
        hoveredLabel = pin.label;
    }
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
// One box, whether it draws one declaration or a folded stage. Its width is measured from the text
// this frame actually puts in it, so expanding a selection's labels widens exactly those boxes.
void drawItem(const GraphNodeModel& model, const GraphLayout& layout, uint32_t itemIndex,
              bool fullLabels, std::string& hoveredLabel) {
    const GraphLayoutItem& item = layout.items[itemIndex];
    LMX_ASSERT(item.inputs.size() + item.outputs.size() < kPinIdStride,
               "a canvas item declares more pins than its id block can hold");

    const bool isGroup = item.kind == GraphLayoutItemKind::Group;
    const ItemStyle style = itemStyleOf(model, item);
    const std::string header = isGroup ? groupHeaderText(layout.groups[item.index])
                                       : nodeHeaderText(model.nodes[item.index]);
    const std::string cullLine = itemCullLine(model, item);
    const float inputsWidth = widestPinColumn(item.inputs, fullLabels);
    const float outputsWidth = widestPinColumn(item.outputs, fullLabels);
    float bodyWidth = std::max(kNodeMinWidth, ImGui::CalcTextSize(header.c_str()).x);
    if (!cullLine.empty()) {
        bodyWidth = std::max(bodyWidth, ImGui::CalcTextSize(cullLine.c_str()).x);
    }
    if (outputsWidth > 0.0f) {
        bodyWidth = std::max(bodyWidth, inputsWidth + kPinColumnGap + outputsWidth);
    } else {
        bodyWidth = std::max(bodyWidth, inputsWidth);
    }

    const uintptr_t itemId = itemIdValue(layout, itemIndex);
    ed::PushStyleColor(ed::StyleColor_NodeBg, style.background);
    ed::PushStyleColor(ed::StyleColor_NodeBorder, style.border);
    ed::BeginNode(ed::NodeId(itemId));

    // Inside the canvas, ImGui screen space and the editor's canvas space are the same plane, so
    // this origin is what lets the output column be placed against the node's right edge below.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(bodyWidth, 0.0f));
    if (isGroup) {
        // A band behind the header is what separates a stage standing in for several passes from a
        // single one, before any of the text is read.
        const ImVec2 band = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            band, ImVec2(band.x + bodyWidth, band.y + ImGui::GetTextLineHeight()),
            kGroupHeaderBandColor);
    }
    ImGui::TextColored(style.accent, "%s", header.c_str());
    if (!cullLine.empty()) {
        ImGui::TextDisabled("%s", cullLine.c_str());
    }

    if (!item.inputs.empty() || !item.outputs.empty()) {
        const float pinsTop = ImGui::GetCursorScreenPos().y;
        ImGui::BeginGroup();
        for (uint32_t pin = 0; pin < item.inputs.size(); ++pin) {
            drawPin(item.inputs[pin], pinIdOf(itemId, pin), ed::PinKind::Input, style.accent,
                    fullLabels, hoveredLabel);
        }
        ImGui::EndGroup();

        if (!item.outputs.empty()) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + bodyWidth - outputsWidth, pinsTop));
            ImGui::BeginGroup();
            for (uint32_t pin = 0; pin < item.outputs.size(); ++pin) {
                const uint32_t ordinal = static_cast<uint32_t>(item.inputs.size()) + pin;
                drawPin(item.outputs[pin], pinIdOf(itemId, ordinal), ed::PinKind::Output,
                        style.accent, fullLabels, hoveredLabel);
            }
            ImGui::EndGroup();
        }
    }

    ed::EndNode();
    ed::PopStyleColor(2);
}

//======================================================================================================================
// The overlays the node editor has no path for: the arrowhead that gives a sink its endpoint
// silhouette, the dashed border of a culled item, and the dashed alias links. All three need item
// bounds, so they run once every item has been submitted this frame.
void drawItemOverlays(const GraphNodeModel& model, const GraphLayout& layout) {
    for (uint32_t index = 0; index < layout.items.size(); ++index) {
        const GraphLayoutItem& item = layout.items[index];
        const bool isSink = item.kind == GraphLayoutItemKind::Node &&
                            model.nodes[item.index].kind == GraphNodeKind::Sink;
        if (!isSink && !item.culled) {
            continue;
        }
        const ed::NodeId id = itemNodeId(layout, index);
        ImDrawList* drawList = ed::GetNodeBackgroundDrawList(id);
        if (drawList == nullptr) {
            continue;
        }
        const ImVec2 position = ed::GetNodePosition(id);
        const ImVec2 size = ed::GetNodeSize(id);
        if (item.culled) {
            addDashedRect(drawList, position, ImVec2(position.x + size.x, position.y + size.y),
                          kCulledBorderColor);
            continue;
        }
        const float middle = position.y + size.y * 0.5f;
        const ImVec4 accent = nodeAccentColor(model.nodes[item.index]);
        drawList->AddTriangleFilled(ImVec2(position.x + size.x, middle - kSinkMarkerHalfHeight),
                                    ImVec2(position.x + size.x + kSinkMarkerLength, middle),
                                    ImVec2(position.x + size.x, middle + kSinkMarkerHalfHeight),
                                    ImColor(accent));
    }

    for (const GraphLayoutAliasLink& link : layout.aliasLinks) {
        const ed::NodeId fromId = itemNodeId(layout, link.fromItem);
        const ed::NodeId toId = itemNodeId(layout, link.toItem);
        ImDrawList* drawList = ed::GetNodeBackgroundDrawList(fromId);
        if (drawList == nullptr) {
            continue;
        }
        const ImVec2 fromPosition = ed::GetNodePosition(fromId);
        const ImVec2 fromSize = ed::GetNodeSize(fromId);
        const ImVec2 toPosition = ed::GetNodePosition(toId);
        const ImVec2 toSize = ed::GetNodeSize(toId);
        addDashedLine(
            drawList, ImVec2(fromPosition.x + fromSize.x * 0.5f, fromPosition.y + fromSize.y),
            ImVec2(toPosition.x + toSize.x * 0.5f, toPosition.y + toSize.y), kAliasLinkColor);
    }
}

//======================================================================================================================
void toggleGroupExpansion(GraphLayoutOptions& options, const std::string& key) {
    const auto found = std::ranges::find(options.expandedGroups, key);
    if (found == options.expandedGroups.end()) {
        options.expandedGroups.push_back(key);
    } else {
        options.expandedGroups.erase(found);
    }
}

//======================================================================================================================
// A double-click is the one gesture that changes how much of a stage is drawn: on a folded stage it
// opens it, and on any member of an open one it folds that stage back up.
void applyDoubleClick(const GraphLayout& layout, GraphLayoutOptions& options) {
    const std::optional<uint32_t> itemIndex =
        itemOfCanvasId(layout, ed::GetDoubleClickedNode().Get());
    if (!itemIndex) {
        return;
    }
    const GraphLayoutItem& item = layout.items[*itemIndex];
    if (item.kind == GraphLayoutItemKind::Group) {
        toggleGroupExpansion(options, layout.groups[item.index].key);
        return;
    }
    for (const GraphLayoutGroup& group : layout.groups) {
        if (group.expanded && std::ranges::find(group.members, item.index) != group.members.end()) {
            toggleGroupExpansion(options, group.key);
            return;
        }
    }
}

//======================================================================================================================
// The items whose pins spell their versions out in full: the selected one and everything a visible
// edge joins it to, which is exactly the neighbourhood a reader is checking when they select a box.
std::vector<bool> fullLabelItems(const GraphLayout& layout, std::optional<uint32_t> selectedItem) {
    std::vector<bool> full(layout.items.size(), false);
    if (!selectedItem || *selectedItem >= layout.items.size()) {
        return full;
    }
    full[*selectedItem] = true;
    for (const GraphLayoutEdge& edge : layout.edges) {
        if (edge.fromItem == *selectedItem) {
            full[edge.toItem] = true;
        }
        if (edge.toItem == *selectedItem) {
            full[edge.fromItem] = true;
        }
    }
    return full;
}

//======================================================================================================================
void drawCanvas(const GraphNodeModel& model, const GraphLayout& layout,
                RenderGraphPanelState& state, bool resetLayout) {
    LMX_ASSERT(model.nodes.size() <= kGroupIdBase,
               "a compiled frame declares more nodes than the canvas id space holds");

    ed::SetCurrentEditor(state.editor->context);
    ed::Begin("lmx.renderGraph", ImVec2(0.0f, 0.0f));

    // Item indices only mean something within one picture, so the positions the user dragged and
    // the item they selected are both surrendered the moment the drawn picture changes.
    const bool applyLayout = resetLayout || state.appliedSignature != layout.signature;
    if (applyLayout) {
        for (uint32_t index = 0; index < layout.items.size(); ++index) {
            ed::SetNodePosition(itemNodeId(layout, index),
                                ImVec2(layout.items[index].x, layout.items[index].y));
        }
    }

    const std::vector<bool> fullLabels = fullLabelItems(layout, state.selectedItem);
    std::string hoveredLabel;
    for (uint32_t index = 0; index < layout.items.size(); ++index) {
        drawItem(model, layout, index, fullLabels[index], hoveredLabel);
    }

    for (uint32_t index = 0; index < layout.edges.size(); ++index) {
        const GraphLayoutEdge& edge = layout.edges[index];
        const uintptr_t fromId = itemIdValue(layout, edge.fromItem);
        const uint32_t fromOrdinal =
            static_cast<uint32_t>(layout.items[edge.fromItem].inputs.size()) + edge.fromPin;
        ed::Link(ed::LinkId(static_cast<uintptr_t>(index) + 1), pinIdOf(fromId, fromOrdinal),
                 pinIdOf(itemIdValue(layout, edge.toItem), edge.toPin), kEdgeColor, kEdgeThickness);
    }

    drawItemOverlays(model, layout);

    if (!hoveredLabel.empty()) {
        // A tooltip is an ImGui window, and the canvas is a transformed space: suspending it is
        // what puts the tooltip back in the screen space the mouse is actually in.
        ed::Suspend();
        ImGui::SetTooltip("%s", hoveredLabel.c_str());
        ed::Resume();
    }

    if (applyLayout) {
        // Item bounds are current by now, which is what makes fitting to content meaningful on the
        // very first frame a picture is drawn.
        ed::ClearSelection();
        ed::NavigateToContent(0.0f);
        state.appliedSignature = layout.signature;
        state.selectedItem.reset();
    }

    ed::End();

    // Read after End(): that is where this frame's input is turned into the editor's selection and
    // its double-click.
    ed::NodeId selected[2] = {};
    const int selectedCount = ed::GetSelectedNodes(selected, 2);
    state.selectedItem =
        selectedCount == 1 ? itemOfCanvasId(layout, selected[0].Get()) : std::nullopt;
    applyDoubleClick(layout, state.layoutOptions);

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
// A stage answers for its members: what it is keyed by, what it cost, and which passes it folded
// away. The button is the same act as a double-click on the canvas, for a reader who is here.
void drawGroupDetails(const GraphNodeModel& model, const GraphLayoutGroup& group,
                      GraphLayoutOptions& options) {
    ImGui::SeparatorText("Stage group");
    ImGui::TextUnformatted(group.key.c_str());
    ImGui::Text("%zu passes", group.members.size());
    if (group.gpuMillisecondsSum) {
        ImGui::Text("GPU %.3f ms over %u measured", *group.gpuMillisecondsSum,
                    group.measuredMembers);
    } else {
        ImGui::TextDisabled("GPU unmeasured");
    }
    if (ImGui::Button(group.expanded ? "Collapse" : "Expand")) {
        toggleGroupExpansion(options, group.key);
    }

    ImGui::SeparatorText("Members");
    if (!ImGui::BeginTable("members", 3,
                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        return;
    }
    ImGui::TableSetupColumn(group.culled ? "culled" : "#");
    ImGui::TableSetupColumn("pass");
    ImGui::TableSetupColumn("GPU ms");
    ImGui::TableHeadersRow();
    for (const uint32_t member : group.members) {
        const GraphNode& node = model.nodes[member];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (node.cullReason) {
            const std::string_view reason = cullReasonLabel(*node.cullReason);
            ImGui::TextDisabled("%.*s", static_cast<int>(reason.size()), reason.data());
        } else if (node.scheduleOrder) {
            ImGui::Text("#%u", *node.scheduleOrder);
        } else {
            ImGui::TextDisabled("--");
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(node.label.c_str());
        ImGui::TableNextColumn();
        if (node.gpuMilliseconds) {
            ImGui::Text("%.3f", *node.gpuMilliseconds);
        } else {
            ImGui::TextDisabled("--");
        }
    }
    ImGui::EndTable();
}

//======================================================================================================================
void drawDetails(const GraphNodeModel& model, const GraphLayout& layout,
                 RenderGraphPanelState& state) {
    if (!state.selectedItem || *state.selectedItem >= layout.items.size()) {
        ImGui::TextDisabled("nothing selected -- click a node or a stage to inspect it");
        ImGui::SeparatorText("Transients");
        drawTransientTotals(model.memory);
        return;
    }
    const GraphLayoutItem& item = layout.items[*state.selectedItem];
    if (item.kind == GraphLayoutItemKind::Group) {
        drawGroupDetails(model, layout.groups[item.index], state.layoutOptions);
        return;
    }
    const GraphNode& node = model.nodes[item.index];
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

//======================================================================================================================
// How many columns a row holds by default: as many as the canvas is wide enough to show, and never
// fewer than two, because a single column is a list rather than a graph.
uint32_t autoColumnCount(float canvasWidth) {
    const float columns = std::floor(canvasWidth / kGraphLayoutColumnSpacing);
    return std::max(2u, static_cast<uint32_t>(std::max(columns, 0.0f)));
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
    state.layoutOptions = {};
    state.appliedSignature.clear();
    state.selectedItem.reset();
}

//======================================================================================================================
void drawRenderGraphPanel(bool& open, RenderGraphPanelState& state,
                          const FrameRecordRing& frameRecords) {
    // A class of its own, refusing unclassed dock targets and overriding the viewport into
    // NoAutoMerge: together those keep this panel out of the main dockspace and out of the main
    // OS window, so an open Render Graph always has its own window to be large in.
    static const ImGuiWindowClass windowClass = [] {
        ImGuiWindowClass created;
        created.ClassId = kRenderGraphWindowClassId;
        created.DockingAllowUnclassed = false;
        created.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        return created;
    }();
    ImGui::SetNextWindowClass(&windowClass);
    // First open only; afterwards imgui.ini carries whatever the user left. Placing it just right
    // of the main window's work area is a hint, not a guarantee -- a main window that fills the
    // display leaves the OS to clamp the position back on screen.
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({mainViewport->WorkPos.x + mainViewport->WorkSize.x + kDetachedMargin,
                             mainViewport->WorkPos.y},
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({kDetachedWidth, kDetachedHeight}, ImGuiCond_FirstUseEver);

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
    // Measured before the header row, which changes the height left for the children but not the
    // width, so the column count and the canvas child are sized from the same number.
    const float canvasWidth = canvasWidthFor(ImGui::GetContentRegionAvail().x);

    ImGui::Text("frame %llu -- pooling %s", static_cast<unsigned long long>(model.frameId),
                model.poolingEnabled ? "on" : "off");
    ImGui::SameLine();
    if (ImGui::Button("Dump frame")) {
        dumpFrame(newest->record, model.frameId);
    }
    ImGui::SameLine();
    const bool resetLayout = ImGui::Button("Reset Layout");
    if (resetLayout || state.appliedSignature.empty()) {
        state.layoutOptions.columnsPerRow = autoColumnCount(canvasWidth);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kColumnsControlWidth);
    int columns = static_cast<int>(state.layoutOptions.columnsPerRow);
    if (ImGui::InputInt("columns", &columns)) {
        state.layoutOptions.columnsPerRow =
            static_cast<uint32_t>(std::clamp(columns, kMinColumns, kMaxColumns));
    }
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

    const GraphLayout layout = layoutGraph(model, state.layoutOptions);
    const float availableHeight = ImGui::GetContentRegionAvail().y;

    if (ImGui::BeginChild("RenderGraphCanvas", ImVec2(canvasWidth, availableHeight),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        drawCanvas(model, layout, state, resetLayout);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("RenderGraphDetails", ImVec2(0.0f, availableHeight),
                          ImGuiChildFlags_Borders)) {
        drawDetails(model, layout, state);
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace lmx::app
