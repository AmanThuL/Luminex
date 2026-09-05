//----------------------------------------------------------------------------------------------------------------------
/// @file GraphNodeModel.cpp
/// @brief Implements the node, edge, and layout shaping behind the Render Graph canvas.
//----------------------------------------------------------------------------------------------------------------------

#include "App/GraphNodeModel.h"

#include "Core/Assert.h"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace lmx::app {
namespace {

using render::CompiledFrameDebug;
using render::CompiledFrameRecord;

constexpr uint32_t kNoPin = ~0u;

//======================================================================================================================
// Mirrors GraphDump.cpp's private naming for the same reason GraphInspectorModel.cpp mirrors its
// use names: the clients read the same enumerators and should say the same words for them, but
// neither owns the other's vocabulary and Source/Render is out of scope for the panel.
std::string_view passKindName(render::PassKind kind) {
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
std::string_view sinkKindName(render::SinkKind kind) {
    switch (kind) {
    case render::SinkKind::Export:
        return "export";
    case render::SinkKind::Present:
        return "present";
    case render::SinkKind::Readback:
        return "readback";
    }
    return "export";
}

//======================================================================================================================
// Derived from role alone: these four roles are the ones that can advance a resource from version v
// to v + 1. That is a superset of the compiler's actual producer rule (RenderGraph.cpp's
// `discarded` check), which withholds the producer entry when a color or depth attachment stores
// with StoreOp::Discard -- information this model's DebugUse rows do not carry. A discarding write,
// then, still gets an output pin for a version nothing produced; the graph rejects any consumer of
// that version, so no edge can be fabricated for it, and the pin stays unconnected. That
// unconnected pin also participates in the shape signature like any other.
bool producesNextVersion(render::UseRole role) {
    switch (role) {
    case render::UseRole::Write:
    case render::UseRole::ColorAttachment:
    case render::UseRole::DepthAttachment:
    case render::UseRole::CopyDestination:
        return true;
    case render::UseRole::Read:
    case render::UseRole::ShaderRead:
    case render::UseRole::IndirectArgument:
    case render::UseRole::CopySource:
        return false;
    }
    return false;
}

//======================================================================================================================
uint64_t versionKey(uint32_t resource, uint32_t version) {
    return (static_cast<uint64_t>(resource) << 32) | version;
}

//======================================================================================================================
std::string pinLabel(uint32_t resource, const std::string& name, uint32_t version) {
    return std::format("r{} \"{}\" v{}", resource, name, version);
}

//======================================================================================================================
uint32_t findPin(const std::vector<GraphNodePin>& pins, uint32_t resource, uint32_t version) {
    for (uint32_t index = 0; index < pins.size(); ++index) {
        if (pins[index].resource == resource && pins[index].version == version) {
            return index;
        }
    }
    return kNoPin;
}

//======================================================================================================================
// One pin per distinct (resource, version), kept in the order the pass first named it so the pin
// list is a function of the declarations rather than of a container's iteration order.
void addPin(std::vector<GraphNodePin>& pins, uint32_t resource, const std::string& name,
            uint32_t version) {
    if (findPin(pins, resource, version) != kNoPin) {
        return;
    }
    pins.push_back({.resource = resource,
                    .resourceName = name,
                    .version = version,
                    .label = pinLabel(resource, name, version)});
}

//======================================================================================================================
const GraphInspectorTransientRow* findTransient(const GraphInspectorModel& rows,
                                                uint32_t resource) {
    const auto found =
        std::ranges::find(rows.transients, resource, &GraphInspectorTransientRow::resource);
    return found == rows.transients.end() ? nullptr : &*found;
}

//======================================================================================================================
std::string buildShapeSignature(const GraphNodeModel& model) {
    std::string out;
    for (uint32_t index = 0; index < model.nodes.size(); ++index) {
        const GraphNode& node = model.nodes[index];
        if (node.kind == GraphNodeKind::Pass) {
            out += std::format("n{} pass {} {}{}\n", index, passKindName(node.passKind), node.label,
                               node.cullReason ? " culled" : "");
        } else {
            out += std::format("n{} sink {} {}\n", index, sinkKindName(node.sinkKind), node.label);
        }
        for (uint32_t pin = 0; pin < node.inputs.size(); ++pin) {
            out += std::format("n{} in{} {}\n", index, pin, node.inputs[pin].label);
        }
        for (uint32_t pin = 0; pin < node.outputs.size(); ++pin) {
            out += std::format("n{} out{} {}\n", index, pin, node.outputs[pin].label);
        }
    }
    for (uint32_t index = 0; index < model.edges.size(); ++index) {
        const GraphNodeEdge& edge = model.edges[index];
        out += std::format("e{} n{}:o{} -> n{}:i{} r{} v{}\n", index, edge.fromNode, edge.fromPin,
                           edge.toNode, edge.toPin, edge.resource, edge.version);
    }
    // Byte offsets and sizes are left out on purpose: the same frame packed into a bigger heap is
    // the same picture, and a signature that moved with a packing would throw away every dragged
    // node position for a reason the viewer cannot see.
    for (uint32_t index = 0; index < model.aliasLinks.size(); ++index) {
        const GraphAliasLink& link = model.aliasLinks[index];
        out += std::format("a{} n{} -> n{} r{} -> r{}\n", index, link.fromNode, link.toNode,
                           link.freedResource, link.resource);
    }
    return out;
}

} // namespace

//======================================================================================================================
GraphNodeModel buildGraphNodeModel(const CompiledFrameRecord& record,
                                   std::span<const rhi::PassTiming> timings) {
    const CompiledFrameDebug& debug = record.debug;
    const GraphInspectorModel rows = buildGraphInspectorModel(record, timings);

    GraphNodeModel model;
    model.frameId = rows.frameId;
    model.poolingEnabled = rows.poolingEnabled;
    model.memory = rows.memory;

    const uint32_t passCount = static_cast<uint32_t>(rows.passes.size());
    std::vector<std::optional<uint32_t>> schedulePosition(passCount);
    for (uint32_t order = 0; order < rows.schedule.size(); ++order) {
        schedulePosition[rows.schedule[order]] = order;
    }

    model.nodes.reserve(passCount + debug.sinks.size());
    for (uint32_t index = 0; index < passCount; ++index) {
        const GraphInspectorPassRow& row = rows.passes[index];
        GraphNode node;
        node.kind = GraphNodeKind::Pass;
        node.index = index;
        node.passKind = row.kind;
        node.label = row.label;
        node.scheduleOrder = schedulePosition[index];
        node.cullReason = row.cullReason;
        node.gpuMilliseconds = row.gpuMilliseconds;
        node.uses = row.uses;
        for (const GraphInspectorTransitionRow& transition : rows.transitions) {
            if (transition.beforePass == index) {
                node.barriersBefore.push_back(transition);
            }
        }

        // A culled pass is not part of the graph that was proved, so it gets no pin and therefore
        // no edge -- not even the ones its own declarations would imply. Its uses stay for the
        // details pane, which marks them as never executed.
        if (node.scheduleOrder) {
            for (const GraphInspectorUseRow& use : node.uses) {
                addPin(node.inputs, use.resource, use.resourceName, use.version);
                if (producesNextVersion(use.role)) {
                    addPin(node.outputs, use.resource, use.resourceName, use.version + 1);
                }
            }
            for (const GraphInspectorTransientRow& transient : rows.transients) {
                if (!transient.used) {
                    continue;
                }
                const std::optional<uint32_t>& first = schedulePosition[transient.firstPass];
                const std::optional<uint32_t>& last = schedulePosition[transient.lastPass];
                LMX_ASSERT(first && last, "a used transient's lifetime bounds a culled pass");
                if (*node.scheduleOrder >= *first && *node.scheduleOrder <= *last) {
                    node.transientsAlive.push_back({.resource = transient.resource,
                                                    .resourceName = transient.resourceName,
                                                    .offset = transient.offset,
                                                    .size = transient.size,
                                                    .alignment = transient.alignment,
                                                    .aliases = transient.aliases});
                }
            }
        }
        model.nodes.push_back(std::move(node));
    }

    // Producers are looked up, never ordered by, so a hash container cannot reach the output.
    std::unordered_map<uint64_t, uint32_t> producer;
    for (const uint32_t passIndex : rows.schedule) {
        for (const render::DebugUse& use : debug.passes[passIndex].uses) {
            if (producesNextVersion(use.role)) {
                producer.emplace(versionKey(use.resource, use.version + 1), passIndex);
            }
        }
    }

    for (uint32_t index = 0; index < debug.sinks.size(); ++index) {
        const render::DebugSink& sink = debug.sinks[index];
        const std::string& name = rows.resources[sink.resource].name;
        GraphNode node;
        node.kind = GraphNodeKind::Sink;
        node.index = index;
        node.label = std::string(sinkKindName(sink.kind));
        node.sinkKind = sink.kind;
        addPin(node.inputs, sink.resource, name, sink.version);
        const auto found = producer.find(versionKey(sink.resource, sink.version));
        LMX_ASSERT(found != producer.end(),
                   "a compiled sink roots a version no scheduled pass produced");
        node.producerPass = found->second;
        model.nodes.push_back(std::move(node));
    }

    for (uint32_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
        const GraphNode& node = model.nodes[nodeIndex];
        for (uint32_t pin = 0; pin < node.inputs.size(); ++pin) {
            const GraphNodePin& input = node.inputs[pin];
            if (input.version == 0) {
                // Contents the frame did not produce. The pin stays unconnected rather than being
                // rooted in an invented source node, which is the honest picture of an import.
                LMX_ASSERT(node.kind == GraphNodeKind::Pass,
                           "a compiled sink roots version 0, which no pass can have written");
                continue;
            }
            const auto found = producer.find(versionKey(input.resource, input.version));
            LMX_ASSERT(found != producer.end(),
                       "a scheduled use names a version no scheduled pass produced");
            const uint32_t fromNode = found->second;
            LMX_ASSERT(!node.scheduleOrder || *schedulePosition[fromNode] < *node.scheduleOrder,
                       "a producer is scheduled after the pass consuming it");
            const uint32_t fromPin =
                findPin(model.nodes[fromNode].outputs, input.resource, input.version);
            LMX_ASSERT(fromPin != kNoPin,
                       "a producing pass has no output pin for what it produced");
            model.edges.push_back({.fromNode = fromNode,
                                   .fromPin = fromPin,
                                   .toNode = nodeIndex,
                                   .toPin = pin,
                                   .resource = input.resource,
                                   .version = input.version,
                                   .resourceName = input.resourceName});
        }
    }

    // Longest path from the producer-less scheduled passes. The compiled schedule is already
    // topological, so one forward sweep settles every layer: a pass's producers are all behind it.
    std::vector<std::vector<uint32_t>> incoming(model.nodes.size());
    for (uint32_t index = 0; index < model.edges.size(); ++index) {
        incoming[model.edges[index].toNode].push_back(index);
    }
    uint32_t maxLayer = 0;
    for (const uint32_t passIndex : rows.schedule) {
        uint32_t layer = 0;
        for (const uint32_t edge : incoming[passIndex]) {
            layer = std::max(layer, model.nodes[model.edges[edge].fromNode].layer + 1);
        }
        model.nodes[passIndex].layer = layer;
        maxLayer = std::max(maxLayer, layer);
    }
    for (uint32_t index = passCount; index < model.nodes.size(); ++index) {
        GraphNode& node = model.nodes[index];
        LMX_ASSERT(node.producerPass.has_value(), "a sink node has no producing pass");
        node.layer = model.nodes[*node.producerPass].layer + 1;
        maxLayer = std::max(maxLayer, node.layer);
    }

    // Ranking visits the scheduled passes in execution order and then the sinks in declaration
    // order, so within a layer the passes come first and every tie breaks the same way twice.
    std::vector<uint32_t> ranked(rows.schedule);
    for (uint32_t index = passCount; index < model.nodes.size(); ++index) {
        ranked.push_back(index);
    }
    std::vector<uint32_t> nextRank(maxLayer + 1, 0);
    uint32_t maxRank = 0;
    for (const uint32_t nodeIndex : ranked) {
        GraphNode& node = model.nodes[nodeIndex];
        node.rank = nextRank[node.layer]++;
        node.x = static_cast<float>(node.layer) * kGraphNodeColumnSpacing;
        node.y = static_cast<float>(node.rank) * kGraphNodeRowSpacing;
        maxRank = std::max(maxRank, node.rank);
    }

    const uint32_t bandRank = ranked.empty() ? 0 : maxRank + 1;
    uint32_t culledOrdinal = 0;
    for (uint32_t index = 0; index < passCount; ++index) {
        GraphNode& node = model.nodes[index];
        if (!node.cullReason) {
            continue;
        }
        node.layer = culledOrdinal;
        node.rank = bandRank;
        node.x = static_cast<float>(culledOrdinal) * kGraphNodeColumnSpacing;
        node.y = static_cast<float>(bandRank) * kGraphNodeRowSpacing + kGraphCulledBandGap;
        ++culledOrdinal;
    }

    for (const render::DebugTransition& transition : debug.transitions) {
        if (!transition.aliasedFrom) {
            continue;
        }
        const GraphInspectorTransientRow* freed = findTransient(rows, *transition.aliasedFrom);
        const GraphInspectorTransientRow* taking = findTransient(rows, transition.resource);
        LMX_ASSERT(freed && freed->used && taking && taking->used,
                   "a reuse boundary names a transient the frame never placed");
        model.aliasLinks.push_back({.fromNode = freed->lastPass,
                                    .toNode = transition.beforePass,
                                    .freedResource = freed->resource,
                                    .resource = taking->resource,
                                    .freedResourceName = freed->resourceName,
                                    .resourceName = taking->resourceName,
                                    .offset = taking->offset,
                                    .size = taking->size});
    }

    model.shapeSignature = buildShapeSignature(model);
    return model;
}

} // namespace lmx::app
