//----------------------------------------------------------------------------------------------------------------------
/// @file GraphLayout.cpp
/// @brief Implements the stage grouping and placement behind the Render Graph canvas.
//----------------------------------------------------------------------------------------------------------------------

#include "App/GraphLayout.h"

#include "Core/Assert.h"

#include <algorithm>
#include <format>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace lmx::app {
namespace {

constexpr uint32_t kNoIndex = ~0u;

/// The prefix a label groups under and the segment that names the stage.
struct StageKey {
    std::string key;
    std::string stage;
};

//======================================================================================================================
// Four segments are the threshold because that is where a label stops being a name and starts
// being a name inside a stage: `lmx.pass.bloom.threshold` says which bloom step it is, while
// `lmx.pass.scene` is already the whole thing and has no step to fold away.
std::optional<StageKey> stageKeyOf(std::string_view label) {
    uint32_t dots = 0;
    size_t last = 0;
    size_t previous = 0;
    for (size_t index = 0; index < label.size(); ++index) {
        if (label[index] != '.') {
            continue;
        }
        ++dots;
        previous = last;
        last = index;
    }
    if (dots < 3) {
        return std::nullopt;
    }
    return StageKey{.key = std::string(label.substr(0, last)),
                    .stage = std::string(label.substr(previous + 1, last - previous - 1))};
}

//======================================================================================================================
GraphLayoutPin toLayoutPin(const GraphNodePin& pin) {
    return {.resource = pin.resource,
            .resourceName = pin.resourceName,
            .version = pin.version,
            .shortLabel = graphPinShortLabel(pin.resourceName, pin.version),
            .label = pin.label};
}

//======================================================================================================================
std::vector<GraphLayoutPin> toLayoutPins(const std::vector<GraphNodePin>& pins) {
    std::vector<GraphLayoutPin> out;
    out.reserve(pins.size());
    for (const GraphNodePin& pin : pins) {
        out.push_back(toLayoutPin(pin));
    }
    return out;
}

//======================================================================================================================
// One pin per distinct (resource, version), kept in the order the group's visible edges first named
// it, so a collapsed stage's boundary is a function of the edges rather than of a container order.
uint32_t addBoundaryPin(std::vector<GraphLayoutPin>& pins, const GraphNodePin& source) {
    for (uint32_t index = 0; index < pins.size(); ++index) {
        if (pins[index].resource == source.resource && pins[index].version == source.version) {
            return index;
        }
    }
    pins.push_back(toLayoutPin(source));
    return static_cast<uint32_t>(pins.size() - 1);
}

//======================================================================================================================
// Candidates are collected in declaration order and survive only where they say something: two
// members that agree about being culled. Keys are looked up, never ordered by, so the hash
// container cannot reach the output.
std::vector<GraphLayoutGroup> buildGroups(const GraphNodeModel& model,
                                          const GraphLayoutOptions& options) {
    std::vector<GraphLayoutGroup> candidates;
    std::unordered_map<std::string, uint32_t> byKey;
    for (uint32_t index = 0; index < model.nodes.size(); ++index) {
        const GraphNode& node = model.nodes[index];
        if (node.kind != GraphNodeKind::Pass) {
            continue;
        }
        const std::optional<StageKey> stage = stageKeyOf(node.label);
        if (!stage) {
            continue;
        }
        const bool culled = node.cullReason.has_value();
        std::string key = culled ? stage->key + "#culled" : stage->key;
        const auto [slot, inserted] =
            byKey.try_emplace(key, static_cast<uint32_t>(candidates.size()));
        if (inserted) {
            candidates.push_back({.key = std::move(key), .stage = stage->stage, .culled = culled});
        }
        candidates[slot->second].members.push_back(index);
    }

    std::vector<GraphLayoutGroup> groups;
    for (GraphLayoutGroup& candidate : candidates) {
        if (candidate.members.size() < 2) {
            continue;
        }
        candidate.expanded = std::ranges::find(options.expandedGroups, candidate.key) !=
                             options.expandedGroups.end();
        double sum = 0.0;
        for (const uint32_t member : candidate.members) {
            if (const std::optional<double>& milliseconds = model.nodes[member].gpuMilliseconds) {
                sum += *milliseconds;
                ++candidate.measuredMembers;
            }
        }
        if (candidate.measuredMembers > 0) {
            candidate.gpuMillisecondsSum = sum;
        }
        groups.push_back(std::move(candidate));
    }
    return groups;
}

//======================================================================================================================
std::string buildSignature(const GraphNodeModel& model, const GraphLayoutOptions& options) {
    std::string out = model.shapeSignature;
    out += std::format("\ncolumns={}\n", options.columnsPerRow);
    std::vector<std::string> keys = options.expandedGroups;
    std::ranges::sort(keys);
    keys.erase(std::ranges::unique(keys).begin(), keys.end());
    for (const std::string& key : keys) {
        out += std::format("expanded={}\n", key);
    }
    return out;
}

} // namespace

//======================================================================================================================
std::string graphPinShortLabel(std::string_view resourceName, uint32_t version) {
    const size_t dot = resourceName.rfind('.');
    const std::string_view leaf =
        dot == std::string_view::npos ? resourceName : resourceName.substr(dot + 1);
    return std::format("{} v{}", leaf, version);
}

//======================================================================================================================
GraphLayout layoutGraph(const GraphNodeModel& model, const GraphLayoutOptions& options) {
    GraphLayout layout;
    layout.groups = buildGroups(model, options);

    const uint32_t nodeCount = static_cast<uint32_t>(model.nodes.size());
    std::vector<uint32_t> collapsedGroupOfNode(nodeCount, kNoIndex);
    for (uint32_t groupIndex = 0; groupIndex < layout.groups.size(); ++groupIndex) {
        const GraphLayoutGroup& group = layout.groups[groupIndex];
        if (group.expanded) {
            continue;
        }
        for (const uint32_t member : group.members) {
            collapsedGroupOfNode[member] = groupIndex;
        }
    }

    // Items in declaration order, with a collapsed group taking the place of its first member, so
    // the list still reads the way the frame was declared.
    layout.itemOfNode.assign(nodeCount, kNoIndex);
    std::vector<uint32_t> itemOfGroup(layout.groups.size(), kNoIndex);
    for (uint32_t index = 0; index < nodeCount; ++index) {
        const uint32_t groupIndex = collapsedGroupOfNode[index];
        if (groupIndex == kNoIndex) {
            const GraphNode& node = model.nodes[index];
            layout.itemOfNode[index] = static_cast<uint32_t>(layout.items.size());
            layout.items.push_back({.kind = GraphLayoutItemKind::Node,
                                    .index = index,
                                    .inputs = toLayoutPins(node.inputs),
                                    .outputs = toLayoutPins(node.outputs),
                                    .culled = node.cullReason.has_value()});
            continue;
        }
        if (itemOfGroup[groupIndex] == kNoIndex) {
            itemOfGroup[groupIndex] = static_cast<uint32_t>(layout.items.size());
            layout.items.push_back({.kind = GraphLayoutItemKind::Group,
                                    .index = groupIndex,
                                    .culled = layout.groups[groupIndex].culled});
        }
        layout.itemOfNode[index] = itemOfGroup[groupIndex];
    }

    // Visible edges and the boundary pins they imply are one pass over the model's edges: a
    // duplicate never adds a pin the edge it duplicates did not already add.
    std::set<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>> seen;
    for (const GraphNodeEdge& edge : model.edges) {
        const uint32_t fromItem = layout.itemOfNode[edge.fromNode];
        const uint32_t toItem = layout.itemOfNode[edge.toNode];
        if (fromItem == toItem) {
            LMX_ASSERT(layout.items[fromItem].kind == GraphLayoutItemKind::Group,
                       "an execution edge starts and ends at the same node");
            continue;
        }
        if (!seen.emplace(fromItem, toItem, edge.resource, edge.version).second) {
            continue;
        }
        GraphLayoutItem& from = layout.items[fromItem];
        GraphLayoutItem& to = layout.items[toItem];
        const uint32_t fromPin =
            from.kind == GraphLayoutItemKind::Group
                ? addBoundaryPin(from.outputs, model.nodes[edge.fromNode].outputs[edge.fromPin])
                : edge.fromPin;
        const uint32_t toPin =
            to.kind == GraphLayoutItemKind::Group
                ? addBoundaryPin(to.inputs, model.nodes[edge.toNode].inputs[edge.toPin])
                : edge.toPin;
        layout.edges.push_back({.fromItem = fromItem,
                                .fromPin = fromPin,
                                .toItem = toItem,
                                .toPin = toPin,
                                .resource = edge.resource,
                                .version = edge.version,
                                .resourceName = edge.resourceName});
    }

    const uint32_t itemCount = static_cast<uint32_t>(layout.items.size());
    std::vector<std::optional<uint32_t>> lowestSchedule(itemCount);
    for (uint32_t index = 0; index < itemCount; ++index) {
        const GraphLayoutItem& item = layout.items[index];
        if (item.kind == GraphLayoutItemKind::Node) {
            lowestSchedule[index] = model.nodes[item.index].scheduleOrder;
            continue;
        }
        for (const uint32_t member : layout.groups[item.index].members) {
            const std::optional<uint32_t>& order = model.nodes[member].scheduleOrder;
            if (order && (!lowestSchedule[index] || *order < *lowestSchedule[index])) {
                lowestSchedule[index] = *order;
            }
        }
    }

    // Each pass belongs to exactly one item, so the lowest schedule position is unique per item and
    // this is a total order. It is the compiled schedule whenever nothing is collapsed.
    std::vector<uint32_t> scheduled;
    for (uint32_t index = 0; index < itemCount; ++index) {
        if (lowestSchedule[index]) {
            scheduled.push_back(index);
        }
    }
    std::ranges::sort(scheduled, {}, [&](uint32_t index) { return *lowestSchedule[index]; });

    std::vector<std::vector<uint32_t>> incoming(itemCount);
    for (uint32_t index = 0; index < layout.edges.size(); ++index) {
        incoming[layout.edges[index].toItem].push_back(index);
    }

    // Longest visible-edge path, settled in schedule order. Collapsing can put an item's producer
    // behind it -- a stage feeding a pass that feeds the same stage -- and that edge simply does
    // not push a layer, because the collapse threw away the depth it stood for.
    std::vector<bool> settled(itemCount, false);
    uint32_t maxLayer = 0;
    bool anyPlaced = false;
    for (const uint32_t index : scheduled) {
        uint32_t layer = 0;
        for (const uint32_t edge : incoming[index]) {
            const uint32_t from = layout.edges[edge].fromItem;
            if (settled[from]) {
                layer = std::max(layer, layout.items[from].layer + 1);
            }
        }
        layout.items[index].layer = layer;
        settled[index] = true;
        maxLayer = std::max(maxLayer, layer);
        anyPlaced = true;
    }

    std::vector<uint32_t> sinks;
    for (uint32_t index = 0; index < itemCount; ++index) {
        const GraphLayoutItem& item = layout.items[index];
        if (item.kind != GraphLayoutItemKind::Node) {
            continue;
        }
        const GraphNode& node = model.nodes[item.index];
        if (node.kind != GraphNodeKind::Sink) {
            continue;
        }
        LMX_ASSERT(node.producerPass.has_value(), "a sink node has no producing pass");
        layout.items[index].layer = layout.items[layout.itemOfNode[*node.producerPass]].layer + 1;
        maxLayer = std::max(maxLayer, layout.items[index].layer);
        anyPlaced = true;
        sinks.push_back(index);
    }

    // Passes rank before sinks within a layer, and each of those two groups keeps the order it was
    // visited in, so every tie breaks the same way twice.
    const uint32_t layerCount = anyPlaced ? maxLayer + 1 : 0;
    std::vector<uint32_t> rankCount(layerCount, 0);
    std::vector<uint32_t> placed(scheduled);
    placed.insert(placed.end(), sinks.begin(), sinks.end());
    for (const uint32_t index : placed) {
        GraphLayoutItem& item = layout.items[index];
        item.rank = rankCount[item.layer]++;
    }

    const uint32_t columns =
        options.columnsPerRow > 0 ? options.columnsPerRow : std::max(layerCount, 1u);
    const uint32_t rowCount = layerCount == 0 ? 0 : (maxLayer / columns) + 1;
    std::vector<float> rowBase(rowCount + 1, 0.0f);
    for (uint32_t row = 0; row < rowCount; ++row) {
        uint32_t tallest = 0;
        for (uint32_t layer = row * columns; layer < layerCount && layer < (row + 1) * columns;
             ++layer) {
            tallest = std::max(tallest, rankCount[layer]);
        }
        rowBase[row + 1] = rowBase[row] + static_cast<float>(tallest) * kGraphLayoutRowSpacing +
                           kGraphLayoutRowGap;
    }

    for (const uint32_t index : placed) {
        GraphLayoutItem& item = layout.items[index];
        item.row = item.layer / columns;
        item.column = item.layer % columns;
        item.x = static_cast<float>(item.column) * kGraphLayoutColumnSpacing;
        item.y = rowBase[item.row] + static_cast<float>(item.rank) * kGraphLayoutRowSpacing;
    }

    // Culled items are not in the DAG that was proved, so they take no layer of it. Item order is
    // declaration order, which is the order the band reads in.
    const float band = rowBase[rowCount] + kGraphLayoutCulledBandGap;
    uint32_t ordinal = 0;
    for (GraphLayoutItem& item : layout.items) {
        if (!item.culled) {
            continue;
        }
        item.layer = ordinal;
        item.rank = 0;
        item.row = rowCount;
        item.column = ordinal;
        item.x = static_cast<float>(ordinal) * kGraphLayoutColumnSpacing;
        item.y = band;
        ++ordinal;
    }

    for (uint32_t index = 0; index < model.aliasLinks.size(); ++index) {
        const GraphAliasLink& link = model.aliasLinks[index];
        const uint32_t fromItem = layout.itemOfNode[link.fromNode];
        const uint32_t toItem = layout.itemOfNode[link.toNode];
        if (fromItem == toItem && layout.items[fromItem].kind == GraphLayoutItemKind::Group) {
            continue;
        }
        layout.aliasLinks.push_back({.fromItem = fromItem, .toItem = toItem, .sourceLink = index});
    }

    layout.signature = buildSignature(model, options);
    return layout;
}

} // namespace lmx::app
