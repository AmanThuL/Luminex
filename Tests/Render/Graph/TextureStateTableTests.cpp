#include "Support/RenderGraphTestSupport.h"

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
std::vector<std::string> samplingFrameOps(FakeTexture& shadowTexture, TextureStateTable& states,
                                          ShadowImport import) {
    FakeTexture sceneTexture{64, 64, "scene"};
    RenderGraph graph;
    graph.bindTextureStates(states);
    const GraphTexture shadowMap =
        import == ShadowImport::Table
            ? graph.importPersistentTexture(shadowTexture, rojoRHI::Format::D32Float, "shadowMap")
            : graph.importTexture(shadowTexture, rojoRHI::Format::D32Float, "shadowMap");
    const GraphTexture scene =
        graph.importTexture(sceneTexture, rojoRHI::Format::RGBA16Float, "scene");

    PassDesc draw;
    draw.textureReads.push_back(shadowMap);
    draw.color = ColorAttachment{.handle = scene};
    graph.addPass("lmx.pass.scene", draw, kNoWork);
    graph.exportTexture(nextVersion(scene));
    return describe(lowered(graph));
}

} // namespace

//======================================================================================================================
// No entry is no earlier access, which is what a plain import states, so the two frames lower to
// the same operations.
TEST_CASE("a table import with no entry behaves as a plain import", "[render][graph]") {
    FakeTexture shadowTexture{64, 64, "shadowMap"};
    TextureStateTable states;

    REQUIRE(samplingFrameOps(shadowTexture, states, ShadowImport::Table) ==
            samplingFrameOps(shadowTexture, states, ShadowImport::Plain));
}

//======================================================================================================================
// An earlier frame's write left in the table orders this frame's first read after it, the barrier
// the hand-stated previous use produced.
TEST_CASE("a table import seeds the earlier frame's write", "[render][graph]") {
    FakeTexture shadowTexture{64, 64, "shadowMap"};
    TextureStateTable states;
    states.set(shadowTexture, lmx::render::TextureUse::RenderTarget);

    REQUIRE(
        samplingFrameOps(shadowTexture, states, ShadowImport::Table) ==
        std::vector<std::string>{"scope lmx.frame", "barrier shadowMap RenderTarget->ShaderRead",
                                 "begin lmx.pass.scene", "run lmx.pass.scene", "end", "end scope"});
}

//======================================================================================================================
// The shadow pass writes and the scene pass samples last, so the next frame finds it sampled.
TEST_CASE("execute writes back the last scheduled use", "[render][graph]") {
    FakeTexture shadowTexture{64, 64, "shadowMap"};
    FakeTexture sceneTexture{64, 64, "scene"};
    TextureStateTable states;
    RenderGraph graph;
    graph.bindTextureStates(states);
    const GraphTexture shadowMap =
        graph.importPersistentTexture(shadowTexture, rojoRHI::Format::D32Float, "shadowMap");
    const GraphTexture scene =
        graph.importTexture(sceneTexture, rojoRHI::Format::RGBA16Float, "scene");

    PassDesc shadow;
    shadow.depth = DepthAttachment{.handle = shadowMap, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadow, kNoWork);
    PassDesc draw;
    draw.textureReads.push_back(nextVersion(shadowMap));
    draw.color = ColorAttachment{.handle = scene};
    graph.addPass("lmx.pass.scene", draw, kNoWork);
    graph.exportTexture(nextVersion(scene));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(states.find(shadowTexture) == lmx::render::TextureUse::ShaderRead);
}

//======================================================================================================================
// A downsample step reads mip 0 and writes mip 1 of one texture; the write is the use that leaves
// the texture in a producing state, so it is the one written back.
TEST_CASE("a write wins inside the last pass", "[render][graph]") {
    FakeTexture chainTexture{64, 64, "chain", 2};
    TextureStateTable states;
    RenderGraph graph;
    graph.bindTextureStates(states);
    const GraphTexture chain =
        graph.importPersistentTexture(chainTexture, rojoRHI::Format::RGBA16Float, "chain");

    ComputePassDesc downsample;
    downsample.textureReads.push_back({chain, {.baseMipLevel = 0, .mipLevelCount = 1}});
    downsample.textureWrites.push_back({chain, {.baseMipLevel = 1, .mipLevelCount = 1}});
    graph.addComputePass("lmx.pass.downsample", downsample, kNoWork);
    graph.exportTexture(nextVersion(chain));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(states.find(chainTexture) == lmx::render::TextureUse::StorageWrite);
}

//======================================================================================================================
// The only pass naming the history is culled, so this frame did nothing to it on the GPU and the
// earlier frame's state is still the true one.
TEST_CASE("an unused table import keeps its entry", "[render][graph]") {
    FakeTexture historyTexture{64, 64, "history"};
    FakeTexture unusedTexture{64, 64, "unused"};
    TextureStateTable states;
    states.set(historyTexture, lmx::render::TextureUse::StorageWrite);
    RenderGraph graph;
    graph.bindTextureStates(states);
    const GraphTexture history =
        graph.importPersistentTexture(historyTexture, rojoRHI::Format::RGBA16Float, "history");
    const GraphTexture unused =
        graph.importTexture(unusedTexture, rojoRHI::Format::RGBA16Float, "unused");

    PassDesc resolve;
    resolve.textureReads.push_back(history);
    resolve.color = ColorAttachment{.handle = unused};
    graph.addPass("lmx.pass.resolve", resolve, kNoWork);

    const LoweredFrame frame = lowered(graph);
    REQUIRE(frame.finalStates.empty());

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(states.find(historyTexture) == lmx::render::TextureUse::StorageWrite);
}

//======================================================================================================================
// A caller samples the display after the graph, which the graph cannot see; the stated use replaces
// the derived one, and recording it adds no operation to the frame.
TEST_CASE("setFinalUse overrides the derived state and emits no barrier", "[render][graph]") {
    FakeTexture displayTexture{64, 64, "display"};
    TextureStateTable states;
    const auto declare = [&](RenderGraph& graph, bool stateFinalUse) {
        graph.bindTextureStates(states);
        const GraphTexture display =
            graph.importPersistentTexture(displayTexture, rojoRHI::Format::RGBA8Unorm, "display");
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

    REQUIRE(describe(lowered(stated)) == describe(lowered(plain)));

    RecordingCommandList commands;
    stated.execute(commands, 1);

    REQUIRE(states.find(displayTexture) == lmx::render::TextureUse::ShaderRead);
}

//======================================================================================================================
// Forgetting an entry is how an owner says the texture's earlier accesses no longer bind it: the
// next import is a plain one.
TEST_CASE("forget returns a texture to no earlier access", "[render][graph]") {
    FakeTexture shadowTexture{64, 64, "shadowMap"};
    TextureStateTable states;
    states.set(shadowTexture, lmx::render::TextureUse::RenderTarget);

    states.forget(shadowTexture);

    REQUIRE(states.find(shadowTexture) == std::nullopt);
    REQUIRE(states.size() == 0);
    REQUIRE(samplingFrameOps(shadowTexture, states, ShadowImport::Table) ==
            samplingFrameOps(shadowTexture, states, ShadowImport::Plain));
}
