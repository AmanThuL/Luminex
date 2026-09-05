#include <catch2/catch_test_macros.hpp>

#include "App/GraphInspectorModel.h"
#include "App/GraphNodeModel.h"
#include "GraphTestSupport.h"
#include "Render/RenderGraph.h"
#include "Render/TransientPool.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::app;
using namespace lmx::render;

namespace {

// The model reads a texture's kind, format, and range text off the compiled record alone -- never
// the rhi::Texture behind it -- so a fake needs to be real enough for RenderGraph::compile() to
// accept, and nothing more.
struct FakeTexture final : rhi::Texture {

    //==================================================================================================================
    explicit FakeTexture(uint32_t extent, uint32_t mips = 1, uint32_t layers = 1)
        : m_extent(extent), m_mipLevels(mips), m_arrayLayers(layers) {}

    //==================================================================================================================
    uint32_t width() const override { return m_extent; }

    //==================================================================================================================
    uint32_t height() const override { return m_extent; }

    //==================================================================================================================
    rhi::Format format() const override { return rhi::Format::Unknown; }

    //==================================================================================================================
    uint32_t mipLevels() const override { return m_mipLevels; }

    //==================================================================================================================
    uint32_t arrayLayers() const override { return m_arrayLayers; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    uint32_t m_extent = 0;
    uint32_t m_mipLevels = 1;
    uint32_t m_arrayLayers = 1;
};

struct FakeBuffer final : rhi::Buffer {

    //==================================================================================================================
    explicit FakeBuffer(uint64_t size) : m_size(size) {}

    //==================================================================================================================
    uint64_t size() const override { return m_size; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    uint64_t m_size = 0;
};

const ExecuteFn kNoWork = [](const PassResources&) {};

// A frame whose declaration order deliberately differs from its proved schedule: the scene pass is
// declared before the shadow pass it depends on, so an edge that merely followed declaration order
// would run backwards and be caught.
struct BaseFrame {
    FakeTexture shadowMap{1024};
    FakeTexture sceneColor{64};
    FakeTexture sceneDepth{64};
    FakeTexture displayColor{64};
    FakeTexture extraColor{64};
    RenderGraph graph;
    GraphTexture shadow;
    GraphTexture color;
    GraphTexture depth;
    GraphTexture display;
    GraphTexture extra;
};

// The four one-thing-at-a-time deviations the shape signature has to notice.
struct BaseFrameOptions {
    // Declares one more scheduled pass, plus the sink that keeps it alive.
    bool extraPass = false;
    // Roots the shadow map as well, adding a sink node to a shape that is otherwise unchanged.
    bool extraSink = false;
    // Moves the display pass's one input edge from the scene colour to the scene depth.
    bool displayReadsDepth = false;
};

//======================================================================================================================
void declareBaseFrame(BaseFrame& frame, const BaseFrameOptions& options) {
    RenderGraph& graph = frame.graph;
    frame.shadow = graph.importTexture(frame.shadowMap, rhi::Format::D32Float, "lmx.shadowMap");
    frame.color = graph.importTexture(frame.sceneColor, rhi::Format::RGBA16Float, "lmx.sceneColor");
    frame.depth = graph.importTexture(frame.sceneDepth, rhi::Format::D32Float, "lmx.sceneDepth");
    frame.display =
        graph.importTexture(frame.displayColor, rhi::Format::BGRA8Unorm, "lmx.displayColor");
    frame.extra = graph.importTexture(frame.extraColor, rhi::Format::BGRA8Unorm, "lmx.extraColor");

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(frame.shadow));
    scene.color = ColorAttachment{.handle = frame.color};
    scene.depth = DepthAttachment{.handle = frame.depth, .store = StoreOp::Store};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = frame.shadow, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadowPass, kNoWork);

    PassDesc displayPass;
    displayPass.textureReads.push_back(options.displayReadsDepth ? nextVersion(frame.depth)
                                                                 : nextVersion(frame.color));
    displayPass.color = ColorAttachment{.handle = frame.display};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);

    if (options.extraPass) {
        PassDesc extraPass;
        extraPass.color = ColorAttachment{.handle = frame.extra};
        graph.addPass("lmx.pass.extra", extraPass, kNoWork);
    }

    graph.presentTexture(nextVersion(frame.display));
    if (options.extraPass) {
        graph.exportTexture(nextVersion(frame.extra));
    }
    if (options.extraSink) {
        graph.exportTexture(nextVersion(frame.shadow));
    }
}

// Two transients whose lifetimes overlap by one pass, so neither can take the other's memory and
// the second one's offset is the first one's size -- a placement that moves when the extent does
// while the declared shape stays exactly the same.
struct OverlappingTransientFrame {
    FakeDevice device;
    TransientPool pool{device};
    FakeTexture displayColor{64};
    RenderGraph graph{pool};
};

//======================================================================================================================
void declareOverlappingTransients(OverlappingTransientFrame& frame, uint32_t extent) {
    RenderGraph& graph = frame.graph;
    const GraphTexture first = graph.createTexture({.width = extent,
                                                    .height = extent,
                                                    .format = rhi::Format::RGBA16Float,
                                                    .renderTarget = true,
                                                    .sampled = true},
                                                   "lmx.transient.first");
    const GraphTexture second = graph.createTexture({.width = extent,
                                                     .height = extent,
                                                     .format = rhi::Format::RGBA16Float,
                                                     .renderTarget = true,
                                                     .sampled = true},
                                                    "lmx.transient.second");
    const GraphTexture display =
        graph.importTexture(frame.displayColor, rhi::Format::BGRA8Unorm, "lmx.displayColor");

    PassDesc writeFirst;
    writeFirst.color = ColorAttachment{.handle = first};
    graph.addPass("lmx.pass.first", writeFirst, kNoWork);

    PassDesc bridge;
    bridge.textureReads.push_back(nextVersion(first));
    bridge.color = ColorAttachment{.handle = second};
    graph.addPass("lmx.pass.bridge", bridge, kNoWork);

    PassDesc resolve;
    resolve.textureReads.push_back(nextVersion(second));
    resolve.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.resolve", resolve, kNoWork);

    graph.presentTexture(nextVersion(display));
}

// Two transients whose lifetimes are disjoint, so with pooling on the second takes the first's
// bytes and compilation emits the reuse boundary an alias link is drawn from.
struct AliasingTransientFrame {
    FakeDevice device;
    TransientPool pool{device};
    FakeTexture midTarget{64};
    FakeTexture displayColor{64};
    RenderGraph graph{pool};
};

//======================================================================================================================
void declareAliasingTransients(AliasingTransientFrame& frame) {
    RenderGraph& graph = frame.graph;
    const GraphTexture scene = graph.createTexture({.width = 64,
                                                    .height = 64,
                                                    .format = rhi::Format::RGBA16Float,
                                                    .renderTarget = true,
                                                    .sampled = true},
                                                   "lmx.transient.sceneColor");
    const GraphTexture bloom = graph.createTexture({.width = 64,
                                                    .height = 64,
                                                    .format = rhi::Format::RGBA16Float,
                                                    .renderTarget = true,
                                                    .sampled = true},
                                                   "lmx.transient.bloom");
    const GraphTexture mid =
        graph.importTexture(frame.midTarget, rhi::Format::BGRA8Unorm, "lmx.mid");
    const GraphTexture display =
        graph.importTexture(frame.displayColor, rhi::Format::BGRA8Unorm, "lmx.displayColor");

    PassDesc writeScene;
    writeScene.color = ColorAttachment{.handle = scene};
    graph.addPass("lmx.pass.scene", writeScene, kNoWork);

    PassDesc resolve;
    resolve.textureReads.push_back(nextVersion(scene));
    resolve.color = ColorAttachment{.handle = mid};
    graph.addPass("lmx.pass.resolve", resolve, kNoWork);

    PassDesc writeBloom;
    writeBloom.color = ColorAttachment{.handle = bloom};
    graph.addPass("lmx.pass.bloom", writeBloom, kNoWork);

    PassDesc composite;
    composite.textureReads.push_back(nextVersion(bloom));
    composite.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.composite", composite, kNoWork);

    graph.exportTexture(nextVersion(mid));
    graph.presentTexture(nextVersion(display));
}

//======================================================================================================================
const GraphNodeEdge* edgeInto(const GraphNodeModel& model, uint32_t toNode, uint32_t toPin) {
    for (const GraphNodeEdge& edge : model.edges) {
        if (edge.toNode == toNode && edge.toPin == toPin) {
            return &edge;
        }
    }
    return nullptr;
}

//======================================================================================================================
std::string signatureOfBaseFrame(const BaseFrameOptions& options) {
    BaseFrame frame;
    declareBaseFrame(frame, options);
    const auto record = frame.graph.compileFrame(1);
    REQUIRE(record.has_value());
    return buildGraphNodeModel(*record, {}).shapeSignature;
}

} // namespace

//======================================================================================================================
// Every edge has to name the pass that actually produced the version, and the schedule the compiler
// proved has to agree: a producer that ran after its consumer would be a picture of a frame that
// cannot have happened.
TEST_CASE("execution edges name the producing pass and always run forward", "[app]") {
    BaseFrame frame;
    declareBaseFrame(frame, {});
    const auto record = frame.graph.compileFrame(12);
    REQUIRE(record.has_value());

    const GraphNodeModel model = buildGraphNodeModel(*record, {});

    // Three declared passes then one present sink; passes keep declaration order.
    REQUIRE(model.nodes.size() == 4);
    REQUIRE(model.nodes[0].label == "lmx.pass.scene");
    REQUIRE(model.nodes[1].label == "lmx.pass.shadow");
    REQUIRE(model.nodes[2].label == "lmx.pass.display");
    REQUIRE(model.nodes[3].kind == GraphNodeKind::Sink);

    // The scene pass reads shadowMap v1, which only the later-declared shadow pass writes.
    const GraphNodeEdge* toScene = edgeInto(model, 0, 0);
    REQUIRE(toScene != nullptr);
    REQUIRE(toScene->fromNode == 1);
    REQUIRE(toScene->resource == frame.shadow.index);
    REQUIRE(toScene->version == 1);
    REQUIRE(toScene->resourceName == "lmx.shadowMap");

    // scene -> display, display -> present sink, shadow -> scene: nothing else is an edge.
    REQUIRE(model.edges.size() == 3);

    for (const GraphNodeEdge& edge : model.edges) {
        const GraphNode& from = model.nodes[edge.fromNode];
        const GraphNode& to = model.nodes[edge.toNode];
        REQUIRE(from.kind == GraphNodeKind::Pass);
        REQUIRE(from.scheduleOrder.has_value());
        // Both endpoints agree with the edge about which version travels along it.
        REQUIRE(from.outputs[edge.fromPin].resource == edge.resource);
        REQUIRE(from.outputs[edge.fromPin].version == edge.version);
        REQUIRE(to.inputs[edge.toPin].resource == edge.resource);
        REQUIRE(to.inputs[edge.toPin].version == edge.version);
        if (to.kind == GraphNodeKind::Pass) {
            REQUIRE(to.scheduleOrder.has_value());
            REQUIRE(*from.scheduleOrder < *to.scheduleOrder);
        }
    }
}

//======================================================================================================================
// An imported resource is not a node. Version 0 is contents the frame did not produce, so the pin
// that names it stays honestly unconnected rather than being rooted in an invented source node.
TEST_CASE("a version-0 import is an unconnected input pin with no edge", "[app]") {
    BaseFrame frame;
    declareBaseFrame(frame, {});
    const auto record = frame.graph.compileFrame(4);
    REQUIRE(record.has_value());

    const GraphNodeModel model = buildGraphNodeModel(*record, {});

    // The scene pass names its colour attachment at version 0 -- the imported contents.
    const GraphNode& scene = model.nodes[0];
    REQUIRE(scene.inputs.size() == 3);
    REQUIRE(scene.inputs[1].resource == frame.color.index);
    REQUIRE(scene.inputs[1].version == 0);
    REQUIRE(scene.inputs[1].label == "r1 \"lmx.sceneColor\" v0");

    for (uint32_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
        const GraphNode& node = model.nodes[nodeIndex];
        for (uint32_t pin = 0; pin < node.inputs.size(); ++pin) {
            if (node.inputs[pin].version == 0) {
                REQUIRE(edgeInto(model, nodeIndex, pin) == nullptr);
            }
        }
    }

    // Writing version 0 still produces version 1, so the pass has an output pin to hand on.
    REQUIRE(scene.outputs.size() == 2);
    REQUIRE(scene.outputs[0].resource == frame.color.index);
    REQUIRE(scene.outputs[0].version == 1);
}

//======================================================================================================================
// A sink is the endpoint that roots the frame. It carries the pass that produced what it roots, so
// the details pane can name it without walking the edge list back.
TEST_CASE("a sink node has one edge from its producer and the producing pass recorded", "[app]") {
    BaseFrame frame;
    declareBaseFrame(frame, {});
    const auto record = frame.graph.compileFrame(5);
    REQUIRE(record.has_value());

    const GraphNodeModel model = buildGraphNodeModel(*record, {});

    const GraphNode& sink = model.nodes[3];
    REQUIRE(sink.kind == GraphNodeKind::Sink);
    REQUIRE(sink.index == 0);
    REQUIRE(sink.label == "present");
    REQUIRE(sink.sinkKind == SinkKind::Present);
    REQUIRE(sink.producerPass == 2u);
    REQUIRE_FALSE(sink.scheduleOrder.has_value());
    REQUIRE(sink.outputs.empty());
    REQUIRE(sink.inputs.size() == 1);
    REQUIRE(sink.inputs[0].resource == frame.display.index);
    REQUIRE(sink.inputs[0].version == 1);

    const GraphNodeEdge* rooted = edgeInto(model, 3, 0);
    REQUIRE(rooted != nullptr);
    REQUIRE(rooted->fromNode == 2);
    REQUIRE(rooted->resourceName == "lmx.displayColor");

    // Nothing depends on a sink.
    for (const GraphNodeEdge& edge : model.edges) {
        REQUIRE(edge.fromNode != 3);
    }
}

//======================================================================================================================
// Culled work is not part of the DAG the compiler proved. It keeps its declarations for the details
// pane but gets no pin and no edge, so nothing in the drawn graph can depend on it.
TEST_CASE("a culled pass has no pins and no edges", "[app]") {
    FakeTexture color{64};
    FakeTexture displayed{64};
    FakeTexture unused{64};
    FakeBuffer probe{256};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture displayColor =
        graph.importTexture(displayed, rhi::Format::BGRA8Unorm, "displayColor");
    const GraphTexture orphan =
        graph.importTexture(unused, rhi::Format::BGRA8Unorm, "unusedTarget");
    const GraphBuffer stats = graph.importBuffer(probe, "probe");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc orphaned;
    orphaned.color = ColorAttachment{.handle = orphan};
    graph.addPass("lmx.pass.orphan", orphaned, kNoWork);

    ComputePassDesc observer;
    observer.textureReads.push_back(nextVersion(sceneColor));
    observer.bufferReads.push_back(stats);
    graph.addComputePass("lmx.pass.observer", observer, kNoWork);

    PassDesc display;
    display.textureReads.push_back(nextVersion(sceneColor));
    display.color = ColorAttachment{.handle = displayColor};
    graph.addPass("lmx.pass.display", display, kNoWork);

    graph.exportTexture(nextVersion(displayColor));

    const auto record = graph.compileFrame(3);
    REQUIRE(record.has_value());

    const GraphNodeModel model = buildGraphNodeModel(*record, {});

    const GraphNode& orphanNode = model.nodes[1];
    const GraphNode& observerNode = model.nodes[2];
    REQUIRE(orphanNode.cullReason == CullReason::NoSinkReachesIt);
    REQUIRE(observerNode.cullReason == CullReason::ProducesNothing);

    for (const GraphNode& node : {orphanNode, observerNode}) {
        REQUIRE(node.inputs.empty());
        REQUIRE(node.outputs.empty());
        REQUIRE_FALSE(node.scheduleOrder.has_value());
        // Its declarations are still there for the details pane to read.
        REQUIRE_FALSE(node.uses.empty());
    }
    for (const GraphNodeEdge& edge : model.edges) {
        REQUIRE(edge.fromNode != 1);
        REQUIRE(edge.toNode != 1);
        REQUIRE(edge.fromNode != 2);
        REQUIRE(edge.toNode != 2);
    }
}

//======================================================================================================================
// A reuse boundary is memory being handed over, not a dependency the frame declared. It comes only
// from a transition that says so, which is exactly what turning pooling off takes away.
TEST_CASE("alias links come only from aliasedFrom transitions", "[app]") {
    AliasingTransientFrame pooled;
    declareAliasingTransients(pooled);
    const auto record = pooled.graph.compileFrame(9);
    REQUIRE(record.has_value());

    size_t aliasedTransitions = 0;
    for (const DebugTransition& transition : record->debug.transitions) {
        aliasedTransitions += transition.aliasedFrom.has_value() ? 1 : 0;
    }
    REQUIRE(aliasedTransitions == 1);

    const GraphNodeModel model = buildGraphNodeModel(*record, {});

    REQUIRE(model.aliasLinks.size() == aliasedTransitions);
    const GraphAliasLink& link = model.aliasLinks[0];
    // From the last scheduled pass that touched the freed transient to the pass that takes over.
    REQUIRE(link.fromNode == 1); // lmx.pass.resolve, the sceneColor transient's last reader
    REQUIRE(link.toNode == 2);   // lmx.pass.bloom, which writes into those bytes
    REQUIRE(link.freedResourceName == "lmx.transient.sceneColor");
    REQUIRE(link.resourceName == "lmx.transient.bloom");
    REQUIRE(link.size == 16384);
    REQUIRE(link.offset == 0);

    // The transients a pass's node reports alive are the ones spanning its schedule position.
    REQUIRE(model.nodes[0].transientsAlive.size() == 1);
    REQUIRE(model.nodes[0].transientsAlive[0].resourceName == "lmx.transient.sceneColor");
    REQUIRE(model.nodes[3].transientsAlive.size() == 1);
    REQUIRE(model.nodes[3].transientsAlive[0].resourceName == "lmx.transient.bloom");
    REQUIRE(model.nodes[3].transientsAlive[0].aliases);

    // Same declarations, pooling off: nothing is handed over, so there is nothing to draw.
    AliasingTransientFrame separate;
    separate.graph.setPoolingEnabled(false);
    declareAliasingTransients(separate);
    const auto unpooled = separate.graph.compileFrame(9);
    REQUIRE(unpooled.has_value());

    const GraphNodeModel unpooledModel = buildGraphNodeModel(*unpooled, {});
    REQUIRE(unpooledModel.aliasLinks.empty());
}

//======================================================================================================================
// Two frames with equal signatures are the same graph, so the signature must ignore everything a
// run measured or a heap packer decided and must notice every change to the drawn shape.
TEST_CASE("the shape signature ignores measurements and notices shape changes", "[app]") {
    BaseFrame frame;
    declareBaseFrame(frame, {});
    const auto record = frame.graph.compileFrame(2);
    REQUIRE(record.has_value());
    const std::array<rhi::PassTiming, 1> timings = {
        rhi::PassTiming{.label = "lmx.pass.shadow", .gpuMilliseconds = 7.25}};

    BaseFrame other;
    declareBaseFrame(other, {});
    const auto otherRecord = other.graph.compileFrame(4242);
    REQUIRE(otherRecord.has_value());

    const std::string baseline = buildGraphNodeModel(*record, {}).shapeSignature;
    REQUIRE_FALSE(baseline.empty());
    REQUIRE(buildGraphNodeModel(*otherRecord, timings).shapeSignature == baseline);

    // The same declared shape over transients twice the size: every placement moves, the picture
    // does not.
    OverlappingTransientFrame small;
    declareOverlappingTransients(small, 64);
    const auto smallRecord = small.graph.compileFrame(1);
    REQUIRE(smallRecord.has_value());
    OverlappingTransientFrame large;
    declareOverlappingTransients(large, 128);
    const auto largeRecord = large.graph.compileFrame(1);
    REQUIRE(largeRecord.has_value());
    REQUIRE(smallRecord->debug.transients[1].offset != largeRecord->debug.transients[1].offset);
    REQUIRE(buildGraphNodeModel(*smallRecord, {}).shapeSignature ==
            buildGraphNodeModel(*largeRecord, {}).shapeSignature);

    // One declaration changed at a time.
    REQUIRE(signatureOfBaseFrame({.extraPass = true}) != baseline);
    REQUIRE(signatureOfBaseFrame({.extraSink = true}) != baseline);
    REQUIRE(signatureOfBaseFrame({.displayReadsDepth = true}) != baseline);

    // An alias link is part of the shape too: the same frame with pooling off is a different one.
    AliasingTransientFrame pooled;
    declareAliasingTransients(pooled);
    const auto pooledRecord = pooled.graph.compileFrame(1);
    REQUIRE(pooledRecord.has_value());
    AliasingTransientFrame separate;
    separate.graph.setPoolingEnabled(false);
    declareAliasingTransients(separate);
    const auto separateRecord = separate.graph.compileFrame(1);
    REQUIRE(separateRecord.has_value());
    REQUIRE(buildGraphNodeModel(*pooledRecord, {}).shapeSignature !=
            buildGraphNodeModel(*separateRecord, {}).shapeSignature);
}

//======================================================================================================================
// The node model shapes the same frame the list panel shaped, so a node's facts have to be the very
// rows the inspector reports -- not a second derivation that could drift from it.
TEST_CASE("node facts equal the inspector rows for the same pass", "[app]") {
    AliasingTransientFrame frame;
    declareAliasingTransients(frame);
    const auto record = frame.graph.compileFrame(11);
    REQUIRE(record.has_value());

    const std::array<rhi::PassTiming, 4> timings = {
        rhi::PassTiming{.label = "lmx.pass.scene", .gpuMilliseconds = 0.1},
        rhi::PassTiming{.label = "lmx.pass.resolve", .gpuMilliseconds = 0.2},
        rhi::PassTiming{.label = "lmx.pass.bloom", .gpuMilliseconds = 0.3},
        rhi::PassTiming{.label = "lmx.pass.composite", .gpuMilliseconds = 0.4}};

    const GraphInspectorModel rows = buildGraphInspectorModel(*record, timings);
    const GraphNodeModel model = buildGraphNodeModel(*record, timings);

    REQUIRE(model.frameId == rows.frameId);
    REQUIRE(model.poolingEnabled == rows.poolingEnabled);
    REQUIRE(model.memory.requested == rows.memory.requested);
    REQUIRE(model.memory.highWater == rows.memory.highWater);
    REQUIRE(model.memory.aliasSavings == rows.memory.aliasSavings);
    REQUIRE(model.nodes.size() == rows.passes.size() + record->debug.sinks.size());

    for (uint32_t index = 0; index < rows.passes.size(); ++index) {
        const GraphInspectorPassRow& row = rows.passes[index];
        const GraphNode& node = model.nodes[index];
        REQUIRE(node.kind == GraphNodeKind::Pass);
        REQUIRE(node.index == index);
        REQUIRE(node.label == row.label);
        REQUIRE(node.passKind == row.kind);
        REQUIRE(node.cullReason == row.cullReason);
        REQUIRE(node.gpuMilliseconds == row.gpuMilliseconds);

        REQUIRE(node.uses.size() == row.uses.size());
        for (size_t use = 0; use < row.uses.size(); ++use) {
            REQUIRE(node.uses[use].resource == row.uses[use].resource);
            REQUIRE(node.uses[use].resourceName == row.uses[use].resourceName);
            REQUIRE(node.uses[use].version == row.uses[use].version);
            REQUIRE(node.uses[use].role == row.uses[use].role);
            REQUIRE(node.uses[use].rangeText == row.uses[use].rangeText);
        }

        std::vector<GraphInspectorTransitionRow> expected;
        for (const GraphInspectorTransitionRow& transition : rows.transitions) {
            if (transition.beforePass == index) {
                expected.push_back(transition);
            }
        }
        REQUIRE(node.barriersBefore.size() == expected.size());
        for (size_t barrier = 0; barrier < expected.size(); ++barrier) {
            REQUIRE(node.barriersBefore[barrier].resource == expected[barrier].resource);
            REQUIRE(node.barriersBefore[barrier].resourceName == expected[barrier].resourceName);
            REQUIRE(node.barriersBefore[barrier].description == expected[barrier].description);
            REQUIRE(node.barriersBefore[barrier].aliasedFrom == expected[barrier].aliasedFrom);
        }
    }

    // A pass carrying real barriers is what makes the comparison above worth making.
    REQUIRE_FALSE(model.nodes[1].barriersBefore.empty());
    REQUIRE(model.nodes[0].gpuMilliseconds == 0.1);
}
