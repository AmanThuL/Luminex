//----------------------------------------------------------------------------------------------------------------------
/// @file GraphLayout.h
/// @brief Declares the ImGui-free stage grouping and placement behind the Render Graph canvas.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/GraphNodeModel.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::app {

/// Horizontal distance between two adjacent columns, in canvas units. A pin draws as a dot with a
/// short label -- the resource's last name segment and its version -- so a node is roughly 200
/// units wide and two of them never meet at this pitch.
inline constexpr float kGraphLayoutColumnSpacing = 260.0f;

/// Vertical distance between two adjacent ranks within a column, in canvas units.
inline constexpr float kGraphLayoutRowSpacing = 110.0f;

/// Vertical gap below a wrapped row before the next one begins, in canvas units. It is what keeps
/// the last rank of one row from reading as one more rank of the next.
inline constexpr float kGraphLayoutRowGap = 80.0f;

/// Vertical gap between the last wrapped row and the culled band, in canvas units. It exists so a
/// culled pass is never read as one more rank of the DAG that was proved.
inline constexpr float kGraphLayoutCulledBandGap = 120.0f;

/// What the caller wants drawn: how far a chain may run before it wraps, and which stage groups
/// are open.
struct GraphLayoutOptions {
    /// Columns a row holds before the next layer wraps onto a new row; 0 leaves the chain in one
    /// row of unlimited width.
    uint32_t columnsPerRow = 0;
    /// Keys of the groups drawn as their member nodes. Every other group draws as one node. A key
    /// naming no group is ignored, which is what lets the set outlive a shape change.
    std::vector<std::string> expandedGroups;
};

/// One stage of passes that a shared label prefix names, drawn as a single node while collapsed.
///
/// A group exists only where it says something: its key must name at least two passes, and its
/// members must agree about being culled, because a band of culled work and the proved DAG are two
/// different pictures. A key whose passes disagree therefore yields up to two groups, `<key>` for
/// the scheduled ones and `<key>#culled` for the rest, and each of those must reach two members of
/// its own to survive.
struct GraphLayoutGroup {
    std::string key;   ///< The shared label prefix; a culled group appends "#culled".
    std::string stage; ///< The prefix's last segment: the group's display name.
    std::vector<uint32_t>
        members;           ///< Member node indices, in declaration order; never fewer than two.
    bool culled = false;   ///< Whether every member was culled.
    bool expanded = false; ///< Whether the caller asked to see the members instead.
    std::optional<double> gpuMillisecondsSum; ///< Summed GPU time over the measured members, or
                                              ///< empty when no member was measured.
    uint32_t measuredMembers = 0;             ///< How many members carried a timing.
};

/// What a drawn item stands for.
enum class GraphLayoutItemKind {
    Node, ///< One node of the model: a pass or a sink.
    Group ///< One collapsed stage group standing in for its members.
};

/// One endpoint on an item, naming the exact resource version that flows through it.
struct GraphLayoutPin {
    uint32_t resource = 0;    ///< Index into GraphInspectorModel::resources.
    std::string resourceName; ///< The resource's name, copied for direct display.
    uint32_t version = 0;     ///< The version this pin carries.
    std::string shortLabel;   ///< Compact display text, `sceneColorHdr v1`.
    std::string label;        ///< Full display text, `r3 "lmx.render.sceneColorHdr" v1`.
};

/// One box on the canvas: a node the layout drew on its own, or a collapsed group standing in for
/// several, with the pins it shows and the place it was put.
///
/// A node item carries the node's own pins, unconnected imports included. A group item carries only
/// the boundary pins its visible edges cross, so what a collapsed stage shows is exactly what
/// enters and leaves it. `layer`, `rank`, `row`, and `column` describe the proved DAG; a culled
/// item is not in that DAG and instead reports the band row and its ordinal along it.
struct GraphLayoutItem {
    GraphLayoutItemKind kind = GraphLayoutItemKind::Node; ///< Which index below is meaningful.
    uint32_t index = 0;                  ///< Node index, or index into GraphLayout::groups.
    std::vector<GraphLayoutPin> inputs;  ///< Versions arriving, in first-seen order.
    std::vector<GraphLayoutPin> outputs; ///< Versions leaving, in first-seen order.
    uint32_t layer = 0;                  ///< Longest visible-edge path from a producer-less item,
                                         ///< or the ordinal along the band for a culled item.
    uint32_t rank = 0;                   ///< Position within the layer; always 0 in the band.
    uint32_t row = 0;                    ///< Wrapped row holding the layer, or the band's own row.
    uint32_t column = 0;                 ///< Column within that row.
    float x = 0.0f;                      ///< Horizontal position, in canvas units.
    float y = 0.0f;                      ///< Vertical position, in canvas units.
    bool culled = false;                 ///< Whether it sits in the culled band.
};

/// One execution dependency between two drawn items, the model's edges after collapsing.
///
/// An edge whose endpoints fall inside one collapsed group is internal to that group and is not
/// drawn at all. What remains is deduplicated per (from item, to item, resource, version), so a
/// stage that hands three versions of one resource to another stage draws three edges and a stage
/// that hands the same version through two members draws one.
struct GraphLayoutEdge {
    uint32_t fromItem = 0;    ///< Index into GraphLayout::items of the producing item.
    uint32_t fromPin = 0;     ///< Index into that item's `outputs`.
    uint32_t toItem = 0;      ///< Index into GraphLayout::items of the consumer.
    uint32_t toPin = 0;       ///< Index into that item's `inputs`.
    uint32_t resource = 0;    ///< Index into GraphInspectorModel::resources.
    uint32_t version = 0;     ///< The version travelling along it.
    std::string resourceName; ///< The resource's name, copied for direct display.
};

/// One transient reuse boundary between two drawn items. A boundary whose two ends fall inside one
/// collapsed group is memory changing hands out of sight and is not drawn.
struct GraphLayoutAliasLink {
    uint32_t fromItem = 0;   ///< Index into GraphLayout::items of the item that freed the memory.
    uint32_t toItem = 0;     ///< Index into GraphLayout::items of the item that took it.
    uint32_t sourceLink = 0; ///< Index into GraphNodeModel::aliasLinks, which holds the details.
};

/// One drawable picture of a compiled frame: which stages exist, which boxes are drawn, what
/// connects them, and where they sit.
///
/// It is a pure function of the node model, the expansion set, and the column count -- timings,
/// frame ID, and byte offsets never move a box -- and carries no ImGui or node-editor type, so it
/// is unit-testable without a UI.
struct GraphLayout {
    std::vector<GraphLayoutGroup> groups;         ///< Stage groups, ordered by first member.
    std::vector<GraphLayoutItem> items;           ///< Drawn boxes, in declaration order.
    std::vector<uint32_t> itemOfNode;             ///< Node index -> the item that draws it.
    std::vector<GraphLayoutEdge> edges;           ///< Visible execution dependencies.
    std::vector<GraphLayoutAliasLink> aliasLinks; ///< Visible transient reuse boundaries.
    std::string signature;                        ///< Identity of the drawn picture; see below.
};

/// Groups, collapses, layers, and places a node model into the picture the canvas draws.
///
/// Items are the model's nodes with every collapsed group's members replaced by one box, ordered so
/// a group takes the place of its first member and the whole list still reads in declaration order.
/// Layer is the longest visible-edge path from the producer-less scheduled items and a sink sits
/// one layer past its producer; rank orders a layer by its items' lowest schedule position and then
/// puts the sinks after, by declaration. With `columnsPerRow = C > 0` layer L sits at row `L / C`
/// and column `L % C`, each row is as tall as its busiest layer plus a gap, and rows stack
/// downwards. Culled items form a band below the last row, ordered by declaration index.
///
/// Collapsing can hide the ordering the schedule proved -- a stage feeding a pass that feeds the
/// same stage is one box pointing at itself -- so layers are settled in schedule order and an edge
/// arriving from an item not yet settled does not push a layer. The picture stays finite and
/// deterministic; it simply cannot show a depth the collapse threw away.
///
/// `signature` is the model's shape signature followed by the options that shaped it, so equal
/// signatures mean the same picture. It changes when a group opens or the column count moves and
/// never when a driver reports a different time, which is what lets a caller keep dragged positions
/// across frames and know when it must not.
GraphLayout layoutGraph(const GraphNodeModel& model, const GraphLayoutOptions& options);

/// Renders the compact pin text a node draws: the resource name's last dotted segment and its
/// version, so `lmx.render.sceneColorHdr` at version 1 reads `sceneColorHdr v1`.
std::string graphPinShortLabel(std::string_view resourceName, uint32_t version);

} // namespace lmx::app
