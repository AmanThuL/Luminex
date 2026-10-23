#include "Support/RenderGraphTestSupport.h"

#include "Render/Graph/LoweredFrame.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

//======================================================================================================================
// The frame scope is unconditional: an observer timing or labelling the frame sees it even when
// nothing survived culling.
TEST_CASE("an empty frame lowers to the frame scope alone", "[render][graph]") {
    const RenderGraph graph;

    const LoweredFrame frame = lowered(graph);

    REQUIRE(describe(frame) == std::vector<std::string>{"scope lmx.frame", "end scope"});
}

//======================================================================================================================
// The barrier the scene pass needs sits before its render pass, and the render pass carries the
// attachments' clears as declared.
TEST_CASE("a raster pass lowers to its render pass around its body", "[render][graph]") {
    FakeTexture shadowTexture{64, 64, "shadowMap"};
    FakeTexture sceneTexture{64, 64, "scene"};
    FakeTexture depthTexture{64, 64, "sceneDepth"};
    RenderGraph graph;
    const GraphTexture shadowMap =
        graph.importTexture(shadowTexture, rojoRHI::Format::D32Float, "shadowMap");
    const GraphTexture scene =
        graph.importTexture(sceneTexture, rojoRHI::Format::RGBA16Float, "scene");
    const GraphTexture sceneDepth =
        graph.importTexture(depthTexture, rojoRHI::Format::D32Float, "sceneDepth");

    PassDesc shadow;
    shadow.depth = DepthAttachment{.handle = shadowMap, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadow, kNoWork);

    PassDesc draw;
    draw.textureReads.push_back(nextVersion(shadowMap));
    draw.color = ColorAttachment{.handle = scene, .clearColor = {0.05f, 0.07f, 0.10f, 1.0f}};
    draw.depth = DepthAttachment{.handle = sceneDepth};
    graph.addPass("lmx.pass.scene", draw, kNoWork);
    graph.exportTexture(nextVersion(scene));

    const LoweredFrame frame = lowered(graph);

    REQUIRE(
        describe(frame) ==
        std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.shadow", "run lmx.pass.shadow",
                                 "end", "barrier shadowMap RenderTarget->ShaderRead",
                                 "begin lmx.pass.scene", "run lmx.pass.scene", "end", "end scope"});
    const auto& begin = std::get<BeginRenderPassOp>(frame.ops[5]);
    REQUIRE(begin.color.has_value());
    REQUIRE(begin.color->resource == scene.index);
    REQUIRE(begin.color->load == LoadOp::Clear);
    REQUIRE(begin.color->clearColor == std::array<float, 4>{0.05f, 0.07f, 0.10f, 1.0f});
    REQUIRE(begin.depth.has_value());
    REQUIRE(begin.depth->resource == sceneDepth.index);
    REQUIRE(begin.depth->clearDepth == 0.0f);
    REQUIRE(begin.depth->store == StoreOp::Discard);
}

//======================================================================================================================
// A compute or copy pass has no render pass to carry its name, so a scope labelled like the pass
// does, and the barriers it consumes come before that scope opens.
TEST_CASE("compute and copy passes lower to labelled scopes", "[render][graph]") {
    FakeBuffer histogram{1024, "histogram"};
    RenderGraph graph;
    const GraphBuffer bins = graph.importBuffer(histogram, "histogram");

    CopyPassDesc clear;
    clear.bufferDestinations.push_back(bins);
    graph.addCopyPass("lmx.pass.clear", clear, kNoWork);

    ComputePassDesc accumulate;
    accumulate.bufferWrites.push_back(nextVersion(bins));
    graph.addComputePass("lmx.pass.accumulate", accumulate, kNoWork);
    graph.exportBuffer(GraphBuffer{bins.index, 2});

    const LoweredFrame frame = lowered(graph);

    REQUIRE(describe(frame) == std::vector<std::string>{
                                   "scope lmx.frame", "scope lmx.pass.clear", "run lmx.pass.clear",
                                   "end scope", "barrier histogram CopyDestination->StorageWrite",
                                   "scope lmx.pass.accumulate", "run lmx.pass.accumulate",
                                   "end scope", "end scope"});
    REQUIRE(std::get<RunOp>(frame.ops[2]).kind == PassKind::Copy);
    REQUIRE(std::get<RunOp>(frame.ops[6]).kind == PassKind::Compute);
}

//======================================================================================================================
// An external operation owns its own encoding, so the graph opens nothing around it.
TEST_CASE("an external pass lowers to its body alone", "[render][graph]") {
    FakeTexture outputTexture{64, 64, "upscaled"};
    RenderGraph graph;
    const GraphTexture output =
        graph.importTexture(outputTexture, rojoRHI::Format::RGBA16Float, "upscaled");

    ExternalPassDesc upscale;
    upscale.textureWrites.push_back(output);
    graph.addExternalPass("lmx.pass.upscale", upscale, kNoWork);
    graph.exportTexture(nextVersion(output));

    const LoweredFrame frame = lowered(graph);

    REQUIRE(describe(frame) ==
            std::vector<std::string>{"scope lmx.frame", "run lmx.pass.upscale", "end scope"});
    REQUIRE(std::get<RunOp>(frame.ops[1]).kind == PassKind::External);
}

//======================================================================================================================
// Texture and buffer barriers of one transition point stay one ordered list, so neither kind is
// hoisted past the other.
TEST_CASE("one transition point keeps texture and buffer barriers in record order",
          "[render][graph]") {
    FakeTexture imageTexture{64, 64, "image"};
    FakeBuffer countsBuffer{256, "counts"};
    FakeBuffer outputBuffer{256, "output"};
    RenderGraph graph;
    const GraphTexture image =
        graph.importTexture(imageTexture, rojoRHI::Format::RGBA16Float, "image");
    const GraphBuffer counts = graph.importBuffer(countsBuffer, "counts");
    const GraphBuffer output = graph.importBuffer(outputBuffer, "output");

    CopyPassDesc fillCounts;
    fillCounts.bufferDestinations.push_back(counts);
    graph.addCopyPass("lmx.pass.fillCounts", fillCounts, kNoWork);

    ComputePassDesc writeImage;
    writeImage.textureWrites.push_back(image);
    graph.addComputePass("lmx.pass.writeImage", writeImage, kNoWork);

    ComputePassDesc consume;
    consume.bufferReads.push_back(nextVersion(counts));
    consume.shaderTextureReads.push_back(nextVersion(image));
    consume.bufferWrites.push_back(output);
    graph.addComputePass("lmx.pass.consume", consume, kNoWork);
    graph.exportBuffer(nextVersion(output));

    const LoweredFrame frame = lowered(graph);

    std::vector<std::string> expected;
    for (const DebugTransition& transition : frame.record.debug.transitions) {
        if (transition.beforePass != 2) {
            continue;
        }
        const std::string& name = frame.record.debug.resources[transition.resource].name;
        expected.push_back(
            transition.kind == GraphResourceKind::Buffer
                ? std::format("{} {}->{}", name, bufferUseName(transition.bufferFrom),
                              bufferUseName(transition.bufferTo))
                : std::format("{} {}->{}", name, textureUseName(transition.textureFrom),
                              textureUseName(transition.textureTo)));
    }
    REQUIRE(expected.size() == 2);
    for (std::string& line : expected) {
        line.insert(0, "barrier ");
    }

    // The consumer's scope is preceded by exactly one barrier op, which holds both entries.
    const auto consumeScope = std::ranges::find_if(frame.ops, [](const LoweredOp& op) {
        const auto* scope = std::get_if<BeginScopeOp>(&op);
        return scope != nullptr && scope->label == "lmx.pass.consume";
    });
    REQUIRE(consumeScope != frame.ops.end());
    REQUIRE(consumeScope != frame.ops.begin());
    const LoweredOp& transitionPoint = *std::prev(consumeScope);
    REQUIRE(std::holds_alternative<BarriersOp>(transitionPoint));
    LoweredFrame single = frame;
    single.ops = {transitionPoint};
    REQUIRE(describe(single) == expected);
}

//======================================================================================================================
// Lowering is device-free: a transient has no resource until execute() places it, whatever an
// earlier frame placed.
TEST_CASE("lowering leaves transients unplaced", "[render][graph]") {
    DisjointFrame frame;
    frame.declare();

    frame.device.frame = 1;
    frame.pool.beginFrame();
    RecordingCommandList commands;
    frame.graph.execute(commands, 1);
    const LoweredFrame lower = lowered(frame.graph);

    REQUIRE(lower.resources.size() == lower.record.debug.resources.size());
    for (const DebugTransient& transient : lower.record.debug.transients) {
        INFO(lower.record.debug.resources[transient.resource].name);
        REQUIRE(transient.used);
        REQUIRE(lower.resources[transient.resource].texture == nullptr);
        REQUIRE(lower.resources[transient.resource].buffer == nullptr);
    }
    REQUIRE(lower.resources[2].texture == &frame.midTarget);
    REQUIRE(lower.resources[3].texture == &frame.outTarget);

    size_t aliasedBarriers = 0;
    for (const LoweredOp& op : lower.ops) {
        const auto* barriers = std::get_if<BarriersOp>(&op);
        if (barriers == nullptr) {
            continue;
        }
        for (const LoweredBarrier& barrier : barriers->barriers) {
            const auto* texture = std::get_if<LoweredTextureBarrier>(&barrier);
            if (texture != nullptr && texture->alias) {
                ++aliasedBarriers;
            }
        }
    }
    const size_t recordedAliases = static_cast<size_t>(std::ranges::count_if(
        lower.record.debug.transitions,
        [](const DebugTransition& transition) { return transition.aliasedFrom.has_value(); }));
    REQUIRE(recordedAliases > 0);
    REQUIRE(aliasedBarriers == recordedAliases);
}
