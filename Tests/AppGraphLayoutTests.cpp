#include <catch2/catch_test_macros.hpp>

#include "App/GraphLayout.h"
#include "App/GraphNodeModel.h"
#include "GraphTestSupport.h"
#include "Render/RenderGraph.h"
#include "Render/TransientPool.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::app;
using namespace lmx::render;

namespace {

// The layout reads nothing but the compiled record, so a fake needs to be real enough for
// RenderGraph::compile() to accept and nothing more.
struct FakeTexture final : rhi::Texture {

    //==================================================================================================================
    explicit FakeTexture(uint32_t extent) : m_extent(extent) {}

    //==================================================================================================================
    uint32_t width() const override { return m_extent; }

    //==================================================================================================================
    uint32_t height() const override { return m_extent; }

    //==================================================================================================================
    rhi::Format format() const override { return rhi::Format::Unknown; }

    //==================================================================================================================
    uint32_t mipLevels() const override { return 1; }

    //==================================================================================================================
    uint32_t arrayLayers() const override { return 1; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    uint32_t m_extent = 0;
};

const ExecuteFn kNoWork = [](const PassResources&) {};

// A scene pass, a three-step bloom stage, and a tonemap: the four-segment labels that group, the
// three-segment label that cannot, and a four-segment label with nobody to group with.
struct BloomFrame {
    FakeTexture sceneColor{64};
    FakeTexture bloomA{64};
    FakeTexture bloomB{64};
    FakeTexture bloomC{64};
    FakeTexture displayColor{64};
    RenderGraph graph;
    GraphTexture scene;
    GraphTexture first;
    GraphTexture second;
    GraphTexture third;
    GraphTexture display;
};

//======================================================================================================================
void declareBloomFrame(BloomFrame& frame) {
    RenderGraph& graph = frame.graph;
    frame.scene =
        graph.importTexture(frame.sceneColor, rhi::Format::RGBA16Float, "lmx.render.sceneColorHdr");
    frame.first = graph.importTexture(frame.bloomA, rhi::Format::RGBA16Float, "lmx.render.bloomA");
    frame.second = graph.importTexture(frame.bloomB, rhi::Format::RGBA16Float, "lmx.render.bloomB");
    frame.third = graph.importTexture(frame.bloomC, rhi::Format::RGBA16Float, "lmx.render.bloomC");
    frame.display =
        graph.importTexture(frame.displayColor, rhi::Format::BGRA8Unorm, "lmx.render.displayColor");

    PassDesc scenePass;
    scenePass.color = ColorAttachment{.handle = frame.scene};
    graph.addPass("lmx.pass.scene", scenePass, kNoWork);

    PassDesc threshold;
    threshold.textureReads.push_back(nextVersion(frame.scene));
    threshold.color = ColorAttachment{.handle = frame.first};
    graph.addPass("lmx.pass.bloom.threshold", threshold, kNoWork);

    PassDesc downsample;
    downsample.textureReads.push_back(nextVersion(frame.first));
    downsample.color = ColorAttachment{.handle = frame.second};
    graph.addPass("lmx.pass.bloom.downsample0", downsample, kNoWork);

    // Reads the scene colour as well, so two members of the stage consume the same version from
    // outside it and one collapsed edge has to stand for both.
    PassDesc upsample;
    upsample.textureReads.push_back(nextVersion(frame.second));
    upsample.textureReads.push_back(nextVersion(frame.scene));
    upsample.color = ColorAttachment{.handle = frame.third};
    graph.addPass("lmx.pass.bloom.upsample0", upsample, kNoWork);

    PassDesc tonemap;
    tonemap.textureReads.push_back(nextVersion(frame.third));
    tonemap.color = ColorAttachment{.handle = frame.display};
    graph.addPass("lmx.pass.tonemap.apply", tonemap, kNoWork);

    graph.presentTexture(nextVersion(frame.display));
}

// One key whose passes disagree about being culled, and one whose two halves are a single pass
// each -- the split that must happen and the two groups that must not survive it.
struct SplitFrame {
    FakeTexture mid{64};
    FakeTexture displayColor{64};
    FakeTexture finalColor{64};
    FakeTexture orphanA{64};
    FakeTexture orphanB{64};
    FakeTexture orphanC{64};
    RenderGraph graph;
};

//======================================================================================================================
void declareSplitFrame(SplitFrame& frame) {
    RenderGraph& graph = frame.graph;
    const GraphTexture mid = graph.importTexture(frame.mid, rhi::Format::RGBA16Float, "lmx.mid");
    const GraphTexture display =
        graph.importTexture(frame.displayColor, rhi::Format::RGBA16Float, "lmx.displayColor");
    const GraphTexture out =
        graph.importTexture(frame.finalColor, rhi::Format::BGRA8Unorm, "lmx.finalColor");
    const GraphTexture spareA =
        graph.importTexture(frame.orphanA, rhi::Format::BGRA8Unorm, "lmx.orphanA");
    const GraphTexture spareB =
        graph.importTexture(frame.orphanB, rhi::Format::BGRA8Unorm, "lmx.orphanB");
    const GraphTexture spareC =
        graph.importTexture(frame.orphanC, rhi::Format::BGRA8Unorm, "lmx.orphanC");

    PassDesc wide;
    wide.color = ColorAttachment{.handle = mid};
    graph.addPass("lmx.pass.blur.wide", wide, kNoWork);

    PassDesc narrow;
    narrow.textureReads.push_back(nextVersion(mid));
    narrow.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.blur.narrow", narrow, kNoWork);

    PassDesc spare;
    spare.color = ColorAttachment{.handle = spareA};
    graph.addPass("lmx.pass.blur.spare", spare, kNoWork);

    PassDesc extra;
    extra.color = ColorAttachment{.handle = spareB};
    graph.addPass("lmx.pass.blur.extra", extra, kNoWork);

    PassDesc apply;
    apply.textureReads.push_back(nextVersion(display));
    apply.color = ColorAttachment{.handle = out};
    graph.addPass("lmx.pass.tone.apply", apply, kNoWork);

    PassDesc toneSpare;
    toneSpare.color = ColorAttachment{.handle = spareC};
    graph.addPass("lmx.pass.tone.spare", toneSpare, kNoWork);

    graph.presentTexture(nextVersion(out));
}

// A four-pass chain plus a sink rooting its first result: five layers, one of which holds two
// items, which is what makes a row's height depend on more than its layer count.
struct ChainFrame {
    std::array<FakeTexture, 4> targets{FakeTexture{64}, FakeTexture{64}, FakeTexture{64},
                                       FakeTexture{64}};
    RenderGraph graph;
};

//======================================================================================================================
void declareChainFrame(ChainFrame& frame) {
    RenderGraph& graph = frame.graph;
    const GraphTexture zero =
        graph.importTexture(frame.targets[0], rhi::Format::BGRA8Unorm, "lmx.chain0");
    const GraphTexture one =
        graph.importTexture(frame.targets[1], rhi::Format::BGRA8Unorm, "lmx.chain1");
    const GraphTexture two =
        graph.importTexture(frame.targets[2], rhi::Format::BGRA8Unorm, "lmx.chain2");
    const GraphTexture three =
        graph.importTexture(frame.targets[3], rhi::Format::BGRA8Unorm, "lmx.chain3");

    PassDesc a;
    a.color = ColorAttachment{.handle = zero};
    graph.addPass("lmx.chain.a", a, kNoWork);

    PassDesc b;
    b.textureReads.push_back(nextVersion(zero));
    b.color = ColorAttachment{.handle = one};
    graph.addPass("lmx.chain.b", b, kNoWork);

    PassDesc c;
    c.textureReads.push_back(nextVersion(one));
    c.color = ColorAttachment{.handle = two};
    graph.addPass("lmx.chain.c", c, kNoWork);

    PassDesc d;
    d.textureReads.push_back(nextVersion(two));
    d.color = ColorAttachment{.handle = three};
    graph.addPass("lmx.chain.d", d, kNoWork);

    graph.presentTexture(nextVersion(three));
    graph.exportTexture(nextVersion(zero));
}

// The frame the M5.4 model tests used for the culled band: a scheduled chain plus two passes
// nothing roots, whose labels are three-segment and therefore never group.
struct CulledFrame {
    FakeTexture sceneColor{64};
    FakeTexture displayColor{64};
    FakeTexture unusedColor{64};
    RenderGraph graph;
};

//======================================================================================================================
void declareCulledFrame(CulledFrame& frame) {
    RenderGraph& graph = frame.graph;
    const GraphTexture scene =
        graph.importTexture(frame.sceneColor, rhi::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture display =
        graph.importTexture(frame.displayColor, rhi::Format::BGRA8Unorm, "displayColor");
    const GraphTexture orphan =
        graph.importTexture(frame.unusedColor, rhi::Format::BGRA8Unorm, "unusedTarget");

    PassDesc scenePass;
    scenePass.color = ColorAttachment{.handle = scene};
    graph.addPass("lmx.pass.scene", scenePass, kNoWork);

    PassDesc orphaned;
    orphaned.color = ColorAttachment{.handle = orphan};
    graph.addPass("lmx.pass.orphan", orphaned, kNoWork);

    ComputePassDesc observer;
    observer.textureReads.push_back(nextVersion(scene));
    graph.addComputePass("lmx.pass.observer", observer, kNoWork);

    PassDesc displayPass;
    displayPass.textureReads.push_back(nextVersion(scene));
    displayPass.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);

    graph.exportTexture(nextVersion(display));
}

// Two transients whose lifetimes are disjoint, so with pooling on the second takes the first's
// bytes and compilation emits the reuse boundary an alias link is drawn from. The two passes on
// either side of that boundary share a stage, so collapsing it hides the handover entirely.
struct AliasFrame {
    FakeDevice device;
    TransientPool pool{device};
    FakeTexture midTarget{64};
    FakeTexture displayColor{64};
    RenderGraph graph{pool};
};

//======================================================================================================================
void declareAliasFrame(AliasFrame& frame) {
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
    graph.addPass("lmx.pass.post.resolve", resolve, kNoWork);

    PassDesc writeBloom;
    writeBloom.color = ColorAttachment{.handle = bloom};
    graph.addPass("lmx.pass.post.bloom", writeBloom, kNoWork);

    PassDesc composite;
    composite.textureReads.push_back(nextVersion(bloom));
    composite.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.composite", composite, kNoWork);

    graph.exportTexture(nextVersion(mid));
    graph.presentTexture(nextVersion(display));
}

// Nothing a sink reaches: every pass writes an imported target the frame never roots, so the
// proved DAG is empty and the whole picture is the culled band. The three-segment labels never
// group, which leaves the band holding one box per pass.
struct DeadFrame {
    FakeTexture orphanA{64};
    FakeTexture orphanB{64};
    RenderGraph graph;
};

//======================================================================================================================
void declareDeadFrame(DeadFrame& frame) {
    RenderGraph& graph = frame.graph;
    const GraphTexture spareA =
        graph.importTexture(frame.orphanA, rhi::Format::BGRA8Unorm, "lmx.orphanA");
    const GraphTexture spareB =
        graph.importTexture(frame.orphanB, rhi::Format::BGRA8Unorm, "lmx.orphanB");

    PassDesc first;
    first.color = ColorAttachment{.handle = spareA};
    graph.addPass("lmx.pass.dead", first, kNoWork);

    PassDesc second;
    second.color = ColorAttachment{.handle = spareB};
    graph.addPass("lmx.pass.gone", second, kNoWork);
}

// The same empty DAG, with two four-segment labels that do share a stage: the group they form is
// culled, so the band holds one box standing for both.
struct DeadStageFrame {
    FakeTexture orphanA{64};
    FakeTexture orphanB{64};
    RenderGraph graph;
};

//======================================================================================================================
void declareDeadStageFrame(DeadStageFrame& frame) {
    RenderGraph& graph = frame.graph;
    const GraphTexture spareA =
        graph.importTexture(frame.orphanA, rhi::Format::BGRA8Unorm, "lmx.orphanA");
    const GraphTexture spareB =
        graph.importTexture(frame.orphanB, rhi::Format::BGRA8Unorm, "lmx.orphanB");

    PassDesc first;
    first.color = ColorAttachment{.handle = spareA};
    graph.addPass("lmx.pass.blur.wide", first, kNoWork);

    PassDesc second;
    second.color = ColorAttachment{.handle = spareB};
    graph.addPass("lmx.pass.blur.narrow", second, kNoWork);
}

//======================================================================================================================
GraphNodeModel modelOf(RenderGraph& graph, uint64_t frameId,
                       std::span<const rhi::PassTiming> timings = {}) {
    const auto record = graph.compileFrame(frameId);
    REQUIRE(record.has_value());
    return buildGraphNodeModel(*record, timings);
}

//======================================================================================================================
const GraphLayoutGroup* groupByKey(const GraphLayout& layout, std::string_view key) {
    const auto found = std::ranges::find(layout.groups, key, &GraphLayoutGroup::key);
    return found == layout.groups.end() ? nullptr : &*found;
}

//======================================================================================================================
uint32_t itemCountOfKind(const GraphLayout& layout, GraphLayoutItemKind kind) {
    return static_cast<uint32_t>(std::ranges::count(layout.items, kind, &GraphLayoutItem::kind));
}

} // namespace

//======================================================================================================================
// A group exists only where it says something. Four segments make a stage, two members make it
// worth drawing as one box, and scheduled work never shares a box with work that never ran.
TEST_CASE("stage groups need four segments, two members, and one culled-ness", "[app]") {
    BloomFrame bloom;
    declareBloomFrame(bloom);
    const GraphNodeModel bloomModel = modelOf(bloom.graph, 1);
    const GraphLayout bloomLayout = layoutGraph(bloomModel, {});

    // lmx.pass.scene has three segments and lmx.pass.tonemap.apply has nobody to group with.
    REQUIRE(bloomLayout.groups.size() == 1);
    REQUIRE(bloomLayout.groups[0].key == "lmx.pass.bloom");
    REQUIRE(bloomLayout.groups[0].stage == "bloom");
    REQUIRE(bloomLayout.groups[0].members == std::vector<uint32_t>{1, 2, 3});
    REQUIRE_FALSE(bloomLayout.groups[0].culled);
    REQUIRE_FALSE(bloomLayout.groups[0].expanded);

    SplitFrame split;
    declareSplitFrame(split);
    const GraphNodeModel splitModel = modelOf(split.graph, 2);
    const GraphLayout splitLayout = layoutGraph(splitModel, {});

    // The frame really does mix scheduled and culled work under one key.
    REQUIRE(splitModel.nodes[2].cullReason.has_value());
    REQUIRE(splitModel.nodes[3].cullReason.has_value());
    REQUIRE_FALSE(splitModel.nodes[0].cullReason.has_value());

    REQUIRE(splitLayout.groups.size() == 2);
    const GraphLayoutGroup* scheduled = groupByKey(splitLayout, "lmx.pass.blur");
    const GraphLayoutGroup* culled = groupByKey(splitLayout, "lmx.pass.blur#culled");
    REQUIRE(scheduled != nullptr);
    REQUIRE(culled != nullptr);
    REQUIRE(scheduled->members == std::vector<uint32_t>{0, 1});
    REQUIRE(culled->members == std::vector<uint32_t>{2, 3});
    REQUIRE_FALSE(scheduled->culled);
    REQUIRE(culled->culled);
    REQUIRE(scheduled->stage == "blur");
    REQUIRE(culled->stage == "blur");

    // lmx.pass.tone splits into one scheduled and one culled pass, so neither half reaches two.
    REQUIRE(groupByKey(splitLayout, "lmx.pass.tone") == nullptr);
    REQUIRE(groupByKey(splitLayout, "lmx.pass.tone#culled") == nullptr);
}

//======================================================================================================================
// The point of collapsing is that a stage shows what crosses its boundary and hides what does not.
TEST_CASE("a collapsed group shows only its boundary pins and hides its internal edges", "[app]") {
    BloomFrame frame;
    declareBloomFrame(frame);
    const GraphNodeModel model = modelOf(frame.graph, 3);
    const GraphLayout layout = layoutGraph(model, {});

    // scene, the bloom group, tonemap, the present sink.
    REQUIRE(layout.items.size() == 4);
    REQUIRE(layout.itemOfNode == std::vector<uint32_t>{0, 1, 1, 1, 2, 3});
    REQUIRE(itemCountOfKind(layout, GraphLayoutItemKind::Group) == 1);

    const GraphLayoutItem& group = layout.items[1];
    REQUIRE(group.kind == GraphLayoutItemKind::Group);
    REQUIRE(group.index == 0);

    // Only the version entering and the version leaving; bloomA and bloomB stay inside.
    REQUIRE(group.inputs.size() == 1);
    REQUIRE(group.inputs[0].resourceName == "lmx.render.sceneColorHdr");
    REQUIRE(group.inputs[0].version == 1);
    REQUIRE(group.inputs[0].shortLabel == "sceneColorHdr v1");
    REQUIRE(group.inputs[0].label == "r0 \"lmx.render.sceneColorHdr\" v1");
    REQUIRE(group.outputs.size() == 1);
    REQUIRE(group.outputs[0].resourceName == "lmx.render.bloomC");
    REQUIRE(group.outputs[0].shortLabel == "bloomC v1");

    // Two members read lmx.render.sceneColorHdr v1, and the collapsed picture draws that once.
    REQUIRE(model.edges.size() == 6);
    REQUIRE(layout.edges.size() == 3);
    for (const GraphLayoutEdge& edge : layout.edges) {
        REQUIRE(edge.resourceName != "lmx.render.bloomA");
        REQUIRE(edge.resourceName != "lmx.render.bloomB");
        REQUIRE(edge.fromItem != edge.toItem);
        // Both endpoints agree with the edge about which version travels along it.
        const GraphLayoutItem& from = layout.items[edge.fromItem];
        const GraphLayoutItem& to = layout.items[edge.toItem];
        REQUIRE(from.outputs[edge.fromPin].resource == edge.resource);
        REQUIRE(from.outputs[edge.fromPin].version == edge.version);
        REQUIRE(to.inputs[edge.toPin].resource == edge.resource);
        REQUIRE(to.inputs[edge.toPin].version == edge.version);
    }

    // Layers are over items, so collapsing three passes into one box shortens the chain.
    REQUIRE(layout.items[0].layer == 0);
    REQUIRE(layout.items[1].layer == 1);
    REQUIRE(layout.items[2].layer == 2);
    REQUIRE(layout.items[3].layer == 3);
}

//======================================================================================================================
// Expanding is not a different picture of the graph, it is the same one with nothing hidden.
TEST_CASE("expanding a group draws exactly the model's own nodes and edges", "[app]") {
    BloomFrame frame;
    declareBloomFrame(frame);
    const GraphNodeModel model = modelOf(frame.graph, 4);
    const GraphLayout layout = layoutGraph(model, {.expandedGroups = {"lmx.pass.bloom"}});

    REQUIRE(layout.groups.size() == 1);
    REQUIRE(layout.groups[0].expanded);
    REQUIRE(itemCountOfKind(layout, GraphLayoutItemKind::Group) == 0);
    REQUIRE(layout.items.size() == model.nodes.size());

    for (uint32_t index = 0; index < model.nodes.size(); ++index) {
        REQUIRE(layout.itemOfNode[index] == index);
        const GraphLayoutItem& item = layout.items[index];
        REQUIRE(item.kind == GraphLayoutItemKind::Node);
        REQUIRE(item.index == index);
        REQUIRE(item.inputs.size() == model.nodes[index].inputs.size());
        REQUIRE(item.outputs.size() == model.nodes[index].outputs.size());
    }

    REQUIRE(layout.edges.size() == model.edges.size());
    for (uint32_t index = 0; index < model.edges.size(); ++index) {
        REQUIRE(layout.edges[index].fromItem == model.edges[index].fromNode);
        REQUIRE(layout.edges[index].fromPin == model.edges[index].fromPin);
        REQUIRE(layout.edges[index].toItem == model.edges[index].toNode);
        REQUIRE(layout.edges[index].toPin == model.edges[index].toPin);
    }
}

//======================================================================================================================
// A collapsed stage has to answer "what did this cost" for the passes it hid, and say honestly how
// many of them the driver actually measured.
TEST_CASE("a group sums the GPU time of its measured members and counts them", "[app]") {
    BloomFrame frame;
    declareBloomFrame(frame);
    // Timings arrive in schedule order; the last two scheduled passes go unmeasured.
    const std::array<rhi::PassTiming, 3> timings = {
        rhi::PassTiming{.label = "lmx.pass.scene", .gpuMilliseconds = 0.125},
        rhi::PassTiming{.label = "lmx.pass.bloom.threshold", .gpuMilliseconds = 0.5},
        rhi::PassTiming{.label = "lmx.pass.bloom.downsample0", .gpuMilliseconds = 0.25}};
    const GraphNodeModel model = modelOf(frame.graph, 5, timings);

    // The join really landed, so the sum below is not vacuous.
    REQUIRE(model.nodes[1].gpuMilliseconds == 0.5);
    REQUIRE_FALSE(model.nodes[3].gpuMilliseconds.has_value());

    const GraphLayout layout = layoutGraph(model, {});
    REQUIRE(layout.groups.size() == 1);
    REQUIRE(layout.groups[0].members.size() == 3);
    REQUIRE(layout.groups[0].measuredMembers == 2);
    REQUIRE(layout.groups[0].gpuMillisecondsSum == 0.75);

    // Nothing measured at all is an absent sum rather than a zero that reads like a fast stage.
    const GraphLayout unmeasured = layoutGraph(modelOf(frame.graph, 6), {});
    REQUIRE(unmeasured.groups[0].measuredMembers == 0);
    REQUIRE_FALSE(unmeasured.groups[0].gpuMillisecondsSum.has_value());
}

//======================================================================================================================
// Wrapping is what keeps a long chain on screen when a reader asks for it. Which row and column a
// layer lands in is the whole of what the layout decides; how tall that row is drawn is measured
// from the cards, and so belongs to the canvas.
TEST_CASE("columns wrap layers into rows and leave ranks stacked within them", "[app]") {
    ChainFrame frame;
    declareChainFrame(frame);
    const GraphNodeModel model = modelOf(frame.graph, 7);
    const GraphLayout layout = layoutGraph(model, {.columnsPerRow = 2});

    REQUIRE(layout.items.size() == 6);
    // Four chained passes, the present sink one layer past the last, and the export sink one layer
    // past the first -- which is the layer the second pass already occupies.
    const std::array<uint32_t, 6> layers = {0, 1, 2, 3, 4, 1};
    const std::array<uint32_t, 6> ranks = {0, 0, 0, 0, 0, 1};
    for (uint32_t index = 0; index < layers.size(); ++index) {
        REQUIRE(layout.items[index].layer == layers[index]);
        REQUIRE(layout.items[index].rank == ranks[index]);
        REQUIRE(layout.items[index].row == layers[index] / 2);
        REQUIRE(layout.items[index].column == layers[index] % 2);
    }

    // Row 0 holds layers 0 and 1, and layer 1 stacks two items, so that row is two ranks tall and
    // the row below it starts clear of both.
    REQUIRE(layout.items[0].row == 0);
    REQUIRE(layout.items[1].row == 0);
    REQUIRE(layout.items[5].row == 0);
    REQUIRE(layout.items[5].rank == 1);
    REQUIRE(layout.items[2].row == 1);
    REQUIRE(layout.items[3].row == 1);
    REQUIRE(layout.items[4].row == 2);

    // Unlimited columns leave the same five layers in one row.
    const GraphLayout wide = layoutGraph(model, {});
    for (uint32_t index = 0; index < layers.size(); ++index) {
        REQUIRE(wide.items[index].row == 0);
        REQUIRE(wide.items[index].column == layers[index]);
        REQUIRE(wide.items[index].rank == ranks[index]);
    }
}

//======================================================================================================================
// Culled work is not part of the DAG the compiler proved, so it never occupies a layer of it. It
// sits in its own band below everything that ran, in declaration order.
TEST_CASE("a culled pass sits in the culled band below the last row", "[app]") {
    CulledFrame frame;
    declareCulledFrame(frame);
    const GraphNodeModel model = modelOf(frame.graph, 8);
    const GraphLayout layout = layoutGraph(model, {});

    REQUIRE(layout.groups.empty());
    REQUIRE(layout.items.size() == 5);
    REQUIRE(model.nodes[1].cullReason == CullReason::NoSinkReachesIt);
    REQUIRE(model.nodes[2].cullReason == CullReason::ProducesNothing);

    const GraphLayoutItem& orphan = layout.items[1];
    const GraphLayoutItem& observer = layout.items[2];
    REQUIRE(orphan.culled);
    REQUIRE(observer.culled);

    // One row holds every proved layer, so the band is the row after it and reads left to right.
    REQUIRE(orphan.row == 1);
    REQUIRE(observer.row == 1);
    REQUIRE(orphan.column == 0);
    REQUIRE(observer.column == 1);
    REQUIRE(orphan.rank == 0);
    REQUIRE(observer.rank == 0);

    for (const GraphLayoutItem& item : layout.items) {
        if (item.culled) {
            continue;
        }
        REQUIRE(item.row == 0);
    }

    // A whole culled stage collapses into one box, and that box goes to the band too.
    SplitFrame split;
    declareSplitFrame(split);
    const GraphLayout splitLayout = layoutGraph(modelOf(split.graph, 9), {});
    REQUIRE(splitLayout.items.size() == 5);
    REQUIRE(splitLayout.items[1].kind == GraphLayoutItemKind::Group);
    REQUIRE(splitLayout.items[1].culled);
    REQUIRE(splitLayout.items[1].column == 0);
    REQUIRE(splitLayout.items[3].culled);
    REQUIRE(splitLayout.items[3].column == 1);
    REQUIRE(splitLayout.items[1].row == splitLayout.items[3].row);
    REQUIRE_FALSE(splitLayout.items[0].culled);
}

//======================================================================================================================
// The layout is what makes the canvas readable across runs and machines. It is derived from the
// declarations alone, so two compiles of the same frame give every item the same cell and the
// numbers a driver reported never move a box.
TEST_CASE("item cells are identical for two compiles and ignore timings", "[app]") {
    BloomFrame first;
    declareBloomFrame(first);
    const GraphNodeModel plain = modelOf(first.graph, 1);

    BloomFrame second;
    declareBloomFrame(second);
    const std::array<rhi::PassTiming, 2> timings = {
        rhi::PassTiming{.label = "lmx.pass.scene", .gpuMilliseconds = 2.5},
        rhi::PassTiming{.label = "lmx.pass.bloom.threshold", .gpuMilliseconds = 3.5}};
    const GraphNodeModel measured = modelOf(second.graph, 98765, timings);

    // The timings really did land, so the comparison below is not vacuous.
    REQUIRE(measured.nodes[0].gpuMilliseconds == 2.5);
    REQUIRE_FALSE(plain.nodes[0].gpuMilliseconds.has_value());
    REQUIRE(measured.frameId == 98765);

    const GraphLayoutOptions options{.expandedGroups = {"lmx.pass.bloom"}};
    const GraphLayout plainLayout = layoutGraph(plain, options);
    const GraphLayout measuredLayout = layoutGraph(measured, options);

    REQUIRE(plainLayout.items.size() == measuredLayout.items.size());
    for (uint32_t index = 0; index < plainLayout.items.size(); ++index) {
        REQUIRE(plainLayout.items[index].layer == measuredLayout.items[index].layer);
        REQUIRE(plainLayout.items[index].rank == measuredLayout.items[index].rank);
        REQUIRE(plainLayout.items[index].row == measuredLayout.items[index].row);
        REQUIRE(plainLayout.items[index].column == measuredLayout.items[index].column);
    }

    // Longest path over the expanded chain, one item per layer.
    for (uint32_t index = 0; index < plainLayout.items.size(); ++index) {
        REQUIRE(plainLayout.items[index].layer == index);
        REQUIRE(plainLayout.items[index].rank == 0);
        REQUIRE(plainLayout.items[index].row == 0);
        REQUIRE(plainLayout.items[index].column == index);
    }
}

//======================================================================================================================
// Equal signatures mean the same picture, and the picture is the shape plus the two options that
// place it. A time the driver reported is not part of either.
TEST_CASE("the layout signature follows the options and not the measurements", "[app]") {
    BloomFrame frame;
    declareBloomFrame(frame);
    const std::array<rhi::PassTiming, 1> timings = {
        rhi::PassTiming{.label = "lmx.pass.scene", .gpuMilliseconds = 9.5}};
    const GraphNodeModel plain = modelOf(frame.graph, 1);
    const GraphNodeModel measured = modelOf(frame.graph, 4242, timings);

    const std::string baseline = layoutGraph(plain, {}).signature;
    REQUIRE_FALSE(baseline.empty());
    REQUIRE(baseline.starts_with(plain.shapeSignature));
    REQUIRE(layoutGraph(measured, {}).signature == baseline);

    REQUIRE(layoutGraph(plain, {.columnsPerRow = 2}).signature != baseline);
    REQUIRE(layoutGraph(plain, {.expandedGroups = {"lmx.pass.bloom"}}).signature != baseline);

    // The expansion set is a set: the same keys in another order are the same picture.
    const GraphLayoutOptions ordered{.expandedGroups = {"lmx.pass.a", "lmx.pass.bloom"}};
    const GraphLayoutOptions reversed{.expandedGroups = {"lmx.pass.bloom", "lmx.pass.a"}};
    REQUIRE(layoutGraph(plain, ordered).signature == layoutGraph(plain, reversed).signature);
    REQUIRE(layoutGraph(plain, ordered).signature != baseline);
}

//======================================================================================================================
// The compact pin label is what lets a node be 200 units wide instead of 500.
TEST_CASE("a pin's short label is the resource's last segment and its version", "[app]") {
    REQUIRE(graphPinShortLabel("lmx.render.sceneColorHdr", 1) == "sceneColorHdr v1");
    REQUIRE(graphPinShortLabel("displayColor", 0) == "displayColor v0");
    REQUIRE(graphPinShortLabel("", 3) == " v3");
}

//======================================================================================================================
// A reuse boundary is memory changing hands between two passes. When both of them are inside one
// collapsed stage the handover happens out of sight, and there is nothing honest to draw.
TEST_CASE("an alias link maps to items and disappears inside a collapsed stage", "[app]") {
    AliasFrame frame;
    declareAliasFrame(frame);
    const GraphNodeModel model = modelOf(frame.graph, 10);

    // The frame really does hand memory over, so the comparisons below are not vacuous.
    REQUIRE(model.aliasLinks.size() == 1);
    REQUIRE(model.aliasLinks[0].fromNode == 1);
    REQUIRE(model.aliasLinks[0].toNode == 2);

    const GraphLayout collapsed = layoutGraph(model, {});
    REQUIRE(collapsed.groups.size() == 1);
    REQUIRE(collapsed.groups[0].key == "lmx.pass.post");
    REQUIRE(collapsed.itemOfNode[1] == collapsed.itemOfNode[2]);
    REQUIRE(collapsed.aliasLinks.empty());

    const GraphLayout expanded = layoutGraph(model, {.expandedGroups = {"lmx.pass.post"}});
    REQUIRE(expanded.aliasLinks.size() == 1);
    REQUIRE(expanded.aliasLinks[0].fromItem == 1);
    REQUIRE(expanded.aliasLinks[0].toItem == 2);
    REQUIRE(expanded.aliasLinks[0].sourceLink == 0);
}

//======================================================================================================================
// A frame can declare nothing that survives culling. The layout still has to place it: there is no
// DAG to lay out, so the band it puts everything in starts at the top of the canvas rather than
// below rows that do not exist.
TEST_CASE("a frame with nothing scheduled lays out an empty DAG and a band at the top", "[app]") {
    DeadFrame frame;
    declareDeadFrame(frame);
    const GraphNodeModel model = modelOf(frame.graph, 11);

    // The frame really is entirely dead, so the placement below is not vacuous.
    REQUIRE(model.nodes.size() == 2);
    REQUIRE(model.nodes[0].cullReason == CullReason::NoSinkReachesIt);
    REQUIRE(model.nodes[1].cullReason == CullReason::NoSinkReachesIt);

    const GraphLayout layout = layoutGraph(model, {});
    REQUIRE(layout.groups.empty());
    REQUIRE(layout.items.size() == 2);
    REQUIRE(layout.edges.empty());

    // No row above the band, so the band is row 0 and starts at the top of the canvas.
    for (const GraphLayoutItem& item : layout.items) {
        REQUIRE(item.culled);
        REQUIRE(item.row == 0);
        REQUIRE(item.rank == 0);
    }
    // Ordinals along the band, which is what `layer` and `column` mean for a culled item.
    REQUIRE(layout.items[0].layer == 0);
    REQUIRE(layout.items[1].layer == 1);
    REQUIRE(layout.items[0].column == 0);
    REQUIRE(layout.items[1].column == 1);
}

//======================================================================================================================
// Grouping does not need a proved DAG either: a stage every one of whose passes was culled folds
// into one box, and that box is the whole picture.
TEST_CASE("an all-culled frame folds its stage into one box in the band", "[app]") {
    DeadStageFrame frame;
    declareDeadStageFrame(frame);
    const GraphNodeModel model = modelOf(frame.graph, 12);

    const GraphLayout layout = layoutGraph(model, {});
    REQUIRE(layout.groups.size() == 1);
    REQUIRE(layout.groups[0].key == "lmx.pass.blur#culled");
    REQUIRE(layout.groups[0].culled);
    REQUIRE(layout.groups[0].members == std::vector<uint32_t>{0, 1});

    REQUIRE(layout.items.size() == 1);
    REQUIRE(layout.items[0].kind == GraphLayoutItemKind::Group);
    REQUIRE(layout.items[0].culled);
    REQUIRE(layout.items[0].row == 0);
    REQUIRE(layout.items[0].column == 0);

    // Opening it puts both members in the band, side by side, in the same row.
    const GraphLayout expanded = layoutGraph(model, {.expandedGroups = {"lmx.pass.blur#culled"}});
    REQUIRE(expanded.items.size() == 2);
    REQUIRE(expanded.items[0].row == 0);
    REQUIRE(expanded.items[1].row == 0);
    REQUIRE(expanded.items[0].column == 0);
    REQUIRE(expanded.items[1].column == 1);
}

//======================================================================================================================
// Asking for more columns than there are layers is not a degenerate wrap: it is the unlimited case
// spelled out, and it must place exactly what an unlimited row does.
TEST_CASE("a column count above the layer count leaves every item in row 0", "[app]") {
    ChainFrame frame;
    declareChainFrame(frame);
    const GraphNodeModel model = modelOf(frame.graph, 13);

    const GraphLayout wide = layoutGraph(model, {.columnsPerRow = 16});
    const GraphLayout unlimited = layoutGraph(model, {});

    // Five layers, well under the sixteen columns asked for, so nothing can wrap.
    REQUIRE(wide.items.size() == 6);
    for (const GraphLayoutItem& item : wide.items) {
        REQUIRE(item.layer < 5);
        REQUIRE(item.row == 0);
        REQUIRE(item.column == item.layer);
    }
    for (uint32_t index = 0; index < wide.items.size(); ++index) {
        REQUIRE(wide.items[index].row == unlimited.items[index].row);
        REQUIRE(wide.items[index].column == unlimited.items[index].column);
        REQUIRE(wide.items[index].rank == unlimited.items[index].rank);
    }
}
