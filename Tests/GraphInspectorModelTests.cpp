#include <catch2/catch_test_macros.hpp>

#include "App/GraphInspectorModel.h"
#include "GraphTestSupport.h"
#include "Render/RenderGraph.h"
#include "Render/TransientPool.h"

#include <array>
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
    rhi::Format format() const override { return rhi::Format::BGRA8Unorm; }

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

} // namespace

//======================================================================================================================
// The dump reads debug.passes in declaration order and debug.schedule.passes for execution order;
// the model has to carry both without reshuffling either, since an observer comparing the two needs
// them to agree on what "declared" and "scheduled" mean.
TEST_CASE("the model preserves declaration order and the proved schedule", "[app]") {
    FakeTexture shadowMap{1024};
    FakeTexture sceneColor{64};
    FakeTexture sceneDepth{64};
    FakeTexture displayColor{64};
    RenderGraph graph;
    const GraphTexture shadow =
        graph.importTexture(shadowMap, rhi::Format::D32Float, "lmx.render.shadowMap");
    const GraphTexture color =
        graph.importTexture(sceneColor, rhi::Format::RGBA16Float, "lmx.render.sceneColorHdr");
    const GraphTexture depth =
        graph.importTexture(sceneDepth, rhi::Format::D32Float, "lmx.render.sceneDepth");
    const GraphTexture display =
        graph.importTexture(displayColor, rhi::Format::BGRA8Unorm, "lmx.render.displayColor");

    // Declared before its producer, exactly as the shipped frame is: the scheduled order differs
    // from declaration order, which is the case worth pinning.
    PassDesc scene;
    scene.textureReads.push_back(nextVersion(shadow));
    scene.color = ColorAttachment{.handle = color};
    scene.depth = DepthAttachment{.handle = depth, .store = StoreOp::Store};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadowPass, kNoWork);

    PassDesc displayPass;
    displayPass.textureReads.push_back(nextVersion(color));
    displayPass.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);

    graph.presentTexture(nextVersion(display));

    const auto record = graph.compileFrame(12);
    REQUIRE(record.has_value());

    const GraphInspectorModel model = buildGraphInspectorModel(*record, {});

    REQUIRE(model.frameId == 12);
    REQUIRE(model.passes.size() == 3);
    REQUIRE(model.passes[0].label == "lmx.pass.scene");
    REQUIRE(model.passes[1].label == "lmx.pass.shadow");
    REQUIRE(model.passes[2].label == "lmx.pass.display");

    // The schedule is the same index sequence compile() proved, not a sequence the model derived
    // its own way.
    REQUIRE(model.schedule == record->debug.schedule.passes);
    REQUIRE(model.passes[model.schedule[0]].label == "lmx.pass.shadow");

    // A pass's uses resolve the resource's name, not just its index -- what the panel displays.
    REQUIRE(model.passes[0].uses.size() == 3);
    REQUIRE(model.passes[0].uses[0].resourceName == "lmx.render.shadowMap");
    REQUIRE(model.passes[0].uses[0].role == UseRole::Read);
}

//======================================================================================================================
// Culling has two distinct reasons (RenderGraph.h's CullReason), and a dump-equivalent observer has
// to tell them apart rather than flattening both to "not scheduled".
TEST_CASE("culled rows carry their reason, and scheduled ones carry none", "[app]") {
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

    // Writes a target nothing else names: valid work with no consumer and no sink.
    PassDesc orphaned;
    orphaned.color = ColorAttachment{.handle = orphan};
    graph.addPass("lmx.pass.orphan", orphaned, kNoWork);

    // Reads and writes nothing, so no sink could name its output even in principle.
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

    const GraphInspectorModel model = buildGraphInspectorModel(*record, {});

    REQUIRE(model.passes.size() == 4);
    REQUIRE_FALSE(model.passes[0].cullReason.has_value());
    REQUIRE(model.passes[1].label == "lmx.pass.orphan");
    REQUIRE(model.passes[1].cullReason == CullReason::NoSinkReachesIt);
    REQUIRE(model.passes[2].label == "lmx.pass.observer");
    REQUIRE(model.passes[2].cullReason == CullReason::ProducesNothing);
    REQUIRE_FALSE(model.passes[3].cullReason.has_value());

    // Culled passes are declared, not scheduled: they never appear in the execution order.
    REQUIRE(model.schedule.size() == 2);
    for (const uint32_t index : model.schedule) {
        REQUIRE_FALSE(model.passes[index].cullReason.has_value());
    }
}

//======================================================================================================================
// The join contract: a row's time comes only from a timing whose label equals that pass's own, a
// culled pass that never ran must not be given one, and a timing naming nothing here is ignored
// rather than raising an error.
TEST_CASE("timing rows join by label and leave unmeasured passes empty", "[app]") {
    FakeTexture color{64};
    FakeTexture displayed{64};
    FakeTexture unused{64};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture displayColor =
        graph.importTexture(displayed, rhi::Format::BGRA8Unorm, "displayColor");
    const GraphTexture orphan =
        graph.importTexture(unused, rhi::Format::BGRA8Unorm, "unusedTarget");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    // Never reached by a sink, so it is declared but never scheduled or measured.
    PassDesc orphaned;
    orphaned.color = ColorAttachment{.handle = orphan};
    graph.addPass("lmx.pass.orphan", orphaned, kNoWork);

    PassDesc display;
    display.textureReads.push_back(nextVersion(sceneColor));
    display.color = ColorAttachment{.handle = displayColor};
    graph.addPass("lmx.pass.display", display, kNoWork);

    graph.exportTexture(nextVersion(displayColor));

    const auto record = graph.compileFrame(7);
    REQUIRE(record.has_value());

    // "lmx.pass.ghost" names no declared pass at all -- a stale label from a prior frame's shape,
    // which must be silently ignored rather than attached to the nearest row.
    const std::array<rhi::PassTiming, 2> timings = {
        rhi::PassTiming{.label = "lmx.pass.display", .gpuMilliseconds = 0.42},
        rhi::PassTiming{.label = "lmx.pass.ghost", .gpuMilliseconds = 9.99}};

    const GraphInspectorModel model = buildGraphInspectorModel(*record, timings);

    REQUIRE_FALSE(model.passes[0].gpuMilliseconds.has_value()); // scene: scheduled, unmeasured here
    REQUIRE_FALSE(model.passes[1].gpuMilliseconds.has_value()); // orphan: culled, never ran
    REQUIRE(model.passes[2].gpuMilliseconds.has_value());
    REQUIRE(*model.passes[2].gpuMilliseconds == 0.42);

    // Nothing invents an aggregate: the model carries no frame-wide time anywhere.
}

//======================================================================================================================
// Transient rows carry lifetime and placement, an unused transient is reported as unused with no
// placement at all, and the frame's memory totals are copied through untouched -- what the panel's
// high-water mark and alias-savings figures come from.
TEST_CASE("transient rows carry lifetimes and placement, and totals match the frame", "[app]") {
    FakeDevice device;
    TransientPool pool(device);
    FakeTexture midTarget{64};
    FakeTexture displayColor{64};
    RenderGraph graph(pool);
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
    const GraphTexture unused = graph.createTexture({.width = 64,
                                                     .height = 64,
                                                     .format = rhi::Format::RGBA16Float,
                                                     .renderTarget = true,
                                                     .sampled = true},
                                                    "lmx.transient.unused");
    const GraphTexture mid = graph.importTexture(midTarget, rhi::Format::BGRA8Unorm, "lmx.mid");
    const GraphTexture display =
        graph.importTexture(displayColor, rhi::Format::BGRA8Unorm, "lmx.displayColor");

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

    // Declared and never reached: the pass that would have filled it has no sink.
    PassDesc dead;
    dead.color = ColorAttachment{.handle = unused};
    graph.addPass("lmx.pass.dead", dead, kNoWork);

    graph.exportTexture(nextVersion(mid));
    graph.presentTexture(nextVersion(display));

    const auto record = graph.compileFrame(9);
    REQUIRE(record.has_value());

    const GraphInspectorModel model = buildGraphInspectorModel(*record, {});

    REQUIRE(model.poolingEnabled);
    REQUIRE(model.transients.size() == 3);

    const GraphInspectorTransientRow& sceneRow = model.transients[0];
    REQUIRE(sceneRow.resourceName == "lmx.transient.sceneColor");
    REQUIRE(sceneRow.used);
    REQUIRE(sceneRow.offset == 0);
    REQUIRE(sceneRow.size == 16384);
    REQUIRE(sceneRow.alignment == 16384);
    REQUIRE_FALSE(sceneRow.aliases);

    const GraphInspectorTransientRow& bloomRow = model.transients[1];
    REQUIRE(bloomRow.resourceName == "lmx.transient.bloom");
    REQUIRE(bloomRow.used);
    // Placed after sceneColor's lifetime ends, so it reuses the same bytes.
    REQUIRE(bloomRow.offset == sceneRow.offset);
    REQUIRE(bloomRow.aliases);

    const GraphInspectorTransientRow& unusedRow = model.transients[2];
    REQUIRE(unusedRow.resourceName == "lmx.transient.unused");
    REQUIRE_FALSE(unusedRow.used);

    // Two 16 KiB placements sharing one 16 KiB slot: the frame paid once, not twice.
    REQUIRE(model.memory.requested == 32768);
    REQUIRE(model.memory.highWater == 16384);
    REQUIRE(model.memory.aliasSavings == 16384);
}
