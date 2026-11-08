#include "Support/RenderGraphTestSupport.h"
#include "Support/TestGpu.h"

#include "Render/Graph/LoweredFrame.h"
#include "Render/Graph/TextureStateTable.h"

#include <optional>
#include <string>
#include <vector>

namespace {

// How a frame brings the shadow map in: as a plain import, or through the bound state table.
enum class ShadowImport { Plain, Table };

//======================================================================================================================
// A scene pass sampling the shadow map's imported contents, with no pass of this frame writing it.
// Whatever the shadow map holds came from an earlier frame, which is what the table states.
LoweredFrame samplingFrameOps(const engine::TextureRef& shadowTexture, TextureStateTable& states,
                              ShadowImport import) {
    auto sceneTexture = textureFixture(64, 64, "scene");
    RenderGraph graph;
    graph.bindTextureStates(states);
    const GraphTexture shadowMap =
        import == ShadowImport::Table
            ? graph.importPersistentTexture(formatted(shadowTexture, rojoRHI::Format::D32Float),
                                            "shadowMap")
            : graph.importTexture(formatted(shadowTexture, rojoRHI::Format::D32Float), "shadowMap");
    const GraphTexture scene =
        graph.importTexture(formatted(sceneTexture, rojoRHI::Format::RGBA16Float), "scene");

    PassDesc draw;
    draw.textureReads.push_back(shadowMap);
    draw.color = ColorAttachment{.handle = scene};
    graph.addPass("lmx.pass.scene", draw, kNoWork);
    graph.exportTexture(nextVersion(scene));
    return lowered(graph);
}

} // namespace

//======================================================================================================================
// No entry is no earlier access, which is what a plain import states, so the two frames lower to
// the same operations.
TEST_CASE("a table import with no entry behaves as a plain import", "[render][graph]") {
    auto shadowTexture = textureFixture(64, 64, "shadowMap");
    TextureStateTable states;

    const auto table = samplingFrameOps(shadowTexture, states, ShadowImport::Table);
    const auto plain = samplingFrameOps(shadowTexture, states, ShadowImport::Plain);
    REQUIRE(passEvents(table) == passEvents(plain));
    REQUIRE(logicalTransitions(table.record) == logicalTransitions(plain.record));
    const auto tableBarriers = textureBarriers(table);
    const auto plainBarriers = textureBarriers(plain);
    REQUIRE(tableBarriers.size() == plainBarriers.size());
    for (size_t i = 0; i < tableBarriers.size(); ++i) {
        REQUIRE(tableBarriers[i].resource == plainBarriers[i].resource);
        REQUIRE(tableBarriers[i].from == plainBarriers[i].from);
        REQUIRE(tableBarriers[i].to == plainBarriers[i].to);
    }
}

//======================================================================================================================
// An earlier frame's write left in the table orders this frame's first read after it, the barrier
// the hand-stated previous use produced.
TEST_CASE("a table import seeds the earlier frame's write", "[render][graph]") {
    auto shadowTexture = textureFixture(64, 64, "shadowMap");
    TextureStateTable states;
    states.setLogicalUse(shadowTexture.texture, lmx::render::TextureUse::RenderTarget);

    const auto frame = samplingFrameOps(shadowTexture, states, ShadowImport::Table);
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier shadowMap RenderTarget->ShaderRead"});
    REQUIRE(passEvents(frame) == std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.scene",
                                                          "run lmx.pass.scene", "end",
                                                          "end scope"});
    const auto barriers = textureBarriers(frame);
    REQUIRE(barriers.size() == 2);
    REQUIRE(barriers[0].from.layout == rojoRHI::Layout::Undefined);
    REQUIRE(barriers[0].to.layout == rojoRHI::Layout::DepthStencilRead);
    REQUIRE(states.physicalRanges(shadowTexture.texture, {}).empty());
}

//======================================================================================================================
// The shadow pass writes and the scene pass samples last, so the next frame finds it sampled.
TEST_CASE("execute writes back the last scheduled use", "[render][graph][device]") {
    auto gpu = createTestGpu();
    auto shadowTexture =
        createTestTexture(*gpu, rojoRHI::Format::D32Float,
                          rojoRHI::TextureUsage::DepthStencil | rojoRHI::TextureUsage::Sampled);
    auto sceneTexture =
        createTestTexture(*gpu, rojoRHI::Format::RGBA16Float, rojoRHI::TextureUsage::RenderTarget);
    TextureStateTable states;
    RenderGraph graph;
    graph.bindTextureStates(states);
    const GraphTexture shadowMap = graph.importPersistentTexture(shadowTexture, "shadowMap");
    const GraphTexture scene = graph.importTexture(sceneTexture.ref(), "scene");

    PassDesc shadow;
    shadow.depth = DepthAttachment{.handle = shadowMap, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadow, kNoWork);
    PassDesc draw;
    draw.textureReads.push_back(nextVersion(shadowMap));
    draw.color = ColorAttachment{.handle = scene};
    graph.addPass("lmx.pass.scene", draw, kNoWork);
    graph.exportTexture(nextVersion(scene));

    REQUIRE_FALSE(states.logicalUse(shadowTexture.rhi()).has_value());
    const auto proposal = lowered(graph);
    REQUIRE_FALSE(states.logicalUse(shadowTexture.rhi()).has_value());
    auto recording = gpu->beginFrame();
    graph.execute(recording.commands, recording.number);
    gpu->submitAndEndFrame(recording);
    gpu->waitIdle();
    REQUIRE(states.logicalUse(shadowTexture.rhi()) == lmx::render::TextureUse::ShaderRead);
    REQUIRE(states.find(shadowTexture.rhi(), {})->layout == rojoRHI::Layout::DepthStencilRead);
    REQUIRE(shadowTexture.initialLayout() == rojoRHI::Layout::Undefined);
}

//======================================================================================================================
// A downsample step reads mip 0 and writes mip 1 of one texture; the write is the use that leaves
// the texture in a producing state, so it is the one written back.
TEST_CASE("a write wins inside the last pass", "[render][graph][device]") {
    auto gpu = createTestGpu();
    auto chainTexture = createTestTexture(*gpu, rojoRHI::Format::RGBA16Float,
                                          rojoRHI::TextureUsage::Storage, 64, 64, 2);
    TextureStateTable states;
    RenderGraph graph;
    graph.bindTextureStates(states);
    const GraphTexture chain = graph.importPersistentTexture(chainTexture, "chain");

    ComputePassDesc downsample;
    downsample.textureReads.push_back({chain, {.baseMip = 0, .mipCount = 1}});
    downsample.textureWrites.push_back({chain, {.baseMip = 1, .mipCount = 1}});
    graph.addComputePass("lmx.pass.downsample", downsample, kNoWork);
    graph.exportTexture(nextVersion(chain));

    auto recording = gpu->beginFrame();
    graph.execute(recording.commands, recording.number);
    gpu->submitAndEndFrame(recording);
    gpu->waitIdle();
    REQUIRE(states.logicalUse(chainTexture.rhi()) == lmx::render::TextureUse::StorageWrite);
    REQUIRE(states.find(chainTexture.rhi(), {0, 1, 0, 1})->access == rojoRHI::Access::ShaderRead);
    REQUIRE(states.find(chainTexture.rhi(), {1, 1, 0, 1})->access == rojoRHI::Access::ShaderWrite);
}

//======================================================================================================================
// The only pass naming the history is culled, so this frame did nothing to it on the GPU and the
// earlier frame's state is still the true one.
TEST_CASE("an unused table import keeps its entry", "[render][graph]") {
    auto historyTexture = textureFixture(64, 64, "history");
    auto unusedTexture = textureFixture(64, 64, "unused");
    TextureStateTable states;
    states.setLogicalUse(historyTexture.texture, lmx::render::TextureUse::StorageWrite);
    RenderGraph graph;
    graph.bindTextureStates(states);
    const GraphTexture history = graph.importPersistentTexture(
        formatted(historyTexture, rojoRHI::Format::RGBA16Float), "history");
    const GraphTexture unused =
        graph.importTexture(formatted(unusedTexture, rojoRHI::Format::RGBA16Float), "unused");

    PassDesc resolve;
    resolve.textureReads.push_back(history);
    resolve.color = ColorAttachment{.handle = unused};
    graph.addPass("lmx.pass.resolve", resolve, kNoWork);

    const LoweredFrame frame = lowered(graph);
    REQUIRE(frame.finalStates.empty());

    REQUIRE(frame.physicalStates.empty());
    REQUIRE(states.logicalUse(historyTexture.texture) == lmx::render::TextureUse::StorageWrite);
}

//======================================================================================================================
// A caller samples the display after the graph, which the graph cannot see; the stated use replaces
// the derived one, and lowering records a real final physical dependency.
TEST_CASE("setFinalUse overrides the logical state and emits a physical transition",
          "[render][graph]") {
    auto displayTexture = textureFixture(64, 64, "display");
    TextureStateTable states;
    const auto declare = [&](RenderGraph& graph, bool stateFinalUse) {
        graph.bindTextureStates(states);
        const GraphTexture display = graph.importPersistentTexture(
            formatted(displayTexture, rojoRHI::Format::RGBA8Unorm), "display");
        PassDesc draw;
        draw.color = ColorAttachment{.handle = display};
        graph.addPass("lmx.pass.display", draw, kNoWork);
        graph.exportTexture(nextVersion(display));
        if (stateFinalUse) {
            graph.setFinalUse(display, lmx::render::TextureUse::ShaderRead);
        }
    };
    RenderGraph plain;
    declare(plain, false);
    RenderGraph stated;
    declare(stated, true);

    const auto plainFrame = lowered(plain);
    const auto statedFrame = lowered(stated);
    REQUIRE(logicalTransitions(statedFrame.record) == logicalTransitions(plainFrame.record));
    REQUIRE(passEvents(statedFrame) == passEvents(plainFrame));
    const auto plainBarriers = textureBarriers(plainFrame);
    const auto statedBarriers = textureBarriers(statedFrame);
    REQUIRE(statedBarriers.size() == plainBarriers.size() + 1);
    REQUIRE(statedBarriers.back().from.layout == rojoRHI::Layout::ColorAttachment);
    REQUIRE(statedBarriers.back().from.access ==
            (rojoRHI::Access::ColorAttachmentRead | rojoRHI::Access::ColorAttachmentWrite));
    REQUIRE(statedBarriers.back().to.layout == rojoRHI::Layout::ShaderRead);
    REQUIRE(statedBarriers.back().to.access == rojoRHI::Access::ShaderRead);
    REQUIRE(statedFrame.finalStates.front().use == lmx::render::TextureUse::ShaderRead);
    REQUIRE_FALSE(states.logicalUse(displayTexture.texture).has_value());
}

//======================================================================================================================
// Forgetting an entry is how an owner says the texture's earlier accesses no longer bind it: the
// next import is a plain one.
TEST_CASE("forget returns a texture to no earlier access", "[render][graph]") {
    auto shadowTexture = textureFixture(64, 64, "shadowMap");
    TextureStateTable states;
    states.setLogicalUse(shadowTexture.texture, lmx::render::TextureUse::RenderTarget);

    states.forget(shadowTexture.texture);

    REQUIRE(states.logicalUse(shadowTexture.texture) == std::nullopt);
    REQUIRE(states.size() == 0);
    const auto table = samplingFrameOps(shadowTexture, states, ShadowImport::Table);
    const auto plain = samplingFrameOps(shadowTexture, states, ShadowImport::Plain);
    REQUIRE(passEvents(table) == passEvents(plain));
    REQUIRE(logicalTransitions(table.record) == logicalTransitions(plain.record));
    const auto tableBarriers = textureBarriers(table);
    const auto plainBarriers = textureBarriers(plain);
    REQUIRE(tableBarriers.size() == plainBarriers.size());
    for (size_t i = 0; i < tableBarriers.size(); ++i) {
        REQUIRE(tableBarriers[i].resource == plainBarriers[i].resource);
        REQUIRE(tableBarriers[i].from == plainBarriers[i].from);
        REQUIRE(tableBarriers[i].to == plainBarriers[i].to);
    }
}

//======================================================================================================================
TEST_CASE("logical-only entries seed physical cells without replacing known states",
          "[render][graph]") {
    using Stage = rojoRHI::Stage;
    using Access = rojoRHI::Access;
    using Layout = rojoRHI::Layout;
    const rojoRHI::Texture texture{91};
    TextureStateTable states;
    states.setLogicalUse(texture, lmx::render::TextureUse::ShaderRead);
    REQUIRE(states.size() == 1);
    REQUIRE(states.physicalRanges(texture, {}).empty());
    states.seedPhysical(texture, 3, 2, Layout::ShaderRead);
    states.set(texture, {1, 1, 1, 1},
               {Stage::ComputeShader, Access::ShaderWrite, Layout::ShaderWrite});
    states.seedPhysical(texture, 3, 2, Layout::Undefined);
    REQUIRE(states.size() == 1);
    REQUIRE(states.logicalUse(texture) == lmx::render::TextureUse::ShaderRead);
    REQUIRE(states.physicalRanges(texture, {}).size() == 6);
    REQUIRE(states.find(texture, {1, 1, 1, 1})->layout == Layout::ShaderWrite);
    REQUIRE(states.find(texture, {0, 1, 0, 1})->layout == Layout::ShaderRead);
    REQUIRE_FALSE(states.find(texture, {}).has_value());
    states.forget(texture);
    REQUIRE(states.size() == 0);
    REQUIRE_FALSE(states.logicalUse(texture).has_value());
    REQUIRE(states.physicalRanges(texture, {}).empty());
}
