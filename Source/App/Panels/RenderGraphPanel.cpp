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
#include <array>
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

/// Docking class of the Render Graph window. Any non-zero value distinguishes it from the unclassed
/// panels; it is a literal rather than ImHashStr("...") because that lives in imgui_internal.h,
/// which this panel deliberately does not include.
constexpr ImGuiID kRenderGraphWindowClassId = 0x6C6D7867u; // 'lmxg'

/// Narrowest a card may be, so a short-labelled pass still reads as a box.
constexpr float kCardMinWidth = 170.0f;

/// Horizontal gap held between a card's input and output pin columns.
constexpr float kColumnInnerGap = 32.0f;

/// Radius of the dot that stands for a pin, and the gap between that dot and its label.
constexpr float kPinDotRadius = 5.0f;
constexpr float kPinLabelGap = 6.0f;

/// Inner padding a card applies to its own title band and body. The node editor's node padding is
/// pushed to zero so the title band can reach the card's rounded edge and a pin dot can sit on it,
/// which leaves the card to pad itself.
constexpr float kTitlePadX = 10.0f;
constexpr float kTitlePadY = 4.0f;
constexpr float kTitleGap = 16.0f;
constexpr float kBodyPadY = 6.0f;
constexpr float kBodyBottomPad = 6.0f;

/// Corner radius the editor rounds a card with, matched by the title band's top corners.
constexpr float kCardRounding = 6.0f;

/// Measured placement, in canvas units: between two columns of cards, between two ranks of one row,
/// between two rows, and between the last row and the culled band. Pixels are decided here rather
/// than in `GraphLayout` because only this side can measure the text a card has to hold.
constexpr float kColumnGap = 120.0f;
constexpr float kRankGap = 40.0f;
constexpr float kRowGap = 100.0f;
constexpr float kCulledBandGap = 140.0f;

/// The coarse grid a picture is drawn at for the one frame before its cards have been measured.
/// Nothing is read off it but the sizes the cards report afterwards, so it only has to be roomy.
constexpr float kProvisionalColumnPitch = 600.0f;
constexpr float kProvisionalRankPitch = 200.0f;
constexpr float kProvisionalRowPitch = 1200.0f;

/// Share of the view a chain too long to fit is opened filling. The leading columns are zoomed to
/// this much of the visible width, so the head of the graph reads large while the tail waits behind
/// a pan, rather than the whole chain shrinking until none of it can be read.
constexpr float kLeadingViewFill = 0.9f;

/// Bounds the column-count control accepts, and the width it is drawn at. Zero is the default and
/// means no wrap: one row, left to right, which is the only arrangement a chain reads cleanly in.
constexpr int kMinColumns = 0;
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

/// Solid execution edges, the link curve's round-out, and the dashed overlays drawn outside the
/// node editor's own link path.
constexpr float kEdgeThickness = 2.5f;
constexpr float kLinkStrength = 180.0f;
constexpr float kDashThickness = 1.4f;
constexpr ImU32 kAliasLinkColor = IM_COL32(214, 122, 196, 220);
constexpr ImU32 kCulledBorderColor = IM_COL32(140, 140, 146, 220);

/// Eight readable hues, one per resource by `resource % 8`. A resource keeps its hue for the whole
/// picture, so two links crossing are told apart by what travels along them rather than by where
/// they happen to run. None of them is the magenta the alias overlay owns.
const std::array<ImVec4, 8> kLinkPalette = {
    ImVec4(0.49f, 0.69f, 0.91f, 0.92f), // blue
    ImVec4(0.91f, 0.66f, 0.42f, 0.92f), // orange
    ImVec4(0.53f, 0.82f, 0.60f, 0.92f), // green
    ImVec4(0.89f, 0.81f, 0.47f, 0.92f), // yellow
    ImVec4(0.47f, 0.81f, 0.81f, 0.92f), // teal
    ImVec4(0.91f, 0.56f, 0.56f, 0.92f), // salmon
    ImVec4(0.69f, 0.63f, 0.91f, 0.92f), // lavender
    ImVec4(0.77f, 0.77f, 0.59f, 0.92f)  // sand
};

/// The title band's fill, one per kind, and the text drawn over it. A group is deliberately unlike
/// any pass kind: it is not one declaration but several folded together.
const ImVec4 kRasterTitleColor{0.16f, 0.30f, 0.48f, 1.0f};
const ImVec4 kComputeTitleColor{0.29f, 0.21f, 0.45f, 1.0f};
const ImVec4 kCopyTitleColor{0.13f, 0.36f, 0.30f, 1.0f};
const ImVec4 kGroupTitleColor{0.36f, 0.28f, 0.14f, 1.0f};
const ImVec4 kSinkTitleColor{0.45f, 0.33f, 0.11f, 1.0f};
const ImVec4 kCulledTitleColor{0.26f, 0.26f, 0.28f, 1.0f};
constexpr ImU32 kTitleTextColor = IM_COL32(238, 238, 244, 255);
constexpr ImU32 kCulledTitleTextColor = IM_COL32(196, 196, 202, 255);

/// A collapsed stage's accent, and what a culled item of either kind is accented in.
const ImVec4 kGroupAccentColor{0.92f, 0.86f, 0.66f, 1.0f};
const ImVec4 kCulledAccentColor{0.58f, 0.58f, 0.60f, 1.0f};

/// The colours one canvas card is drawn in: the band across its top, the dots and border that
/// carry its kind, and the text laid over the band.
struct ItemStyle {
    ImVec4 title;
    ImVec4 accent;
    ImVec4 border;
    ImU32 titleText = kTitleTextColor;
};

/// Where the measured pass put the columns, in canvas units: every column's left edge and the width
/// of its widest card. It is what the settling navigation reads to decide how much of the chain
/// fits on screen without changing the zoom.
struct MeasuredColumns {
    std::vector<float> x;
    std::vector<float> width;
};

/// A card title reads outwards from both ends: what the declaration is on the left, what it cost or
/// why it never ran on the right.
struct CardTitle {
    std::string left;
    std::string right;
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
// The title band's fill, chosen so the declaration path a pass took is readable at a glance and a
// culled pass reads as inert next to the graph that was proved.
ImVec4 nodeTitleColor(const GraphNode& node) {
    if (node.cullReason) {
        return kCulledTitleColor;
    }
    if (node.kind == GraphNodeKind::Sink) {
        return kSinkTitleColor;
    }
    switch (node.passKind) {
    case render::PassKind::Raster:
        return kRasterTitleColor;
    case render::PassKind::Compute:
        return kComputeTitleColor;
    case render::PassKind::Copy:
        return kCopyTitleColor;
    }
    return kRasterTitleColor;
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
        style.title = item.culled ? kCulledTitleColor : kGroupTitleColor;
        style.accent = item.culled ? kCulledAccentColor : kGroupAccentColor;
    } else {
        style.title = nodeTitleColor(model.nodes[item.index]);
        style.accent = nodeAccentColor(model.nodes[item.index]);
    }
    style.titleText = item.culled ? kCulledTitleTextColor : kTitleTextColor;
    style.border = item.culled ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
                               : ImVec4(style.accent.x * 0.7f, style.accent.y * 0.7f,
                                        style.accent.z * 0.7f, 0.9f);
    return style;
}

//======================================================================================================================
// Schedule position and name on the left, kind and cost on the right. A culled pass never ran, so
// what it would have cost is replaced by why it was dropped.
CardTitle nodeTitleText(const GraphNode& node) {
    if (node.kind == GraphNodeKind::Sink) {
        return {.left = node.label, .right = "sink"};
    }
    CardTitle title;
    title.left =
        node.scheduleOrder ? std::format("#{}  {}", *node.scheduleOrder, node.label) : node.label;
    if (node.cullReason) {
        title.right = std::format("culled  {}", cullReasonLabel(*node.cullReason));
        return title;
    }
    title.right =
        node.gpuMilliseconds
            ? std::format("{}  {:.3f} ms", passKindLabel(node.passKind), *node.gpuMilliseconds)
            : std::format("{}  unmeasured", passKindLabel(node.passKind));
    return title;
}

//======================================================================================================================
// A collapsed stage says what it stands for and what it cost. The measured count appears only when
// it differs from the member count, so a fully measured stage is not made to look partial. The
// leading marker is what says the box can be opened.
CardTitle groupTitleText(const GraphLayoutGroup& group) {
    CardTitle title{.left = std::format("> {}", group.stage), .right = {}};
    if (group.culled) {
        title.right = std::format("{} passes  culled", group.members.size());
        return title;
    }
    if (!group.gpuMillisecondsSum) {
        title.right = std::format("{} passes  unmeasured", group.members.size());
        return title;
    }
    title.right =
        std::format("{} passes  {:.3f} ms", group.members.size(), *group.gpuMillisecondsSum);
    if (group.measuredMembers < group.members.size()) {
        title.right += std::format(" ({} measured)", group.measuredMembers);
    }
    return title;
}

//======================================================================================================================
const std::string& pinText(const GraphLayoutPin& pin, bool fullLabel) {
    return fullLabel ? pin.label : pin.shortLabel;
}

//======================================================================================================================
// How much of a card one pin column claims: the half of the dot that falls inside the card, the gap
// after it, and the widest label. An empty column claims nothing.
float pinColumnWidth(const std::vector<GraphLayoutPin>& pins, bool fullLabels) {
    if (pins.empty()) {
        return 0.0f;
    }
    float widest = 0.0f;
    for (const GraphLayoutPin& pin : pins) {
        widest = std::max(widest, ImGui::CalcTextSize(pinText(pin, fullLabels).c_str()).x);
    }
    return kPinDotRadius + kPinLabelGap + widest;
}

//======================================================================================================================
// A pin reads outwards: an input's dot leads its label and an output's label leads its dot, so the
// two columns point the way the versions travel, and both dots are centred on the card's edge so a
// link lands on the silhouette rather than somewhere inside it.
//
// The dot is drawn by hand into the node's content channel, which the editor keeps above the node
// background and border, and it is deliberately not an ImGui item: an item half outside the card
// would grow the card's bounds around it and the edge the dot is centred on would move.
//
// The full label the short one abbreviates is what a hover reports; the caller shows it once, after
// every node has been submitted.
void drawPin(const GraphLayoutPin& pin, ed::PinId id, ed::PinKind kind, float edgeX,
             const ImVec4& accent, bool fullLabel, std::string& hoveredLabel) {
    const std::string& text = pinText(pin, fullLabel);
    const float lineHeight = ImGui::GetTextLineHeight();
    const float rowY = ImGui::GetCursorScreenPos().y;
    const ImVec2 dot(edgeX, rowY + lineHeight * 0.5f);

    ed::BeginPin(id, kind);
    // PinPivotSize re-arms the automatic pivot, so it has to be stated before the rect that
    // replaces it; the other order would compute a pivot over the row and throw the dot away.
    ed::PinPivotSize(ImVec2(0.0f, 0.0f));
    ed::PinPivotRect(dot, dot);
    if (kind == ed::PinKind::Input) {
        ImGui::SetCursorScreenPos(ImVec2(edgeX, rowY));
        ImGui::Dummy(ImVec2(kPinDotRadius + kPinLabelGap, lineHeight));
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(text.c_str());
    } else {
        const float width = ImGui::CalcTextSize(text.c_str()).x;
        ImGui::SetCursorScreenPos(ImVec2(edgeX - kPinDotRadius - kPinLabelGap - width, rowY));
        ImGui::TextUnformatted(text.c_str());
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::Dummy(ImVec2(kPinLabelGap + kPinDotRadius, lineHeight));
    }
    ed::EndPin();
    if (ImGui::IsItemHovered()) {
        hoveredLabel = pin.label;
    }
    ImGui::GetWindowDrawList()->AddCircleFilled(dot, kPinDotRadius, ImColor(accent));
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
// One card, whether it draws one declaration or a folded stage: a title band in the kind's colour
// across the full width, then a body with the versions arriving down the left edge and the versions
// leaving down the right. Its width is measured from the text this frame actually puts in it, so
// expanding a selection's labels widens exactly those cards.
//
// The editor's node padding is zero for the whole canvas, so the cursor at BeginNode is the card's
// top-left corner and the title band and the pin dots can be placed against the card's own edges.
void drawItem(const GraphNodeModel& model, const GraphLayout& layout, uint32_t itemIndex,
              bool fullLabels, std::string& hoveredLabel) {
    const GraphLayoutItem& item = layout.items[itemIndex];
    LMX_ASSERT(item.inputs.size() + item.outputs.size() < kPinIdStride,
               "a canvas item declares more pins than its id block can hold");

    const bool isGroup = item.kind == GraphLayoutItemKind::Group;
    const ItemStyle style = itemStyleOf(model, item);
    const CardTitle title = isGroup ? groupTitleText(layout.groups[item.index])
                                    : nodeTitleText(model.nodes[item.index]);
    const float rightWidth = ImGui::CalcTextSize(title.right.c_str()).x;
    const float titleWidth =
        kTitlePadX * 2.0f + ImGui::CalcTextSize(title.left.c_str()).x + kTitleGap + rightWidth;
    const float inputsWidth = pinColumnWidth(item.inputs, fullLabels);
    const float outputsWidth = pinColumnWidth(item.outputs, fullLabels);
    const float cardWidth =
        std::max({kCardMinWidth, titleWidth, inputsWidth + kColumnInnerGap + outputsWidth});

    const uintptr_t itemId = itemIdValue(layout, itemIndex);
    ed::PushStyleColor(ed::StyleColor_NodeBorder, style.border);
    ed::BeginNode(ed::NodeId(itemId));

    // Inside the canvas, ImGui screen space and the editor's canvas space are the same plane, and
    // with zero node padding this origin is the card's own top-left corner.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float lineHeight = ImGui::GetTextLineHeight();
    const float titleHeight = lineHeight + kTitlePadY * 2.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + cardWidth, origin.y + titleHeight),
                            ImColor(style.title), kCardRounding, ImDrawFlags_RoundCornersTop);
    drawList->AddText(ImVec2(origin.x + kTitlePadX, origin.y + kTitlePadY), style.titleText,
                      title.left.c_str());
    drawList->AddText(ImVec2(origin.x + cardWidth - kTitlePadX - rightWidth, origin.y + kTitlePadY),
                      style.titleText, title.right.c_str());
    ImGui::Dummy(ImVec2(cardWidth, titleHeight));

    const float pinsTop = origin.y + titleHeight + kBodyPadY;
    float bodyBottom = pinsTop;
    if (!item.inputs.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x, pinsTop));
        ImGui::BeginGroup();
        for (uint32_t pin = 0; pin < item.inputs.size(); ++pin) {
            drawPin(item.inputs[pin], pinIdOf(itemId, pin), ed::PinKind::Input, origin.x,
                    style.accent, fullLabels, hoveredLabel);
        }
        ImGui::EndGroup();
        bodyBottom = std::max(bodyBottom, ImGui::GetItemRectMax().y);
    }
    if (!item.outputs.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x, pinsTop));
        ImGui::BeginGroup();
        for (uint32_t pin = 0; pin < item.outputs.size(); ++pin) {
            const uint32_t ordinal = static_cast<uint32_t>(item.inputs.size()) + pin;
            drawPin(item.outputs[pin], pinIdOf(itemId, ordinal), ed::PinKind::Output,
                    origin.x + cardWidth, style.accent, fullLabels, hoveredLabel);
        }
        ImGui::EndGroup();
        bodyBottom = std::max(bodyBottom, ImGui::GetItemRectMax().y);
    }

    // A pinless card would otherwise be a title band with nothing under it, and every card needs a
    // body deep enough for the bottom rounding to read.
    ImGui::SetCursorScreenPos(ImVec2(origin.x, bodyBottom));
    ImGui::Dummy(ImVec2(cardWidth, kBodyBottomPad));

    ed::EndNode();
    ed::PopStyleColor();
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
// The coarse grid a picture is drawn at once, so that every card reports a size the measured pass
// can then place it from. Overlap here is harmless: nothing is read off these positions.
void applyProvisionalPositions(const GraphLayout& layout) {
    for (uint32_t index = 0; index < layout.items.size(); ++index) {
        const GraphLayoutItem& item = layout.items[index];
        ed::SetNodePosition(itemNodeId(layout, index),
                            ImVec2(static_cast<float>(item.column) * kProvisionalColumnPitch,
                                   static_cast<float>(item.row) * kProvisionalRowPitch +
                                       static_cast<float>(item.rank) * kProvisionalRankPitch));
    }
}

//======================================================================================================================
// Turns the layout's cells into pixels using the sizes the cards actually reported: a column is as
// wide as its widest card, a row is as tall as its deepest stack of its own tallest card, and the
// culled band takes a gap of its own so it never reads as one more row of the DAG that was proved.
//
// Returns false while any card has yet to report a size, which is true for exactly the first frame
// a picture is drawn -- a card has no size until the editor has laid it out once. On success it
// also reports the column geometry it had to compute anyway.
bool applyMeasuredPositions(const GraphLayout& layout, MeasuredColumns& columns) {
    columns = {};
    if (layout.items.empty()) {
        return true;
    }

    std::vector<ImVec2> sizes(layout.items.size());
    uint32_t columnCount = 0;
    uint32_t rowCount = 0;
    for (uint32_t index = 0; index < layout.items.size(); ++index) {
        sizes[index] = ed::GetNodeSize(itemNodeId(layout, index));
        if (sizes[index].x <= 0.0f || sizes[index].y <= 0.0f) {
            return false;
        }
        columnCount = std::max(columnCount, layout.items[index].column + 1);
        rowCount = std::max(rowCount, layout.items[index].row + 1);
    }

    std::vector<float> columnWidth(columnCount, 0.0f);
    std::vector<float> rowCardHeight(rowCount, 0.0f);
    std::vector<uint32_t> rowRanks(rowCount, 0);
    std::vector<bool> rowIsBand(rowCount, false);
    for (uint32_t index = 0; index < layout.items.size(); ++index) {
        const GraphLayoutItem& item = layout.items[index];
        columnWidth[item.column] = std::max(columnWidth[item.column], sizes[index].x);
        rowCardHeight[item.row] = std::max(rowCardHeight[item.row], sizes[index].y);
        rowRanks[item.row] = std::max(rowRanks[item.row], item.rank + 1);
        rowIsBand[item.row] = rowIsBand[item.row] || item.culled;
    }

    std::vector<float> columnX(columnCount, 0.0f);
    for (uint32_t column = 1; column < columnCount; ++column) {
        columnX[column] = columnX[column - 1] + columnWidth[column - 1] + kColumnGap;
    }
    std::vector<float> rowY(rowCount, 0.0f);
    for (uint32_t row = 1; row < rowCount; ++row) {
        const float height =
            static_cast<float>(rowRanks[row - 1]) * (rowCardHeight[row - 1] + kRankGap);
        rowY[row] = rowY[row - 1] + height + (rowIsBand[row] ? kCulledBandGap : kRowGap);
    }

    for (uint32_t index = 0; index < layout.items.size(); ++index) {
        const GraphLayoutItem& item = layout.items[index];
        ed::SetNodePosition(itemNodeId(layout, index),
                            ImVec2(columnX[item.column],
                                   rowY[item.row] + static_cast<float>(item.rank) *
                                                        (rowCardHeight[item.row] + kRankGap)));
    }
    columns.x = std::move(columnX);
    columns.width = std::move(columnWidth);
    return true;
}

//======================================================================================================================
// Opens the picture on as much of the chain as can be read: a graph the reader has to zoom into is
// worse than one they have to pan across. A graph that already fits the view is centred at the
// zoom in force; a longer one is opened on the leading columns, zoomed so those columns fill the
// view rather than so the whole chain does.
//
// The selection is a means, not a state -- it is set, navigated to, and cleared within the frame,
// so nothing stays selected and the details pane still reads empty.
//
// `ed::GetCurrentZoom()` is the view's inverse scale, canvas units per screen pixel (see
// `ImGuiEx::Canvas::ViewRect()`, which is `(widget size - origin) * InvScale`), so multiplying by
// the screen size is how many canvas units the panel can show at the zoom it already has.
void navigateToLeadingColumns(const GraphLayout& layout, const MeasuredColumns& columns) {
    if (columns.x.empty()) {
        return;
    }
    const float viewWidth = ed::GetScreenSize().x * ed::GetCurrentZoom();
    const uint32_t lastIndex = static_cast<uint32_t>(columns.x.size()) - 1;
    const bool wholeGraphFits = columns.x[lastIndex] + columns.width[lastIndex] <= viewWidth;

    // The first column reaching most of the view is the last one opened on; a chain whose columns
    // never reach that far is short enough to open on all of them.
    uint32_t lastColumn = lastIndex;
    if (!wholeGraphFits) {
        for (uint32_t column = 0; column <= lastIndex; ++column) {
            if (columns.x[column] + columns.width[column] >= kLeadingViewFill * viewWidth) {
                lastColumn = column;
                break;
            }
        }
    }

    bool any = false;
    for (uint32_t index = 0; index < layout.items.size(); ++index) {
        const GraphLayoutItem& item = layout.items[index];
        if (item.culled || item.column > lastColumn) {
            continue;
        }
        ed::SelectNode(itemNodeId(layout, index), true);
        any = true;
    }
    if (!any) {
        return;
    }
    // zoomIn = false is ZoomMode::None, which recentres and leaves the scale exactly as the reader
    // left it; true is ZoomMode::WithMargin, which fits the leading columns with a 5% margin, so
    // they start at that margin instead of centred in a half-empty canvas.
    ed::NavigateToSelection(!wholeGraphFits, 0.0f);
    ed::ClearSelection();
}

//======================================================================================================================
void drawCanvas(const GraphNodeModel& model, const GraphLayout& layout,
                RenderGraphPanelState& state, bool resetLayout) {
    LMX_ASSERT(model.nodes.size() <= kGroupIdBase,
               "a compiled frame declares more nodes than the canvas id space holds");

    ed::SetCurrentEditor(state.editor->context);
    // Zero node padding lets a card own its edges: the title band reaches the rounded corners and a
    // pin dot sits on the silhouette. The two link directions are the editor's own defaults, stated
    // here so the round curves below are read against something explicit rather than a default.
    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ed::PushStyleVar(ed::StyleVar_NodeRounding, kCardRounding);
    ed::PushStyleVar(ed::StyleVar_LinkStrength, kLinkStrength);
    ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(1.0f, 0.0f));
    ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(-1.0f, 0.0f));
    ed::Begin("lmx.renderGraph", ImVec2(0.0f, 0.0f));

    // Item indices only mean something within one picture, so the positions the user dragged and
    // the item they selected are both surrendered the moment the drawn picture changes.
    const bool pictureChanged = state.appliedSignature != layout.signature;
    if (pictureChanged) {
        state.appliedSignature = layout.signature;
        state.layoutPhase = GraphLayoutPhase::Provisional;
    }
    MeasuredColumns columns;
    if (state.layoutPhase == GraphLayoutPhase::Provisional || resetLayout) {
        if (applyMeasuredPositions(layout, columns)) {
            state.layoutPhase = GraphLayoutPhase::Measured;
        } else {
            applyProvisionalPositions(layout);
            state.layoutPhase = GraphLayoutPhase::Provisional;
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
                 pinIdOf(itemIdValue(layout, edge.toItem), edge.toPin),
                 kLinkPalette[edge.resource % kLinkPalette.size()], kEdgeThickness);
    }

    drawItemOverlays(model, layout);

    if (!hoveredLabel.empty()) {
        // A tooltip is an ImGui window, and the canvas is a transformed space: suspending it is
        // what puts the tooltip back in the screen space the mouse is actually in.
        ed::Suspend();
        ImGui::SetTooltip("%s", hoveredLabel.c_str());
        ed::Resume();
    }

    if (pictureChanged) {
        ed::ClearSelection();
        state.selectedItem.reset();
    }
    if (state.layoutPhase == GraphLayoutPhase::Measured) {
        // Card bounds are current by now, which is what makes navigating meaningful on the very
        // frame the measured positions were applied.
        navigateToLeadingColumns(layout, columns);
        state.layoutPhase = GraphLayoutPhase::Settled;
    }

    ed::End();
    ed::PopStyleVar(5);

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
    // A group item is drawn only while its stage is folded -- an open stage draws its members
    // instead -- so the only act this pane can offer here is opening it.
    if (ImGui::Button("Expand")) {
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
// The open stage a node belongs to, or null when it belongs to none. An expanded group draws its
// members rather than itself, so this is the only place its identity still reaches the reader.
const GraphLayoutGroup* expandedGroupOfNode(const GraphLayout& layout, uint32_t nodeIndex) {
    for (const GraphLayoutGroup& group : layout.groups) {
        if (group.expanded && std::ranges::contains(group.members, nodeIndex)) {
            return &group;
        }
    }
    return nullptr;
}

//======================================================================================================================
// An open stage has no group box left on the canvas to fold it back up with, so a member's details
// carry the fold. It is the same act as double-clicking that member, offered to a reader who is
// looking at this pane rather than at the canvas.
void drawMemberStageSection(const GraphLayoutGroup& group, GraphLayoutOptions& options) {
    ImGui::SeparatorText("Stage");
    ImGui::TextUnformatted(group.key.c_str());
    ImGui::Text("%zu passes, expanded", group.members.size());
    if (ImGui::Button(std::format("Collapse {}", group.stage).c_str())) {
        toggleGroupExpansion(options, group.key);
    }
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
    if (const GraphLayoutGroup* stage = expandedGroupOfNode(layout, item.index)) {
        drawMemberStageSection(*stage, state.layoutOptions);
    }
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
        dumpFrame(newest->record, model.frameId);
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
