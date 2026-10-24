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

    REQUIRE(passEvents(frame) == std::vector<std::string>{"scope lmx.frame", "end scope"});
    REQUIRE(logicalTransitions(frame.record) == std::vector<std::string>{});
}

//======================================================================================================================
// The barrier the scene pass needs sits before its render pass, and the render pass carries the
// attachments' clears as declared.
TEST_CASE("a raster pass lowers to its render pass around its body", "[render][graph]") {
    auto shadowTexture = textureFixture(64, 64, "shadowMap");
    auto sceneTexture = textureFixture(64, 64, "scene");
    auto depthTexture = textureFixture(64, 64, "sceneDepth");
    RenderGraph graph;
    const GraphTexture shadowMap =
        graph.importTexture(formatted(shadowTexture, rojoRHI::Format::D32Float), "shadowMap");
    const GraphTexture scene =
        graph.importTexture(formatted(sceneTexture, rojoRHI::Format::RGBA16Float), "scene");
    const GraphTexture sceneDepth =
        graph.importTexture(formatted(depthTexture, rojoRHI::Format::D32Float), "sceneDepth");

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

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.shadow",
                                     "run lmx.pass.shadow", "end", "begin lmx.pass.scene",
                                     "run lmx.pass.scene", "end", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier shadowMap RenderTarget->ShaderRead"});
    const auto beginOp = std::ranges::find_if(frame.ops, [](const auto& op) {
        const auto* pass = std::get_if<BeginRenderPassOp>(&op);
        return pass && pass->label == "lmx.pass.scene";
    });
    REQUIRE(beginOp != frame.ops.end());
    const auto& begin = std::get<BeginRenderPassOp>(*beginOp);
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
    auto histogram = bufferFixture(1024, "histogram");
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

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "scope lmx.pass.clear",
                                     "run lmx.pass.clear", "end scope", "scope lmx.pass.accumulate",
                                     "run lmx.pass.accumulate", "end scope", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier histogram CopyDestination->StorageWrite"});
    REQUIRE(std::get<RunOp>(frame.ops[2]).kind == PassKind::Copy);
    REQUIRE(std::get<RunOp>(frame.ops[6]).kind == PassKind::Compute);
}

//======================================================================================================================
// An external operation owns its own encoding, so the graph opens nothing around it.
TEST_CASE("an external pass lowers to its body alone", "[render][graph]") {
    auto outputTexture = textureFixture(64, 64, "upscaled");
    RenderGraph graph;
    const GraphTexture output =
        graph.importTexture(formatted(outputTexture, rojoRHI::Format::RGBA16Float), "upscaled");

    ExternalPassDesc upscale;
    upscale.textureWrites.push_back(output);
    graph.addExternalPass("lmx.pass.upscale", upscale, kNoWork);
    graph.exportTexture(nextVersion(output));

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "run lmx.pass.upscale", "end scope"});
    REQUIRE(logicalTransitions(frame.record) == std::vector<std::string>{});
    const auto run = std::ranges::find_if(
        frame.ops, [](const auto& op) { return std::holds_alternative<RunOp>(op); });
    REQUIRE(run != frame.ops.end());
    REQUIRE(std::get<RunOp>(*run).kind == PassKind::External);
    REQUIRE(textureBarriers(frame).size() == 1);
    REQUIRE(textureBarriers(frame)[0].from.layout == rojoRHI::Layout::Undefined);
}

//======================================================================================================================
// A boundary batches independent physical dependencies while logical rows keep record order.
TEST_CASE("one boundary groups every required texture and buffer dependency", "[render][graph]") {
    auto imageTexture = textureFixture(64, 64, "image");
    auto countsBuffer = bufferFixture(256, "counts");
    auto outputBuffer = bufferFixture(256, "output");
    RenderGraph graph;
    const GraphTexture image =
        graph.importTexture(formatted(imageTexture, rojoRHI::Format::RGBA16Float), "image");
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

    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier image StorageWrite->ShaderRead",
                                     "barrier counts CopyDestination->StorageRead"});

    // The consumer's scope is preceded by exactly one barrier op, which holds both entries.
    const auto consumeScope = std::ranges::find_if(frame.ops, [](const LoweredOp& op) {
        const auto* scope = std::get_if<BeginScopeOp>(&op);
        return scope != nullptr && scope->label == "lmx.pass.consume";
    });
    REQUIRE(consumeScope != frame.ops.end());
    REQUIRE(consumeScope != frame.ops.begin());
    const LoweredOp& transitionPoint = *std::prev(consumeScope);
    REQUIRE(std::holds_alternative<BarriersOp>(transitionPoint));
    const auto& batch = std::get<BarriersOp>(transitionPoint);
    REQUIRE(batch.barriers.size() == 2);
    const auto texture = std::ranges::find_if(batch.barriers, [](const auto& barrier) {
        return std::holds_alternative<LoweredTextureBarrier>(barrier);
    });
    const auto buffer = std::ranges::find_if(batch.barriers, [](const auto& barrier) {
        return std::holds_alternative<LoweredBufferBarrier>(barrier);
    });
    REQUIRE(texture != batch.barriers.end());
    REQUIRE(buffer != batch.barriers.end());
    const auto& imageDependency = std::get<LoweredTextureBarrier>(*texture);
    REQUIRE(imageDependency.resource == image.index);
    REQUIRE(imageDependency.from.stage == rojoRHI::Stage::ComputeShader);
    REQUIRE(imageDependency.from.access == rojoRHI::Access::ShaderWrite);
    REQUIRE(imageDependency.from.layout == rojoRHI::Layout::ShaderWrite);
    REQUIRE(imageDependency.to.stage == rojoRHI::Stage::ComputeShader);
    REQUIRE(imageDependency.to.access == rojoRHI::Access::ShaderRead);
    REQUIRE(imageDependency.to.layout == rojoRHI::Layout::ShaderRead);
    const auto& countsDependency = std::get<LoweredBufferBarrier>(*buffer);
    REQUIRE(countsDependency.resource == counts.index);
    REQUIRE(countsDependency.from.stage == rojoRHI::Stage::Copy);
    REQUIRE(countsDependency.from.access == rojoRHI::Access::CopyWrite);
    REQUIRE(countsDependency.to.stage == rojoRHI::Stage::ComputeShader);
    REQUIRE(countsDependency.to.access == rojoRHI::Access::ShaderRead);
}

//======================================================================================================================
// Lowering is device-free: a transient has no resource until execute() places it, whatever an
// earlier frame placed.
TEST_CASE("lowering leaves transients unplaced", "[render][graph]") {
    DisjointFrame frame;
    frame.declare();

    frame.transients.frame = 1;
    frame.pool.beginFrame();
    const auto first = lowered(frame.graph);
    REQUIRE(frame.pool.reserve(first.transientLayout).has_value());
    for (const auto& placement : first.transientLayout.placements) {
        const auto& desc = std::get<TransientTextureDesc>(placement.desc);
        REQUIRE(frame.pool.placeTexture(desc, placement.offset, placement.label).has_value());
    }
    frame.pool.finishPlacement();
    const LoweredFrame lower = lowered(frame.graph);

    REQUIRE(lower.resources.size() == lower.record.debug.resources.size());
    for (const DebugTransient& transient : lower.record.debug.transients) {
        INFO(lower.record.debug.resources[transient.resource].name);
        REQUIRE(transient.used);
        REQUIRE(lower.resources[transient.resource].texture.texture == rojoRHI::Texture{});
        REQUIRE(lower.resources[transient.resource].buffer.buffer == rojoRHI::Buffer{});
    }
    REQUIRE(lower.resources[2].texture.texture == frame.midTarget.texture);
    REQUIRE(lower.resources[3].texture.texture == frame.outTarget.texture);

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
