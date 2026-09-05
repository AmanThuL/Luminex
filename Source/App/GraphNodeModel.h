//----------------------------------------------------------------------------------------------------------------------
/// @file GraphNodeModel.h
/// @brief Declares the ImGui-free node, edge, and layout shaping behind the Render Graph canvas.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/GraphInspectorModel.h"
#include "RHI/RHI.h"
#include "Render/RenderGraph.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lmx::app {

/// Horizontal distance between two adjacent layers, in canvas units. Pins carry full resource
/// names, so a column must fit two pin columns (inputs and outputs) sized to the shipped
/// renderer's longest names; this model is ImGui-free and cannot measure text itself, so the
/// value is a fixed constant sized generously for the renderer's real pass names rather than a
/// computed fit.
inline constexpr float kGraphNodeColumnSpacing = 520.0f;

/// Vertical distance between two adjacent ranks within a layer, in canvas units.
inline constexpr float kGraphNodeRowSpacing = 140.0f;

/// Vertical gap between the scheduled graph's last rank and the culled band, in canvas units. It
/// exists so a culled pass is never read as one more rank of the DAG that was proved.
inline constexpr float kGraphCulledBandGap = 120.0f;

/// What a node stands for. Imported resources are deliberately absent: they are pins, not nodes,
/// because their contents came from outside the frame and no node in the frame produced them.
enum class GraphNodeKind {
    Pass, ///< One declared pass, scheduled or culled.
    Sink  ///< One declared sink, the endpoint that roots a result.
};

/// One endpoint on a node, naming the exact resource version that flows through it.
struct GraphNodePin {
    uint32_t resource = 0;    ///< Index into GraphInspectorModel::resources.
    std::string resourceName; ///< The resource's name, copied for direct display.
    uint32_t version = 0;     ///< The version this pin carries.
    std::string label;        ///< Display text, `r3 "sceneColor" v1`.
};

/// One graph-created transient whose lifetime spans the node reporting it, with the placement it
/// was assigned. It answers "what memory is live while this pass runs" without a second lookup.
struct GraphNodeTransientSpan {
    uint32_t resource = 0;    ///< Index into GraphInspectorModel::resources.
    std::string resourceName; ///< The resource's name, copied for direct display.
    uint64_t offset = 0;      ///< Byte offset of its placement within the frame's transient heap.
    uint64_t size = 0;        ///< Bytes the RHI reports the descriptor occupies in a heap.
    uint64_t alignment = 0;   ///< Alignment the RHI reports its heap offset must satisfy.
    bool aliases = false;     ///< Whether it took memory an earlier transient's lifetime had freed.
};

/// One box on the canvas: a declared pass or a declared sink, with everything the details pane
/// shows for it and the position the layout put it at.
///
/// A culled pass carries its declarations and its cull reason but has no pins and no edges, because
/// it is not part of the graph the compiler proved. `passKind` is meaningless on a sink node and
/// `sinkKind`/`producerPass` are meaningless on a pass node; `kind` says which to read.
struct GraphNode {
    GraphNodeKind kind = GraphNodeKind::Pass; ///< Which half of this node is meaningful.
    uint32_t index = 0;                       ///< Pass declaration index, or sink index.
    render::PassKind passKind = render::PassKind::Raster; ///< Passes: the declaration path used.
    std::string label; ///< Passes: the declared label. Sinks: "export", "present" or "readback".
    std::optional<uint32_t> scheduleOrder; ///< Execution position, or empty if culled or a sink.
    std::optional<render::CullReason> cullReason; ///< Why it was culled, or empty if scheduled.
    std::optional<double> gpuMilliseconds;        ///< Joined GPU time, or empty if unmeasured.
    std::vector<GraphNodePin> inputs;             ///< Distinct versions consumed; empty if culled.
    std::vector<GraphNodePin> outputs;            ///< Distinct versions produced; sinks have none.
    std::vector<GraphInspectorUseRow> uses;       ///< Every resource version it declared.
    std::vector<GraphInspectorTransitionRow> barriersBefore; ///< Barriers emitted ahead of it.
    std::vector<GraphNodeTransientSpan> transientsAlive;     ///< Transients live across it.
    render::SinkKind sinkKind = render::SinkKind::Export;    ///< Sinks: how the result leaves.
    std::optional<uint32_t> producerPass; ///< Sinks: the pass that wrote the rooted version.
    uint32_t layer = 0;                   ///< Column the layout assigned.
    uint32_t rank = 0;                    ///< Row within the layer.
    float x = 0.0f;                       ///< Horizontal position, in canvas units.
    float y = 0.0f;                       ///< Vertical position, in canvas units.
};

/// One execution dependency: the version an output pin produced arriving at the input pin that
/// consumes it. Alias links are not edges and live in their own list.
struct GraphNodeEdge {
    uint32_t fromNode = 0;    ///< Index into GraphNodeModel::nodes of the producing pass.
    uint32_t fromPin = 0;     ///< Index into that node's `outputs`.
    uint32_t toNode = 0;      ///< Index into GraphNodeModel::nodes of the consumer.
    uint32_t toPin = 0;       ///< Index into that node's `inputs`.
    uint32_t resource = 0;    ///< Index into GraphInspectorModel::resources.
    uint32_t version = 0;     ///< The version travelling along it.
    std::string resourceName; ///< The resource's name, copied for direct display.
};

/// One transient reuse boundary: the point at which a freed transient's bytes become another
/// transient's. It is memory changing hands rather than a declared dependency, which is why it is
/// listed apart from the execution edges and drawn differently.
struct GraphAliasLink {
    uint32_t fromNode = 0;         ///< The last scheduled pass that touched the freed transient.
    uint32_t toNode = 0;           ///< The pass the reuse barrier precedes.
    uint32_t freedResource = 0;    ///< Index of the transient whose memory was handed over.
    uint32_t resource = 0;         ///< Index of the transient that took it.
    std::string freedResourceName; ///< The freed transient's name, copied for display.
    std::string resourceName;      ///< The taking transient's name, copied for display.
    uint64_t offset = 0;           ///< Byte offset of the shared placement.
    uint64_t size = 0;             ///< Bytes of the shared placement.
};

/// One compiled frame shaped as a drawable graph: nodes, pins, execution edges, reuse links, and a
/// deterministic layered layout.
///
/// It is a pure function of its inputs -- the same record and timings always shape to the same
/// nodes at the same positions, on every machine and run -- and carries no ImGui or node-editor
/// type, so it is unit-testable without a UI. `nodes` holds every declared pass in declaration
/// order followed by every declared sink in declaration order, so a pass's node index is its
/// declaration index and a sink's is the pass count plus its sink index.
struct GraphNodeModel {
    uint64_t frameId = 0;                   ///< The device frame number.
    bool poolingEnabled = false;            ///< Whether aliasing was allowed this frame.
    render::TransientMemory memory;         ///< What transients cost, what aliasing saved.
    std::vector<GraphNode> nodes;           ///< Passes in declaration order, then sinks.
    std::vector<GraphNodeEdge> edges;       ///< Execution dependencies only.
    std::vector<GraphAliasLink> aliasLinks; ///< Transient reuse boundaries.
    std::string shapeSignature;             ///< Identity of the drawn shape; see below.
};

/// Shapes a compiled frame record and its joined GPU timings into a drawable node graph.
///
/// Pins and edges come from the graph's version contract alone: an import is version 0, a pass
/// writing version `v` produces `v + 1`, and the producer of `(r, v + 1)` is the scheduled pass
/// whose use of `(r, v)` writes. A version-0 input pin stays unconnected, since nothing in the
/// frame produced it. Culled passes keep their declarations for the details pane but get no pins
/// and no edges, and sit in a band below the scheduled graph.
///
/// The node facts -- uses, barriers, transient spans, and the timing join -- are the rows
/// buildGraphInspectorModel() produces for the same record, so the canvas and the list can never
/// disagree about one frame. A record whose sinks or scheduled uses name a version no scheduled
/// pass produced, or whose producer runs after its consumer, is a compiler contract violation and
/// asserts rather than being drawn.
///
/// `shapeSignature` identifies the drawn shape and nothing else: node identities, pins, edges, and
/// alias links, with timings, frame ID, byte offsets, and memory totals excluded. Two frames with
/// equal signatures are the same graph, which is what lets a caller keep dragged node positions
/// across frames and know when it must not.
GraphNodeModel buildGraphNodeModel(const render::CompiledFrameRecord& record,
                                   std::span<const rhi::PassTiming> timings);

} // namespace lmx::app
