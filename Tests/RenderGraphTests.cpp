#include <catch2/catch_test_macros.hpp>

#include "GraphTestSupport.h"
#include "Render/RenderGraph.h"
#include "Render/TransientPool.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::render;

namespace {
// Texture is an interface, and the graph reads three things from it: the extent, which is what the
// attachment rules inspect since the format is declared at import, and the mip and layer counts,
// which a declared subresource range is resolved and bounds-checked against. readback() is never
// reached, so it is left empty rather than faked.
//
// `name` is the test's own label for the texture, so a recorded barrier says which resource it
// transitioned rather than printing a pointer.
struct FakeTexture final : rhi::Texture {
    std::string name;

    //==================================================================================================================
    FakeTexture(uint32_t width, uint32_t height, std::string label = {}, uint32_t mips = 1,
                uint32_t layers = 1)
        : name(std::move(label)), m_width(width), m_height(height), m_mipLevels(mips),
          m_arrayLayers(layers) {}

    //==================================================================================================================
    uint32_t width() const override { return m_width; }

    //==================================================================================================================
    uint32_t height() const override { return m_height; }

    // The graph declares formats itself, so the fake's own format is never consulted.
    //==================================================================================================================
    rhi::Format format() const override { return rhi::Format::BGRA8Unorm; }

    //==================================================================================================================
    uint32_t mipLevels() const override { return m_mipLevels; }

    //==================================================================================================================
    uint32_t arrayLayers() const override { return m_arrayLayers; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_mipLevels = 1;
    uint32_t m_arrayLayers = 1;
};

// The graph never reads a buffer's size -- buffers carry no attachment or subresource rules at all
// -- so this exists to give importBuffer a real object to borrow and to name itself in a barrier.
struct FakeBuffer final : rhi::Buffer {
    std::string name;

    //==================================================================================================================
    explicit FakeBuffer(uint64_t size, std::string label = {})
        : name(std::move(label)), m_size(size) {}

    //==================================================================================================================
    uint64_t size() const override { return m_size; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    uint64_t m_size = 0;
};

// Passes here are declarations and nothing else: this layer stores the body without running it, so
// every pass gets the same empty one.
const ExecuteFn kNoWork = [](const PassResources&) {};

// REQUIRE(result.has_value()) on its own reports "false != true"; the graph's message is the only
// thing that says *why*, so it is pulled out for INFO before the assertion.
template <typename T>

//======================================================================================================================
std::string errorOf(const GraphResult<T>& result) {
    return result ? std::string{} : result.error().message;
}

//======================================================================================================================
std::string useName(rhi::TextureUse use) {
    switch (use) {
    case rhi::TextureUse::RenderTarget:
        return "RenderTarget";
    case rhi::TextureUse::ShaderRead:
        return "ShaderRead";
    case rhi::TextureUse::StorageRead:
        return "StorageRead";
    case rhi::TextureUse::StorageWrite:
        return "StorageWrite";
    case rhi::TextureUse::CopySource:
        return "CopySource";
    case rhi::TextureUse::CopyDestination:
        return "CopyDestination";
    }
    return "unknown";
}

//======================================================================================================================
std::string useName(rhi::BufferUse use) {
    switch (use) {
    case rhi::BufferUse::ShaderRead:
        return "ShaderRead";
    case rhi::BufferUse::StorageRead:
        return "StorageRead";
    case rhi::BufferUse::StorageWrite:
        return "StorageWrite";
    case rhi::BufferUse::CopySource:
        return "CopySource";
    case rhi::BufferUse::CopyDestination:
        return "CopyDestination";
    case rhi::BufferUse::IndirectArgument:
        return "IndirectArgument";
    }
    return "unknown";
}

// What execute() produces is a sequence of RHI calls, so recording that sequence is what makes it
// observable without a device. Only the calls execute() itself makes are recorded; the draw-level
// binds a pass body might make are not this layer's output and are left as no-ops.
struct RecordingCommandList final : rhi::CommandList {
    // The order of passes and of the barriers between them, as one flat log -- both are ordering,
    // and separate lists would not say which came first.
    std::vector<std::string> events;
    // Every begun pass, for the attachment assertions the log cannot carry.
    std::vector<rhi::RenderPassDesc> passes;

    //==================================================================================================================
    void beginRenderPass(const rhi::RenderPassDesc& desc) override {
        passes.push_back(desc);
        events.push_back("begin " + std::string(desc.label));
    }

    //==================================================================================================================
    void endRenderPass() override { events.push_back("end"); }

    // Compute pass boundaries are part of the ordering this log exists to show, so they share the
    // event list with the render ones; the dispatches inside a body are the body's output, not
    // execute()'s, and are logged only so a stray one is visible.

    //==================================================================================================================
    void beginComputePass(std::string_view label) override {
        events.push_back("begin compute " + std::string(label));
    }

    //==================================================================================================================
    void endComputePass() override { events.push_back("end compute"); }

    //==================================================================================================================
    void bindComputePipeline(rhi::ComputePipeline&) override {}

    //==================================================================================================================
    void bindStorageBuffer(uint32_t, rhi::Buffer&, rhi::StorageAccess) override {}

    //==================================================================================================================
    void bindStorageTexture(uint32_t, rhi::Texture&, const rhi::TextureViewDesc&,
                            rhi::StorageAccess) override {}

    //==================================================================================================================
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        events.push_back("dispatch " + std::to_string(x) + "," + std::to_string(y) + "," +
                         std::to_string(z));
    }

    //==================================================================================================================
    void dispatchIndirect(rhi::Buffer&, uint64_t) override {}

    // Copy pass boundaries log for the same reason the compute ones do; the copies a body records
    // are the body's own output and are left as no-ops.

    //==================================================================================================================
    void beginCopyPass(std::string_view label) override {
        events.push_back("begin copy " + std::string(label));
    }

    //==================================================================================================================
    void endCopyPass() override { events.push_back("end copy"); }

    //==================================================================================================================
    void copyBuffer(rhi::Buffer&, uint64_t, rhi::Buffer&, uint64_t, uint64_t) override {}

    //==================================================================================================================
    void copyBufferToTexture(rhi::Buffer&, const rhi::BufferTextureLayout&, rhi::Texture&,
                             const rhi::TextureCopyRegion&) override {}

    //==================================================================================================================
    void copyTextureToBuffer(rhi::Texture&, const rhi::TextureCopyRegion&, rhi::Buffer&,
                             const rhi::BufferTextureLayout&) override {}

    //==================================================================================================================
    void copyTexture(rhi::Texture&, const rhi::TextureCopyRegion&, rhi::Texture&,
                     const rhi::TextureCopyRegion&) override {}

    //==================================================================================================================
    void fillBuffer(rhi::Buffer&, uint64_t, uint64_t, uint8_t) override {}

    //==================================================================================================================
    void bufferBarrier(rhi::Buffer& buffer, const rhi::BufferRange&, rhi::BufferUse from,
                       rhi::BufferUse to) override {
        events.push_back("barrier " + static_cast<FakeBuffer&>(buffer).name + " " + useName(from) +
                         "->" + useName(to));
    }

    //==================================================================================================================
    void textureBarrier(rhi::Texture& texture, const rhi::TextureSubresourceRange& range,
                        rhi::TextureUse from, rhi::TextureUse to) override {
        // A whole-resource range is what a pass with no subresource detail declares and is the
        // common case, so it is left out of the log; a narrowed one is spelled out, because a
        // barrier covering the wrong subresources is exactly what these cases are looking for.
        const bool wholeResource =
            range.baseMipLevel == 0 && range.mipLevelCount == rhi::kAllMipLevels &&
            range.baseArrayLayer == 0 && range.arrayLayerCount == rhi::kAllArrayLayers;
        events.push_back("barrier " + static_cast<FakeTexture&>(texture).name +
                         (wholeResource ? "" : " " + describeRange(range)) + " " + useName(from) +
                         "->" + useName(to));
    }

    //==================================================================================================================
    void bindPipeline(rhi::GraphicsPipeline&) override {}

    //==================================================================================================================
    void bindBuffer(uint32_t, rhi::Buffer&) override {}

    //==================================================================================================================
    void bindTexture(uint32_t, rhi::Texture&) override {}

    //==================================================================================================================
    void bindSampler(uint32_t, rhi::Sampler&) override {}

    //==================================================================================================================
    void setUniforms(uint32_t, const void*, uint64_t) override {}

    //==================================================================================================================
    void draw(uint32_t, uint32_t) override {}

    //==================================================================================================================
    void drawIndexed(rhi::Buffer&, uint32_t, uint32_t) override {}

    //==================================================================================================================
    void drawIndirect(rhi::Buffer&, uint64_t) override {}

    //==================================================================================================================
    void drawIndexedIndirect(rhi::Buffer&, rhi::Buffer&, uint64_t) override {}
};
} // namespace

//======================================================================================================================
TEST_CASE("an imported texture enters the graph at version 0", "[render][graph]") {
    FakeTexture color{64, 64};
    RenderGraph graph;

    const GraphTexture handle = graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");

    REQUIRE(handle.version == 0);
    REQUIRE(nextVersion(handle) == GraphTexture{handle.index, 1});
}

//======================================================================================================================
TEST_CASE("a graph with no passes compiles to an empty schedule", "[render][graph]") {
    RenderGraph graph;

    const auto schedule = graph.compile();
    REQUIRE(schedule.has_value());
    REQUIRE(schedule->passes.empty());
}

//======================================================================================================================
// The declaration order is deliberately the reverse of the execution order: the scene pass is
// declared first and names a shadow map version the later-declared shadow pass produces. Nothing
// but the versions says so, which is the point.
TEST_CASE("a producer is scheduled before its consumer whatever the declaration order",
          "[render][graph]") {
    FakeTexture shadowMap{1024, 1024};
    FakeTexture color{64, 64};
    RenderGraph graph;
    const GraphTexture shadow = graph.importTexture(shadowMap, rhi::Format::D32Float, "shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(shadow));
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = shadow};
    graph.addPass("lmx.pass.shadow", shadowPass, kNoWork);

    graph.exportTexture(nextVersion(sceneColor));

    const auto schedule = graph.compile();
    INFO(errorOf(schedule));
    REQUIRE(schedule.has_value());
    REQUIRE(schedule->passes == std::vector<uint32_t>{1, 0});
}

//======================================================================================================================
// Two passes that share nothing are ordered by the only tie-break the graph has.
TEST_CASE("independent passes keep their declaration order", "[render][graph]") {
    FakeTexture first{64, 64};
    FakeTexture second{64, 64};
    RenderGraph graph;
    const GraphTexture a = graph.importTexture(first, rhi::Format::BGRA8Unorm, "first");
    const GraphTexture b = graph.importTexture(second, rhi::Format::BGRA8Unorm, "second");

    PassDesc writeA;
    writeA.color = ColorAttachment{.handle = a};
    graph.addPass("lmx.pass.a", writeA, kNoWork);

    PassDesc writeB;
    writeB.color = ColorAttachment{.handle = b};
    graph.addPass("lmx.pass.b", writeB, kNoWork);

    graph.exportTexture(nextVersion(a));
    graph.exportTexture(nextVersion(b));

    const auto schedule = graph.compile();
    INFO(errorOf(schedule));
    REQUIRE(schedule.has_value());
    REQUIRE(schedule->passes == std::vector<uint32_t>{0, 1});
}

//======================================================================================================================
// Buffers carry the same version chain as textures, so a buffer write orders its reader too.
TEST_CASE("a buffer write orders the pass that reads its result", "[render][graph]") {
    FakeBuffer storage{256};
    FakeBuffer result{256};
    RenderGraph graph;
    const GraphBuffer buffer = graph.importBuffer(storage, "instances");
    const GraphBuffer resolved = graph.importBuffer(result, "resolved");

    PassDesc consumer;
    consumer.bufferReads.push_back(nextVersion(buffer));
    consumer.bufferWrites.push_back(resolved);
    graph.addPass("lmx.pass.consumer", consumer, kNoWork);

    PassDesc producer;
    producer.bufferWrites.push_back(buffer);
    graph.addPass("lmx.pass.producer", producer, kNoWork);

    graph.exportBuffer(nextVersion(resolved));

    const auto schedule = graph.compile();
    INFO(errorOf(schedule));
    REQUIRE(schedule.has_value());
    REQUIRE(schedule->passes == std::vector<uint32_t>{1, 0});
}

//======================================================================================================================
// Read before write: version 1 exists only if some pass writes version 0, and none does.
TEST_CASE("reading a version no pass writes is rejected", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024};
    RenderGraph graph;
    const GraphTexture shadow = graph.importTexture(shadowMap, rhi::Format::D32Float, "shadowMap");

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(shadow));
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.scene"));
    REQUIRE(schedule.error().message.contains("read"));
    REQUIRE(schedule.error().message.contains("shadowMap"));
    REQUIRE(schedule.error().message.contains("no pass writes"));
}

//======================================================================================================================
// Every version here is produced, so this is not a missing-producer failure: each pass consumes
// what the other one makes, and no serial order satisfies both.
TEST_CASE("mutually dependent passes are rejected as a cycle", "[render][graph]") {
    FakeTexture first{64, 64};
    FakeTexture second{64, 64};
    RenderGraph graph;
    const GraphTexture a = graph.importTexture(first, rhi::Format::BGRA8Unorm, "first");
    const GraphTexture b = graph.importTexture(second, rhi::Format::BGRA8Unorm, "second");

    PassDesc passA;
    passA.color = ColorAttachment{.handle = a};
    passA.textureReads.push_back(nextVersion(b));
    graph.addPass("lmx.pass.a", passA, kNoWork);

    PassDesc passB;
    passB.color = ColorAttachment{.handle = b};
    passB.textureReads.push_back(nextVersion(a));
    graph.addPass("lmx.pass.b", passB, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("cycle"));
    REQUIRE(schedule.error().message.contains("lmx.pass.a"));
    REQUIRE(schedule.error().message.contains("lmx.pass.b"));
}

//======================================================================================================================
TEST_CASE("attachments of different extents are rejected", "[render][graph]") {
    FakeTexture color{64, 64};
    FakeTexture depth{32, 32};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture sceneDepth = graph.importTexture(depth, rhi::Format::D32Float, "sceneDepth");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.depth = DepthAttachment{.handle = sceneDepth};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.scene"));
    REQUIRE(schedule.error().message.contains("extent"));
    // "they differ" is not actionable; the two extents are.
    REQUIRE(schedule.error().message.contains("64x64"));
    REQUIRE(schedule.error().message.contains("32x32"));
}

//======================================================================================================================
// Height alone, because a square-against-square comparison would pass a check that only ever
// looked at width.
TEST_CASE("attachments differing only in height are rejected", "[render][graph]") {
    FakeTexture color{64, 64};
    FakeTexture depth{64, 32};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture sceneDepth = graph.importTexture(depth, rhi::Format::D32Float, "sceneDepth");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    scene.depth = DepthAttachment{.handle = sceneDepth};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("extent"));
}

//======================================================================================================================
TEST_CASE("a depth format in the color attachment is rejected", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024};
    RenderGraph graph;
    const GraphTexture shadow = graph.importTexture(shadowMap, rhi::Format::D32Float, "shadowMap");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = shadow};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.scene"));
    REQUIRE(schedule.error().message.contains("color attachment"));
    REQUIRE(schedule.error().message.contains("D32Float"));
}

//======================================================================================================================
TEST_CASE("a color format in the depth attachment is rejected", "[render][graph]") {
    FakeTexture color{64, 64};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.depth = DepthAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.scene"));
    REQUIRE(schedule.error().message.contains("depth attachment"));
    REQUIRE(schedule.error().message.contains("BGRA8Unorm"));
}

//======================================================================================================================
// Both passes name version 0, so both would produce version 1 and neither could say which one a
// reader of version 1 gets.
TEST_CASE("two passes writing one version are rejected", "[render][graph]") {
    FakeTexture color{64, 64};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");

    PassDesc first;
    first.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.first", first, kNoWork);

    PassDesc second;
    second.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.second", second, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.first"));
    REQUIRE(schedule.error().message.contains("lmx.pass.second"));
    REQUIRE(schedule.error().message.contains("both write"));
    REQUIRE(schedule.error().message.contains("sceneColor"));
}

//======================================================================================================================
// Exporting roots a result the graph produced. The imported version 0 is not one -- nothing in the
// graph put it there -- and neither is a version past the end of the write chain.
TEST_CASE("exporting a version no pass wrote is rejected", "[render][graph]") {
    FakeTexture color{64, 64};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");

    graph.exportTexture(sceneColor);

    const auto imported = graph.compile();
    REQUIRE_FALSE(imported.has_value());
    REQUIRE(imported.error().message.contains("sceneColor"));
    REQUIRE(imported.error().message.contains("not written by any pass"));

    RenderGraph beyond;
    const GraphTexture beyondColor =
        beyond.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");
    beyond.exportTexture(nextVersion(beyondColor));
    REQUIRE_FALSE(beyond.compile().has_value());
}

//======================================================================================================================
TEST_CASE("exporting a version a pass wrote is accepted", "[render][graph]") {
    FakeTexture color{64, 64};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    REQUIRE(graph.compile().has_value());
}

//======================================================================================================================
// Attachments count as declarations too: the pass writes that version, so it may resolve it.
TEST_CASE("PassResources resolves every texture the pass declared", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024};
    FakeTexture color{64, 64};
    RenderGraph graph;
    const GraphTexture shadow = graph.importTexture(shadowMap, rhi::Format::D32Float, "shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.textureReads.push_back(shadow);
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const PassResources resources = graph.passResources(0);
    const auto read = resources.texture(shadow);
    REQUIRE(read.has_value());
    REQUIRE(*read == &shadowMap);

    const auto attachment = resources.texture(sceneColor);
    REQUIRE(attachment.has_value());
    REQUIRE(*attachment == &color);
}

//======================================================================================================================
// The illegal read: the pass declared version 0 of the shadow map, so version 1 of it -- and the
// scene colour it never named at all -- resolve to nothing. Handing either one over would give the
// pass a resource the schedule never ordered it against.
TEST_CASE("PassResources refuses a texture the pass did not declare", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024};
    FakeTexture color{64, 64};
    RenderGraph graph;
    const GraphTexture shadow = graph.importTexture(shadowMap, rhi::Format::D32Float, "shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.textureReads.push_back(shadow);
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const PassResources resources = graph.passResources(0);

    const auto undeclared = resources.texture(sceneColor);
    REQUIRE_FALSE(undeclared.has_value());
    REQUIRE(undeclared.error().message.contains("lmx.pass.scene"));
    REQUIRE(undeclared.error().message.contains("sceneColor"));
    REQUIRE(undeclared.error().message.contains("did not declare"));

    const auto wrongVersion = resources.texture(nextVersion(shadow));
    REQUIRE_FALSE(wrongVersion.has_value());
    REQUIRE(wrongVersion.error().message.contains("shadowMap"));
}

//======================================================================================================================
TEST_CASE("PassResources resolves a declared buffer and refuses an undeclared one",
          "[render][graph]") {
    FakeBuffer instances{256};
    FakeBuffer unused{128};
    RenderGraph graph;
    const GraphBuffer declared = graph.importBuffer(instances, "instances");
    const GraphBuffer undeclared = graph.importBuffer(unused, "unused");

    PassDesc scene;
    scene.bufferReads.push_back(declared);
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const PassResources resources = graph.passResources(0);

    const auto resolved = resources.buffer(declared);
    REQUIRE(resolved.has_value());
    REQUIRE(*resolved == &instances);

    const auto refused = resources.buffer(undeclared);
    REQUIRE_FALSE(refused.has_value());
    REQUIRE(refused.error().message.contains("unused"));
    REQUIRE(refused.error().message.contains("did not declare"));
}

//======================================================================================================================
// The whole encoding contract in one frame's shape: the declared order is the reverse of the
// scheduled one, each pass becomes one labelled render pass carrying its own attachments, the body
// runs inside it, and the shadow map's render-target-to-sampled transition lands between the two.
TEST_CASE("execute encodes the schedule as labelled render passes", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024, "shadowMap"};
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture depth{64, 64, "sceneDepth"};
    RenderGraph graph;
    const GraphTexture shadow = graph.importTexture(shadowMap, rhi::Format::D32Float, "shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture sceneDepth = graph.importTexture(depth, rhi::Format::D32Float, "sceneDepth");

    RecordingCommandList commands;
    rhi::Texture* resolvedShadow = nullptr;

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(shadow));
    scene.color = ColorAttachment{.handle = sceneColor, .clearColor = {0.05f, 0.07f, 0.10f, 1.0f}};
    scene.depth = DepthAttachment{.handle = sceneDepth};
    graph.addPass("lmx.pass.scene", scene, [&](const PassResources& resources) {
        commands.events.push_back("body scene");
        const auto texture = resources.texture(nextVersion(shadow));
        REQUIRE(texture.has_value());
        resolvedShadow = *texture;
    });

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadowPass,
                  [&commands](const PassResources&) { commands.events.push_back("body shadow"); });

    graph.exportTexture(nextVersion(sceneColor));

    graph.execute(commands, 1);

    REQUIRE(commands.events ==
            std::vector<std::string>{"begin lmx.pass.shadow", "body shadow", "end",
                                     "barrier shadowMap RenderTarget->ShaderRead",
                                     "begin lmx.pass.scene", "body scene", "end"});
    REQUIRE(resolvedShadow == &shadowMap);

    REQUIRE(commands.passes.size() == 2);
    REQUIRE(commands.passes[0].colorTarget == nullptr);
    REQUIRE(commands.passes[0].depthTarget == &shadowMap);
    REQUIRE(commands.passes[0].storeDepth);
    // Forwarded from DepthAttachment's default, which is the reversed convention's far plane --
    // not rhi::RenderPassDesc's own 1.0, which this pass would otherwise have inherited.
    REQUIRE(commands.passes[0].clearDepth == 0.0f);

    REQUIRE(commands.passes[1].colorTarget == &color);
    REQUIRE(commands.passes[1].depthTarget == &depth);
    REQUIRE(commands.passes[1].clear);
    REQUIRE_FALSE(commands.passes[1].storeDepth);
    REQUIRE(commands.passes[1].clearColor[0] == 0.05f);
    REQUIRE(commands.passes[1].clearColor[1] == 0.07f);
    REQUIRE(commands.passes[1].clearColor[2] == 0.10f);
    REQUIRE(commands.passes[1].clearColor[3] == 1.0f);
}

//======================================================================================================================
// The barrier is a state transition of the texture, not a property of one read: the second reader
// finds the shadow map already readable, so a second barrier would be pure cost.
TEST_CASE("execute transitions a sampled render target once", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024, "shadowMap"};
    FakeTexture firstColor{64, 64, "first"};
    FakeTexture secondColor{64, 64, "second"};
    RenderGraph graph;
    const GraphTexture shadow = graph.importTexture(shadowMap, rhi::Format::D32Float, "shadowMap");
    const GraphTexture first = graph.importTexture(firstColor, rhi::Format::BGRA8Unorm, "first");
    const GraphTexture second = graph.importTexture(secondColor, rhi::Format::BGRA8Unorm, "second");

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadowPass, kNoWork);

    const auto reader = [&](GraphTexture target, const char* label) {
        PassDesc desc;
        desc.textureReads.push_back(nextVersion(shadow));
        desc.color = ColorAttachment{.handle = target};
        graph.addPass(label, desc, kNoWork);
    };
    reader(first, "lmx.pass.first");
    reader(second, "lmx.pass.second");
    graph.exportTexture(nextVersion(first));
    graph.exportTexture(nextVersion(second));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(commands.events == std::vector<std::string>{
                                   "begin lmx.pass.shadow", "end",
                                   "barrier shadowMap RenderTarget->ShaderRead",
                                   "begin lmx.pass.first", "end", "begin lmx.pass.second", "end"});
}

//======================================================================================================================
// Exporting roots a result for the caller to read once the queue drains, which is not a shader read
// by another pass -- so it is not what the barrier is for.
TEST_CASE("execute emits no barrier for a texture only exported", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(commands.events == std::vector<std::string>{"begin lmx.pass.scene", "end"});
}

//======================================================================================================================
// A pass that renders into a texture it earlier sampled needs the transition again: one barrier per
// transition is not one barrier per texture. It also needs a barrier *before* that pass, ordering
// its write after the earlier read: "pingPong" was left in ShaderRead by "sample", so "rewrite"
// owes a write-after-read barrier before it overwrites the texture, and "other" symmetrically owes
// one before "resample" for the same reason.
TEST_CASE("execute transitions a render target again after it is rewritten", "[render][graph]") {
    FakeTexture pingPong{64, 64, "pingPong"};
    FakeTexture other{64, 64, "other"};
    RenderGraph graph;
    const GraphTexture target = graph.importTexture(pingPong, rhi::Format::BGRA8Unorm, "pingPong");
    const GraphTexture scratch = graph.importTexture(other, rhi::Format::BGRA8Unorm, "other");

    PassDesc write;
    write.color = ColorAttachment{.handle = target};
    graph.addPass("lmx.pass.write", write, kNoWork);

    PassDesc sample;
    sample.textureReads.push_back(nextVersion(target));
    sample.color = ColorAttachment{.handle = scratch};
    graph.addPass("lmx.pass.sample", sample, kNoWork);

    PassDesc rewrite;
    rewrite.textureReads.push_back(nextVersion(scratch));
    rewrite.color = ColorAttachment{.handle = nextVersion(target)};
    graph.addPass("lmx.pass.rewrite", rewrite, kNoWork);

    PassDesc resample;
    resample.textureReads.push_back(GraphTexture{target.index, 2});
    resample.color = ColorAttachment{.handle = nextVersion(scratch)};
    graph.addPass("lmx.pass.resample", resample, kNoWork);

    graph.exportTexture(GraphTexture{scratch.index, 2});

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(commands.events ==
            std::vector<std::string>{
                "begin lmx.pass.write", "end", "barrier pingPong RenderTarget->ShaderRead",
                "begin lmx.pass.sample", "end", "barrier other RenderTarget->ShaderRead",
                "barrier pingPong ShaderRead->RenderTarget", "begin lmx.pass.rewrite", "end",
                "barrier pingPong RenderTarget->ShaderRead",
                "barrier other ShaderRead->RenderTarget", "begin lmx.pass.resample", "end"});
}

//======================================================================================================================
// The exposure-buffer shape finding 1 named: one pass reads a buffer version and a later pass
// writes the next version of it, with no other resource ordering them transitively. Without a
// write-after-read barrier the write could retire before the read that depends on the prior
// contents does; deriveTransitions now owes one before the writing pass.
TEST_CASE("execute barriers a buffer write after an earlier read", "[render][graph]") {
    FakeBuffer exposureBuffer{16, "exposure"};
    FakeBuffer sceneColorBuffer{16, "sceneColor"};
    RenderGraph graph;
    const GraphBuffer exposure = graph.importBuffer(exposureBuffer, "exposure");
    const GraphBuffer sceneColor = graph.importBuffer(sceneColorBuffer, "sceneColor");

    // The scene pass reads the exposure buffer and, like the real renderer, also writes scene
    // colour -- a pure read with no write of its own would be dead work no sink reaches.
    ComputePassDesc scene;
    scene.bufferReads.push_back(exposure);
    scene.bufferWrites.push_back(sceneColor);
    graph.addComputePass("lmx.pass.scene", scene, kNoWork);

    ComputePassDesc resolve;
    resolve.bufferWrites.push_back(exposure);
    graph.addComputePass("lmx.pass.resolve", resolve, kNoWork);

    graph.exportBuffer(nextVersion(sceneColor));
    graph.exportBuffer(nextVersion(exposure));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(commands.events ==
            std::vector<std::string>{"begin compute lmx.pass.scene", "end compute",
                                     "barrier exposure StorageRead->StorageWrite",
                                     "begin compute lmx.pass.resolve", "end compute"});
}

//======================================================================================================================
// Ledger L60, on the buffer path: the exposure buffer is read by the scene pass through a sampled
// binding and by the histogram dispatch through a storage one, and only then overwritten. A single
// remembered reader would name whichever read last, leaving the other unordered against the write;
// the write owes a barrier from *each* distinct reading use, in the order the reads happened.
TEST_CASE("a buffer write is barriered after every kind of earlier read", "[render][graph]") {
    FakeBuffer exposureBuffer{16, "exposure"};
    FakeTexture sceneColorTarget{64, 64, "sceneColor"};
    FakeBuffer histogramBuffer{1024, "histogram"};
    RenderGraph graph;
    const GraphBuffer exposure = graph.importBuffer(exposureBuffer, "exposure");
    const GraphTexture sceneColor =
        graph.importTexture(sceneColorTarget, rhi::Format::RGBA16Float, "sceneColor");
    const GraphBuffer histogram = graph.importBuffer(histogramBuffer, "histogram");

    // A raster read of the buffer: the shipped scene pass's exposure override.
    PassDesc scene;
    scene.bufferReads.push_back(exposure);
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    // A compute read of the same version, through a storage binding.
    ComputePassDesc histogramPass;
    histogramPass.bufferReads.push_back(exposure);
    histogramPass.bufferWrites.push_back(histogram);
    graph.addComputePass("lmx.pass.histogram", histogramPass, kNoWork);

    ComputePassDesc resolve;
    resolve.bufferWrites.push_back(exposure);
    graph.addComputePass("lmx.pass.resolve", resolve, kNoWork);

    graph.exportTexture(nextVersion(sceneColor));
    graph.exportBuffer(nextVersion(histogram));
    graph.exportBuffer(nextVersion(exposure));

    RecordingCommandList commands;
    const CompiledFrameRecord record = graph.execute(commands, 1);

    // Two write-after-read transitions before the resolve, one per reading use.
    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].bufferFrom == rhi::BufferUse::ShaderRead);
    REQUIRE(record.debug.transitions[1].bufferFrom == rhi::BufferUse::StorageRead);
    REQUIRE(record.debug.transitions[0].beforePass == 2);
    REQUIRE(record.debug.transitions[1].beforePass == 2);

    REQUIRE(commands.events ==
            std::vector<std::string>{"begin lmx.pass.scene", "end",
                                     "begin compute lmx.pass.histogram", "end compute",
                                     "barrier exposure ShaderRead->StorageWrite",
                                     "barrier exposure "
                                     "StorageRead->StorageWrite",
                                     "begin compute lmx.pass.resolve", "end compute"});
}

//======================================================================================================================
// The same rule on the texture path, where the pending reads carry subresource ranges: each range a
// write overlaps keeps the use that read it, so a write over both of them names both.
TEST_CASE("a texture write is barriered after every kind of earlier read", "[render][graph]") {
    FakeTexture sceneColorTarget{64, 64, "sceneColor"};
    FakeBuffer histogramBuffer{1024, "histogram"};
    FakeTexture displayTarget{64, 64, "display"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(sceneColorTarget, rhi::Format::RGBA16Float, "sceneColor");
    const GraphBuffer histogram = graph.importBuffer(histogramBuffer, "histogram");
    const GraphTexture display =
        graph.importTexture(displayTarget, rhi::Format::BGRA8Unorm, "display");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const GraphTexture sceneRead = nextVersion(sceneColor);

    ComputePassDesc histogramPass;
    histogramPass.textureReads.push_back(sceneRead);
    histogramPass.bufferWrites.push_back(histogram);
    graph.addComputePass("lmx.pass.histogram", histogramPass, kNoWork);

    PassDesc displayPass;
    displayPass.textureReads.push_back(sceneRead);
    displayPass.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);

    // Overwrites what both of them read.
    ComputePassDesc overwrite;
    overwrite.textureWrites.push_back(sceneRead);
    graph.addComputePass("lmx.pass.overwrite", overwrite, kNoWork);

    graph.exportBuffer(nextVersion(histogram));
    graph.exportTexture(nextVersion(display));
    graph.exportTexture(nextVersion(sceneRead));

    RecordingCommandList commands;
    const CompiledFrameRecord record = graph.execute(commands, 1);

    // Two read-after-write transitions (one per reading stage class) and then two write-after-read
    // ones before the overwrite, from each of those same uses.
    REQUIRE(record.debug.transitions.size() == 4);
    REQUIRE(record.debug.transitions[2].beforePass == 3);
    REQUIRE(record.debug.transitions[2].textureFrom == rhi::TextureUse::StorageRead);
    REQUIRE(record.debug.transitions[3].beforePass == 3);
    REQUIRE(record.debug.transitions[3].textureFrom == rhi::TextureUse::ShaderRead);

    REQUIRE(commands.events ==
            std::vector<std::string>{
                "begin lmx.pass.scene", "end", "barrier sceneColor RenderTarget->StorageRead",
                "begin compute lmx.pass.histogram", "end compute",
                "barrier sceneColor RenderTarget->ShaderRead", "begin lmx.pass.display", "end",
                "barrier sceneColor StorageRead->StorageWrite",
                "barrier sceneColor ShaderRead->StorageWrite", "begin compute lmx.pass.overwrite",
                "end compute"});
}

//======================================================================================================================
// A pass reading and writing one buffer in the same dispatch (the histogram accumulate shape) must
// not be barriered against itself: the read this pass records is not visible to its own write
// check, only to a later pass's.
TEST_CASE("a pass reading and writing one buffer owes itself no barrier", "[render][graph]") {
    FakeBuffer histogram{1024, "histogram"};
    RenderGraph graph;
    const GraphBuffer bins = graph.importBuffer(histogram, "histogram");

    ComputePassDesc accumulate;
    accumulate.bufferReads.push_back(bins);
    accumulate.bufferWrites.push_back(bins);
    graph.addComputePass("lmx.pass.accumulate", accumulate, kNoWork);
    graph.exportBuffer(nextVersion(bins));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(commands.events ==
            std::vector<std::string>{"begin compute lmx.pass.accumulate", "end compute"});
}

//======================================================================================================================
// A write-after-read barrier must survive an unrelated write to a disjoint range sitting between
// the read and the write that actually needs ordering: A reads mip 0, B writes the disjoint mip 3
// (no barrier -- B touches nothing A read), then C writes mip 0 and must still be ordered after A's
// read. Discharging a resource's *entire* pending-read record on any write to it (rather than only
// the ranges that write overlaps) would let B's write erase A's still-pending mip 0 read, leaving C
// with nothing to barrier against.
TEST_CASE("a write-after-read barrier survives an unrelated write to a disjoint range",
          "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    FakeTexture other{64, 64, "other"};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");
    const GraphTexture sink = graph.importTexture(other, rhi::Format::BGRA8Unorm, "other");

    static constexpr rhi::TextureSubresourceRange kMip0{.baseMipLevel = 0, .mipLevelCount = 1};
    static constexpr rhi::TextureSubresourceRange kMip3{.baseMipLevel = 3, .mipLevelCount = 1};

    // A: reads mip 0, and writes a separate texture so it is not dead work no sink reaches.
    PassDesc readA;
    readA.textureReads.push_back(TextureUseDesc(bloom, kMip0));
    readA.color = ColorAttachment{.handle = sink};
    graph.addPass("lmx.pass.a", readA, kNoWork);

    // B: writes mip 3 only -- disjoint from what A read, so it owes no barrier and must not discard
    // A's still-pending read of mip 0.
    ComputePassDesc writeB;
    writeB.textureWrites.push_back(TextureUseDesc(bloom, kMip3));
    graph.addComputePass("lmx.pass.b", writeB, kNoWork);

    // C: writes mip 0 of the version B produced -- must still be barriered after A's read.
    ComputePassDesc writeC;
    writeC.textureWrites.push_back(TextureUseDesc(nextVersion(bloom), kMip0));
    graph.addComputePass("lmx.pass.c", writeC, kNoWork);

    graph.exportTexture(GraphTexture{bloom.index, 2});
    graph.exportTexture(nextVersion(sink));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(commands.events == std::vector<std::string>{
                                   "begin lmx.pass.a", "end", "begin compute lmx.pass.b",
                                   "end compute",
                                   "barrier chain mips[0..0] layers[0..] ShaderRead->StorageWrite",
                                   "begin compute lmx.pass.c", "end compute"});
}

//======================================================================================================================
// A write-after-read discharge must shrink a pending read to what the write did not touch, not drop
// the whole entry the moment any part of it overlaps: A reads mips 0-3 in one declaration, D writes
// mip 0 alone (overlaps, so it owes a barrier -- and the read shrinks to mips 1-3, not to nothing),
// then E writes mip 2 and must still be barriered, because A's read of mip 2 was never discharged
// -- only mip 0 was. Erasing the whole 0-3 entry on D's overlap (rather than subtracting mip 0 from
// it) would leave E wrongly finding nothing owed.
TEST_CASE("a write-after-read barrier survives a partial-overlap discharge", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    FakeTexture other{64, 64, "other"};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");
    const GraphTexture sink = graph.importTexture(other, rhi::Format::BGRA8Unorm, "other");

    static constexpr rhi::TextureSubresourceRange kMips0to3{.baseMipLevel = 0, .mipLevelCount = 4};
    static constexpr rhi::TextureSubresourceRange kMip0{.baseMipLevel = 0, .mipLevelCount = 1};
    static constexpr rhi::TextureSubresourceRange kMip2{.baseMipLevel = 2, .mipLevelCount = 1};

    // A: reads mips 0-3 in one declaration, and writes a separate texture so it is not dead work no
    // sink reaches.
    PassDesc readA;
    readA.textureReads.push_back(TextureUseDesc(bloom, kMips0to3));
    readA.color = ColorAttachment{.handle = sink};
    graph.addPass("lmx.pass.a", readA, kNoWork);

    // D: writes mip 0 alone -- overlaps A's read, so it owes a barrier, but must only discharge mip
    // 0 of A's pending mips 0-3, not the whole entry.
    ComputePassDesc writeD;
    writeD.textureWrites.push_back(TextureUseDesc(bloom, kMip0));
    graph.addComputePass("lmx.pass.d", writeD, kNoWork);

    // E: writes mip 2 of the version D produced -- must still be barriered, since only mip 0 of A's
    // read was ever discharged.
    ComputePassDesc writeE;
    writeE.textureWrites.push_back(TextureUseDesc(nextVersion(bloom), kMip2));
    graph.addComputePass("lmx.pass.e", writeE, kNoWork);

    graph.exportTexture(GraphTexture{bloom.index, 2});
    graph.exportTexture(nextVersion(sink));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(commands.events == std::vector<std::string>{
                                   "begin lmx.pass.a", "end",
                                   "barrier chain mips[0..0] layers[0..] ShaderRead->StorageWrite",
                                   "begin compute lmx.pass.d", "end compute",
                                   "barrier chain mips[2..2] layers[0..] ShaderRead->StorageWrite",
                                   "begin compute lmx.pass.e", "end compute"});
}

//======================================================================================================================
// The companion to the case above: when a write fully covers a pending read (rather than only part
// of it), the read is completely discharged and a later write of a different, disjoint range owes
// nothing -- confirming the subtraction in the case above does not manufacture a spurious barrier
// when there is truly nothing left pending.
TEST_CASE("a fully covered read is completely discharged", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    FakeTexture other{64, 64, "other"};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");
    const GraphTexture sink = graph.importTexture(other, rhi::Format::BGRA8Unorm, "other");

    static constexpr rhi::TextureSubresourceRange kMip0{.baseMipLevel = 0, .mipLevelCount = 1};
    static constexpr rhi::TextureSubresourceRange kMip1{.baseMipLevel = 1, .mipLevelCount = 1};

    // A: reads mip 0 only, and writes a separate texture so it is not dead work no sink reaches.
    PassDesc readA;
    readA.textureReads.push_back(TextureUseDesc(bloom, kMip0));
    readA.color = ColorAttachment{.handle = sink};
    graph.addPass("lmx.pass.a", readA, kNoWork);

    // D: writes mip 0 -- exactly covers A's read, fully discharging it.
    ComputePassDesc writeD;
    writeD.textureWrites.push_back(TextureUseDesc(bloom, kMip0));
    graph.addComputePass("lmx.pass.d", writeD, kNoWork);

    // E: writes the disjoint mip 1 -- nothing is pending against it, so it owes no barrier.
    ComputePassDesc writeE;
    writeE.textureWrites.push_back(TextureUseDesc(nextVersion(bloom), kMip1));
    graph.addComputePass("lmx.pass.e", writeE, kNoWork);

    graph.exportTexture(GraphTexture{bloom.index, 2});
    graph.exportTexture(nextVersion(sink));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(commands.events == std::vector<std::string>{
                                   "begin lmx.pass.a", "end",
                                   "barrier chain mips[0..0] layers[0..] ShaderRead->StorageWrite",
                                   "begin compute lmx.pass.d", "end compute",
                                   "begin compute lmx.pass.e", "end compute"});
}

//======================================================================================================================
// The bloom step's shape: one pass reads mip 1 and writes mip 2 of the same chain. Under
// whole-resource versions the write still produces the next version of the whole texture, and mip 1
// carries forward into it untouched.
TEST_CASE("a compute pass reads and writes disjoint mips of one texture", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");

    ComputePassDesc downsample;
    downsample.textureReads.push_back({bloom, {.baseMipLevel = 1, .mipLevelCount = 1}});
    downsample.textureWrites.push_back({bloom, {.baseMipLevel = 2, .mipLevelCount = 1}});
    graph.addComputePass("lmx.pass.downsample", downsample, kNoWork);
    graph.exportTexture(nextVersion(bloom));

    const auto schedule = graph.compile();
    INFO(errorOf(schedule));
    REQUIRE(schedule.has_value());
    REQUIRE(schedule->passes == std::vector<uint32_t>{0});
}

//======================================================================================================================
// The same shape with the ranges made to share mip 2. No ordering between passes can fix a pass
// racing against itself, so it is a declaration failure -- and the message has to carry both ranges
// or it cannot say which end to move.
TEST_CASE("a pass reading and writing overlapping ranges is rejected", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");

    ComputePassDesc overlapping;
    overlapping.textureReads.push_back({bloom, {.baseMipLevel = 1, .mipLevelCount = 2}});
    overlapping.textureWrites.push_back({bloom, {.baseMipLevel = 2, .mipLevelCount = 1}});
    graph.addComputePass("lmx.pass.overlap", overlapping, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.overlap"));
    REQUIRE(schedule.error().message.contains("bloom"));
    REQUIRE(schedule.error().message.contains("mips[1..2]"));
    REQUIRE(schedule.error().message.contains("mips[2..2]"));
    REQUIRE(schedule.error().message.contains("disjoint"));
}

//======================================================================================================================
// Whole-resource ranges are the default, so a pass that names one texture as both a read and a
// write without narrowing either declares the maximal overlap there is.
TEST_CASE("a whole-resource read beside a whole-resource write is rejected", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");

    ComputePassDesc inPlace;
    inPlace.textureReads.push_back(bloom);
    inPlace.textureWrites.push_back(bloom);
    graph.addComputePass("lmx.pass.inPlace", inPlace, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("overlap"));
}

//======================================================================================================================
// Layers are the other axis: the same mip of two different faces is two different subresources, so
// the pair is disjoint and the rule must not collapse to a mip comparison.
TEST_CASE("ranges on different array layers do not overlap", "[render][graph]") {
    FakeTexture cube{64, 64, "cube", 1, 6};
    RenderGraph graph;
    const GraphTexture faces = graph.importTexture(cube, rhi::Format::RGBA16Float, "faces");

    ComputePassDesc perFace;
    perFace.textureReads.push_back({faces, {.baseArrayLayer = 0, .arrayLayerCount = 1}});
    perFace.textureWrites.push_back({faces, {.baseArrayLayer = 1, .arrayLayerCount = 1}});
    graph.addComputePass("lmx.pass.face", perFace, kNoWork);
    graph.exportTexture(nextVersion(faces));

    const auto schedule = graph.compile();
    INFO(errorOf(schedule));
    REQUIRE(schedule.has_value());
}

//======================================================================================================================
// A range is checked against the texture it names, so a mip the chain does not have is caught on
// the CPU with the pass named rather than by the RHI when the barrier is finally recorded.
TEST_CASE("a subresource range past the end of the texture is rejected", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 3};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");

    ComputePassDesc tooFar;
    tooFar.textureWrites.push_back({bloom, {.baseMipLevel = 3, .mipLevelCount = 1}});
    graph.addComputePass("lmx.pass.tooFar", tooFar, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.tooFar"));
    REQUIRE(schedule.error().message.contains("mips[3..3]"));
    REQUIRE(schedule.error().message.contains("3 mip levels"));
}

//======================================================================================================================
// A zero count is not "the whole resource", it is nothing at all -- a binding addressing no
// subresource is a declaration that says the pass touches something while touching nothing.
TEST_CASE("an empty subresource range is rejected", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 3};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");

    ComputePassDesc empty;
    empty.textureWrites.push_back({bloom, {.baseMipLevel = 0, .mipLevelCount = 0}});
    graph.addComputePass("lmx.pass.empty", empty, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("covers no subresource"));
}

//======================================================================================================================
// Subresource ranges narrow what a pass touches, not what a version covers. Two passes writing
// different mips of version 0 would both produce version 1, so the second one still has to declare
// itself over the first's output -- the double-write rule does not soften into a range comparison.
TEST_CASE("two passes writing disjoint ranges of one version are rejected", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");

    ComputePassDesc first;
    first.textureWrites.push_back({bloom, {.baseMipLevel = 1, .mipLevelCount = 1}});
    graph.addComputePass("lmx.pass.first", first, kNoWork);

    ComputePassDesc second;
    second.textureWrites.push_back({bloom, {.baseMipLevel = 2, .mipLevelCount = 1}});
    graph.addComputePass("lmx.pass.second", second, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("both write"));
    REQUIRE(schedule.error().message.contains("bloom"));
}

//======================================================================================================================
// Untouched-subresource inheritance, stated as a schedule: the writer touched mip 1 only, and a
// reader of mip 0 of the version it produced is reading contents carried forward from version 0.
// That is a legal read, and it is still ordered after the writer.
TEST_CASE("a read inherits the subresources its producer did not write", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    FakeTexture output{64, 64, "output"};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");
    const GraphTexture target = graph.importTexture(output, rhi::Format::BGRA8Unorm, "output");

    ComputePassDesc write;
    write.textureWrites.push_back({bloom, {.baseMipLevel = 1, .mipLevelCount = 1}});
    graph.addComputePass("lmx.pass.write", write, kNoWork);

    PassDesc read;
    read.textureReads.push_back({nextVersion(bloom), {.baseMipLevel = 0, .mipLevelCount = 1}});
    read.color = ColorAttachment{.handle = target};
    graph.addPass("lmx.pass.read", read, kNoWork);
    graph.exportTexture(nextVersion(target));

    const auto schedule = graph.compile();
    INFO(errorOf(schedule));
    REQUIRE(schedule.has_value());
    REQUIRE(schedule->passes == std::vector<uint32_t>{0, 1});
}

//======================================================================================================================
// Each declaration path opens its own RHI scope, and the barrier between them is derived from the
// declared uses on both sides: a storage write followed by a sampled read.
TEST_CASE("execute encodes each pass kind in its own scope", "[render][graph]") {
    FakeTexture storage{64, 64, "storage"};
    FakeTexture target{64, 64, "target"};
    RenderGraph graph;
    const GraphTexture written = graph.importTexture(storage, rhi::Format::RGBA16Float, "storage");
    const GraphTexture color = graph.importTexture(target, rhi::Format::BGRA8Unorm, "target");

    ComputePassDesc fill;
    fill.textureWrites.push_back(written);
    graph.addComputePass("lmx.pass.fill", fill, kNoWork);

    PassDesc present;
    present.textureReads.push_back(nextVersion(written));
    present.color = ColorAttachment{.handle = color};
    graph.addPass("lmx.pass.present", present, kNoWork);
    graph.exportTexture(nextVersion(color));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(commands.events == std::vector<std::string>{"begin compute lmx.pass.fill",
                                                        "end compute",
                                                        "barrier storage StorageWrite->ShaderRead",
                                                        "begin lmx.pass.present", "end"});
}

//======================================================================================================================
// A copy pass's destination is a write like any other, so a compute pass reading it afterwards gets
// the copy-to-storage transition -- and buffers get one of their own, because a hazard on bytes has
// no texture edge to borrow.
TEST_CASE("execute derives a buffer barrier from a copy destination", "[render][graph]") {
    FakeBuffer histogram{1024, "histogram"};
    RenderGraph graph;
    const GraphBuffer bins = graph.importBuffer(histogram, "histogram");

    CopyPassDesc clear;
    clear.bufferDestinations.push_back(bins);
    graph.addCopyPass("lmx.pass.clear", clear, kNoWork);

    ComputePassDesc accumulate;
    accumulate.bufferReads.push_back(nextVersion(bins));
    accumulate.bufferWrites.push_back(nextVersion(bins));
    graph.addComputePass("lmx.pass.accumulate", accumulate, kNoWork);
    graph.exportBuffer(GraphBuffer{bins.index, 2});

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(commands.events ==
            std::vector<std::string>{"begin copy lmx.pass.clear", "end copy",
                                     "barrier histogram CopyDestination->StorageRead",
                                     "begin compute lmx.pass.accumulate", "end compute"});
}

//======================================================================================================================
// The derived barrier carries the subresources the consumer declared, so a reader of one mip does
// not describe itself as depending on the whole chain.
TEST_CASE("a derived barrier carries the range the reader declared", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");

    ComputePassDesc write;
    write.textureWrites.push_back({bloom, {.baseMipLevel = 0, .mipLevelCount = 1}});
    graph.addComputePass("lmx.pass.write", write, kNoWork);

    ComputePassDesc downsample;
    downsample.textureReads.push_back(
        {nextVersion(bloom), {.baseMipLevel = 0, .mipLevelCount = 1}});
    downsample.textureWrites.push_back(
        {nextVersion(bloom), {.baseMipLevel = 1, .mipLevelCount = 1}});
    graph.addComputePass("lmx.pass.downsample", downsample, kNoWork);
    graph.exportTexture(GraphTexture{bloom.index, 2});

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(commands.events ==
            std::vector<std::string>{"begin compute lmx.pass.write", "end compute",
                                     "barrier chain mips[0..0] layers[0..] "
                                     "StorageWrite->StorageRead",
                                     "begin compute lmx.pass.downsample", "end compute"});
}

//======================================================================================================================
// The record is the frame as compilation saw it: what was imported, what each pass declared, and in
// which order it runs. An observer reads it without the graph, so everything it needs has to be in
// it -- names included, since a resource index alone names nothing to a reader.
TEST_CASE("compileFrame records the declarations it compiled", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024, "shadowMap"};
    FakeTexture color{64, 64, "sceneColor"};
    FakeBuffer storage{256, "instances"};
    RenderGraph graph;
    const GraphTexture shadow = graph.importTexture(shadowMap, rhi::Format::D32Float, "shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");
    const GraphBuffer instances = graph.importBuffer(storage, "instances");

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(shadow));
    scene.bufferReads.push_back(instances);
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadowPass, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    const auto record = graph.compileFrame(42);
    INFO(errorOf(record));
    REQUIRE(record.has_value());

    REQUIRE(record->frameId == 42);

    REQUIRE(record->debug.resources.size() == 3);
    REQUIRE(record->debug.resources[0].name == "shadowMap");
    REQUIRE(record->debug.resources[0].kind == GraphResourceKind::Texture);
    REQUIRE(record->debug.resources[0].format == rhi::Format::D32Float);
    REQUIRE(record->debug.resources[2].name == "instances");
    REQUIRE(record->debug.resources[2].kind == GraphResourceKind::Buffer);

    // Declaration order, not schedule order: the record describes the frame that was declared, and
    // the schedule is a separate answer about it.
    REQUIRE(record->debug.passes.size() == 2);
    REQUIRE(record->debug.passes[0].label == "lmx.pass.scene");
    REQUIRE(record->debug.passes[0].kind == PassKind::Raster);
    REQUIRE(record->debug.passes[1].label == "lmx.pass.shadow");

    const std::vector<DebugUse>& uses = record->debug.passes[0].uses;
    REQUIRE(uses.size() == 3);
    REQUIRE(uses[0].resource == shadow.index);
    REQUIRE(uses[0].version == 1);
    REQUIRE(uses[0].role == UseRole::Read);
    REQUIRE(uses[1].resource == instances.index);
    REQUIRE(uses[1].role == UseRole::Read);
    REQUIRE(uses[2].resource == sceneColor.index);
    REQUIRE(uses[2].role == UseRole::ColorAttachment);

    REQUIRE(record->debug.schedule.passes == std::vector<uint32_t>{1, 0});
}

//======================================================================================================================
// The record's transitions are the barriers, not a second opinion about them: execute() emits what
// compilation recorded, so an inspector reading the record and a capture of the frame describe the
// same synchronisation.
TEST_CASE("execute emits exactly the transitions the record lists", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024, "shadowMap"};
    FakeTexture color{64, 64, "sceneColor"};
    FakeBuffer storage{256, "histogram"};
    RenderGraph graph;
    const GraphTexture shadow = graph.importTexture(shadowMap, rhi::Format::D32Float, "shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");
    const GraphBuffer bins = graph.importBuffer(storage, "histogram");

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadowPass, kNoWork);

    CopyPassDesc clear;
    clear.bufferDestinations.push_back(bins);
    graph.addCopyPass("lmx.pass.clear", clear, kNoWork);

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(shadow));
    scene.bufferReads.push_back(nextVersion(bins));
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    RecordingCommandList commands;
    const CompiledFrameRecord record = graph.execute(commands, 7);

    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].beforePass == 2);
    REQUIRE(record.debug.transitions[0].kind == GraphResourceKind::Texture);
    REQUIRE(record.debug.transitions[0].resource == shadow.index);
    REQUIRE(record.debug.transitions[0].textureFrom == rhi::TextureUse::RenderTarget);
    REQUIRE(record.debug.transitions[0].textureTo == rhi::TextureUse::ShaderRead);
    REQUIRE(record.debug.transitions[1].kind == GraphResourceKind::Buffer);
    REQUIRE(record.debug.transitions[1].resource == bins.index);
    REQUIRE(record.debug.transitions[1].bufferFrom == rhi::BufferUse::CopyDestination);
    REQUIRE(record.debug.transitions[1].bufferTo == rhi::BufferUse::ShaderRead);

    REQUIRE(commands.events ==
            std::vector<std::string>{"begin lmx.pass.shadow", "end", "begin copy lmx.pass.clear",
                                     "end copy", "barrier shadowMap RenderTarget->ShaderRead",
                                     "barrier histogram CopyDestination->ShaderRead",
                                     "begin lmx.pass.scene", "end"});
}

//======================================================================================================================
// The frame number stamps the record and takes no part in compiling it, so two frames declaring the
// same passes differ in nothing else -- which is what lets a dump be compared across runs.
TEST_CASE("the frame id changes nothing else about a record", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    const auto first = graph.compileFrame(1);
    const auto second = graph.compileFrame(9001);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    REQUIRE(first->frameId != second->frameId);
    REQUIRE(first->debug.schedule.passes == second->debug.schedule.passes);
    REQUIRE(first->debug.passes.size() == second->debug.passes.size());
    REQUIRE(first->debug.passes[0].label == second->debug.passes[0].label);
    REQUIRE(first->debug.transitions.size() == second->debug.transitions.size());
}

//======================================================================================================================
// Nothing roots this frame, so nothing in it is asked for. The passes are valid, and that is the
// point: culling is about what the frame is for, not about whether it is well formed.
TEST_CASE("a graph with no sink schedules nothing", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.schedule.passes.empty());
    REQUIRE(record->debug.passes[0].cullReason == CullReason::NoSinkReachesIt);
}

//======================================================================================================================
// The frame that says what culling is for: a chain that reaches the export survives whole, and a
// chain that does not is dropped whole. Both reasons appear, and the record keeps the culled passes
// in declaration order so an observer can see what was left out and why.
TEST_CASE("only the passes a sink reaches are scheduled", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture displayed{64, 64, "displayColor"};
    FakeTexture unused{64, 64, "unusedTarget"};
    FakeBuffer probe{256, "probe"};
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

    // Reads the scene colour and writes nothing at all, so no sink could name its output even in
    // principle -- a different mistake from the orphan's, and recorded as one.
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
    INFO(errorOf(record));
    REQUIRE(record.has_value());

    REQUIRE(record->debug.schedule.passes == std::vector<uint32_t>{0, 3});
    REQUIRE_FALSE(record->debug.passes[0].cullReason.has_value());
    REQUIRE(record->debug.passes[1].cullReason == CullReason::NoSinkReachesIt);
    REQUIRE(record->debug.passes[2].cullReason == CullReason::ProducesNothing);
    REQUIRE_FALSE(record->debug.passes[3].cullReason.has_value());

    // The culled passes are still declared, so the record still describes them in full.
    REQUIRE(record->debug.passes.size() == 4);
    REQUIRE(record->debug.passes[2].label == "lmx.pass.observer");
    REQUIRE(record->debug.passes[2].uses.size() == 2);

    REQUIRE(record->debug.sinks.size() == 1);
    REQUIRE(record->debug.sinks[0].kind == SinkKind::Export);
    REQUIRE(record->debug.sinks[0].resource == displayColor.index);
    REQUIRE(record->debug.sinks[0].version == 1);
}

//======================================================================================================================
// Culling is a function of the declarations, so the same declarations answer the same way whichever
// order the sinks were declared in and however many times compilation is asked.
TEST_CASE("culling answers the same way every time", "[render][graph]") {
    const auto build = [](RenderGraph& graph, FakeTexture& first, FakeTexture& second,
                          bool exportFirst) {
        const GraphTexture a = graph.importTexture(first, rhi::Format::BGRA8Unorm, "a");
        const GraphTexture b = graph.importTexture(second, rhi::Format::BGRA8Unorm, "b");
        PassDesc writeA;
        writeA.color = ColorAttachment{.handle = a};
        graph.addPass("lmx.pass.a", writeA, kNoWork);
        PassDesc writeB;
        writeB.color = ColorAttachment{.handle = b};
        graph.addPass("lmx.pass.b", writeB, kNoWork);
        if (exportFirst) {
            graph.exportTexture(nextVersion(a));
            graph.exportTexture(nextVersion(b));
        } else {
            graph.exportTexture(nextVersion(b));
            graph.exportTexture(nextVersion(a));
        }
    };

    FakeTexture first{64, 64, "a"};
    FakeTexture second{64, 64, "b"};

    RenderGraph forward;
    build(forward, first, second, true);
    RenderGraph reversed;
    build(reversed, first, second, false);

    const auto once = forward.compileFrame(1);
    const auto twice = forward.compileFrame(1);
    const auto other = reversed.compileFrame(1);
    REQUIRE(once.has_value());
    REQUIRE(twice.has_value());
    REQUIRE(other.has_value());

    REQUIRE(once->debug.schedule.passes == std::vector<uint32_t>{0, 1});
    REQUIRE(twice->debug.schedule.passes == once->debug.schedule.passes);
    REQUIRE(other->debug.schedule.passes == once->debug.schedule.passes);
}

//======================================================================================================================
// A culled pass emits nothing at all -- not the scope, not the body, and not the barrier a read of
// its output would otherwise have justified.
TEST_CASE("execute runs none of a culled pass", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture unused{64, 64, "unusedTarget"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture orphan =
        graph.importTexture(unused, rhi::Format::BGRA8Unorm, "unusedTarget");

    bool orphanRan = false;
    PassDesc orphaned;
    orphaned.color = ColorAttachment{.handle = orphan};
    graph.addPass("lmx.pass.orphan", orphaned,
                  [&orphanRan](const PassResources&) { orphanRan = true; });

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(orphan));
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    // Roots the orphan's own output rather than the scene's, so the scene pass is the culled one
    // and the transition its read would have justified goes with it.
    graph.exportTexture(nextVersion(orphan));

    RecordingCommandList commands;
    graph.execute(commands, 1);

    REQUIRE(orphanRan);
    REQUIRE(commands.events == std::vector<std::string>{"begin lmx.pass.orphan", "end"});
}

//======================================================================================================================
// Presentation and readback root work exactly as an export does; they are separate kinds because
// what a frame does with a result is part of what the frame declared, not a detail of the export.
TEST_CASE("presentation and readback root work like an export", "[render][graph]") {
    FakeTexture drawable{64, 64, "drawable"};
    FakeBuffer histogram{1024, "histogram"};
    RenderGraph graph;
    const GraphTexture backbuffer =
        graph.importTexture(drawable, rhi::Format::BGRA8Unorm, "drawable");
    const GraphBuffer bins = graph.importBuffer(histogram, "histogram");

    PassDesc ui;
    ui.color = ColorAttachment{.handle = backbuffer};
    graph.addPass("lmx.pass.ui", ui, kNoWork);

    ComputePassDesc metering;
    metering.bufferWrites.push_back(bins);
    graph.addComputePass("lmx.pass.metering", metering, kNoWork);

    graph.presentTexture(nextVersion(backbuffer));
    graph.readbackBuffer(nextVersion(bins));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.schedule.passes == std::vector<uint32_t>{0, 1});
    REQUIRE(record->debug.sinks[0].kind == SinkKind::Present);
    REQUIRE(record->debug.sinks[0].resourceKind == GraphResourceKind::Texture);
    REQUIRE(record->debug.sinks[1].kind == SinkKind::Readback);
    REQUIRE(record->debug.sinks[1].resourceKind == GraphResourceKind::Buffer);
}

//======================================================================================================================
// Every sink kind roots a version a pass produced, and says which kind of declaration it was: a
// presented drawable nothing drew into is as broken as an exported one, and the message has to name
// the declaration the caller actually made.
TEST_CASE("a sink naming a version no pass wrote is rejected", "[render][graph]") {
    FakeTexture drawable{64, 64, "drawable"};
    FakeBuffer histogram{1024, "histogram"};

    RenderGraph presented;
    const GraphTexture backbuffer =
        presented.importTexture(drawable, rhi::Format::BGRA8Unorm, "drawable");
    presented.presentTexture(nextVersion(backbuffer));
    const auto presentFailure = presented.compile();
    REQUIRE_FALSE(presentFailure.has_value());
    REQUIRE(presentFailure.error().message.contains("presented texture 'drawable'"));
    REQUIRE(presentFailure.error().message.contains("not written by any pass"));

    RenderGraph exported;
    const GraphBuffer bins = exported.importBuffer(histogram, "histogram");
    exported.exportBuffer(nextVersion(bins));
    const auto exportFailure = exported.compile();
    REQUIRE_FALSE(exportFailure.has_value());
    REQUIRE(exportFailure.error().message.contains("exported buffer 'histogram'"));
}

//======================================================================================================================
// Validation covers every declared pass, not only the scheduled ones: a pass that would never run
// is still a pass the caller wrote down wrong, and reporting it only once a sink happens to reach
// it would make the failure depend on unrelated declarations.
TEST_CASE("a culled pass is still validated", "[render][graph]") {
    FakeTexture color{64, 64, "sceneColor"};
    FakeTexture shadowMap{1024, 1024, "shadowMap"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rhi::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture shadow = graph.importTexture(shadowMap, rhi::Format::D32Float, "shadowMap");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    // Unreachable from the export, and mis-declared: a depth texture in the colour slot.
    PassDesc broken;
    broken.color = ColorAttachment{.handle = shadow};
    graph.addPass("lmx.pass.broken", broken, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.broken"));
    REQUIRE(schedule.error().message.contains("D32Float"));
}

//======================================================================================================================
// A barrier orders the passes it sits between, so the range it names has to cover the reader it
// sits in front of -- one of the two axes a barrier is scoped on (rhi::CommandList::textureBarrier
// states the model; the consuming stage class is the other, two cases below). One writer of the
// whole chain and two readers of different mips is the case that tells the two range rules apart:
// "one transition per write" would leave the second reader unordered, and only the second reader's
// own barrier states the dependency it actually has.
TEST_CASE("each reader of a distinct range gets its own transition", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    FakeTexture first{64, 64, "first"};
    FakeTexture second{64, 64, "second"};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");
    const GraphTexture a = graph.importTexture(first, rhi::Format::BGRA8Unorm, "first");
    const GraphTexture b = graph.importTexture(second, rhi::Format::BGRA8Unorm, "second");

    ComputePassDesc write;
    write.textureWrites.push_back(bloom);
    graph.addComputePass("lmx.pass.write", write, kNoWork);

    PassDesc readMip0;
    readMip0.textureReads.push_back({nextVersion(bloom), {.baseMipLevel = 0, .mipLevelCount = 1}});
    readMip0.color = ColorAttachment{.handle = a};
    graph.addPass("lmx.pass.readMip0", readMip0, kNoWork);

    PassDesc readMip1;
    readMip1.textureReads.push_back({nextVersion(bloom), {.baseMipLevel = 1, .mipLevelCount = 1}});
    readMip1.color = ColorAttachment{.handle = b};
    graph.addPass("lmx.pass.readMip1", readMip1, kNoWork);

    graph.exportTexture(nextVersion(a));
    graph.exportTexture(nextVersion(b));

    RecordingCommandList commands;
    const CompiledFrameRecord record = graph.execute(commands, 1);

    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].beforePass == 1);
    REQUIRE(record.debug.transitions[0].range.baseMipLevel == 0);
    REQUIRE(record.debug.transitions[1].beforePass == 2);
    REQUIRE(record.debug.transitions[1].range.baseMipLevel == 1);

    REQUIRE(commands.events == std::vector<std::string>{"begin compute lmx.pass.write",
                                                        "end compute",
                                                        "barrier chain mips[0..0] layers[0..] "
                                                        "StorageWrite->ShaderRead",
                                                        "begin lmx.pass.readMip0", "end",
                                                        "barrier chain mips[1..1] layers[0..] "
                                                        "StorageWrite->ShaderRead",
                                                        "begin lmx.pass.readMip1", "end"});
}

//======================================================================================================================
// The other half of the same rule, and what keeps the shipped frame's barrier count where M4 left
// it: a reader whose subresources an earlier barrier already named, *and* whose stage class that
// barrier was consumed by, is already ordered, so a second barrier would be pure cost. The first
// reader here takes the whole chain, which encloses the second reader's single mip, and both are
// raster passes -- passes of one stage class are ordered among themselves, so the one barrier
// reaches the second reader too. Neither condition alone is enough; the two cases below are what
// each of them rules out.
TEST_CASE("a reader enclosed by an earlier transition gets none", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    FakeTexture first{64, 64, "first"};
    FakeTexture second{64, 64, "second"};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rhi::Format::RGBA16Float, "bloom");
    const GraphTexture a = graph.importTexture(first, rhi::Format::BGRA8Unorm, "first");
    const GraphTexture b = graph.importTexture(second, rhi::Format::BGRA8Unorm, "second");

    ComputePassDesc write;
    write.textureWrites.push_back(bloom);
    graph.addComputePass("lmx.pass.write", write, kNoWork);

    PassDesc readAll;
    readAll.textureReads.push_back(nextVersion(bloom));
    readAll.color = ColorAttachment{.handle = a};
    graph.addPass("lmx.pass.readAll", readAll, kNoWork);

    PassDesc readMip2;
    readMip2.textureReads.push_back({nextVersion(bloom), {.baseMipLevel = 2, .mipLevelCount = 1}});
    readMip2.color = ColorAttachment{.handle = b};
    graph.addPass("lmx.pass.readMip2", readMip2, kNoWork);

    graph.exportTexture(nextVersion(a));
    graph.exportTexture(nextVersion(b));

    RecordingCommandList commands;
    const CompiledFrameRecord record = graph.execute(commands, 1);

    REQUIRE(record.debug.transitions.size() == 1);
    REQUIRE(commands.events == std::vector<std::string>{"begin compute lmx.pass.write",
                                                        "end compute",
                                                        "barrier chain StorageWrite->ShaderRead",
                                                        "begin lmx.pass.readAll", "end",
                                                        "begin lmx.pass.readMip2", "end"});
}

//======================================================================================================================
// The stage-class axis, on the exact shape the shipped frame takes with auto-exposure on and bloom
// off: the scene target's first reader is the histogram dispatch and its second is the display
// raster pass, both over the whole texture. A barrier is scoped to the stage class of the pass that
// consumes it, so the compute reader's barrier orders nothing for the raster one -- an enclosing
// range is not enough, and the display pass would otherwise sample the target with nothing ordering
// it against the scene pass's writes.
TEST_CASE("a reader of another stage class is not covered by an earlier transition",
          "[render][graph]") {
    FakeTexture sceneColorTarget{64, 64, "sceneColor"};
    FakeBuffer histogramBuffer{1024, "histogram"};
    FakeTexture displayTarget{64, 64, "display"};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(sceneColorTarget, rhi::Format::RGBA16Float, "sceneColor");
    const GraphBuffer histogram = graph.importBuffer(histogramBuffer, "histogram");
    const GraphTexture display =
        graph.importTexture(displayTarget, rhi::Format::BGRA8Unorm, "display");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    const GraphTexture sceneRead = nextVersion(sceneColor);

    ComputePassDesc histogramPass;
    histogramPass.textureReads.push_back(sceneRead);
    histogramPass.bufferWrites.push_back(histogram);
    graph.addComputePass("lmx.pass.histogram", histogramPass, kNoWork);

    PassDesc displayPass;
    displayPass.textureReads.push_back(sceneRead);
    displayPass.color = ColorAttachment{.handle = display};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);

    graph.exportBuffer(nextVersion(histogram));
    graph.exportTexture(nextVersion(display));

    RecordingCommandList commands;
    const CompiledFrameRecord record = graph.execute(commands, 1);

    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].beforePass == 1);
    REQUIRE(record.debug.transitions[0].textureTo == rhi::TextureUse::StorageRead);
    REQUIRE(record.debug.transitions[1].beforePass == 2);
    REQUIRE(record.debug.transitions[1].textureTo == rhi::TextureUse::ShaderRead);

    REQUIRE(commands.events ==
            std::vector<std::string>{
                "begin lmx.pass.scene", "end", "barrier sceneColor RenderTarget->StorageRead",
                "begin compute lmx.pass.histogram", "end compute",
                "barrier sceneColor RenderTarget->ShaderRead", "begin lmx.pass.display", "end"});
}

//======================================================================================================================
// The same rule on the buffer path, which has no ranges to fall back on: a buffer's first barrier
// covers every byte, so the stage class is the only thing that can tell two readers apart. The
// exposure buffer's seeded value is read by the scene raster pass and then by the histogram
// dispatch, and each owes a barrier of its own.
TEST_CASE("a buffer reader of another stage class is not covered either", "[render][graph]") {
    FakeBuffer exposureBuffer{16, "exposure"};
    FakeTexture sceneColorTarget{64, 64, "sceneColor"};
    FakeBuffer histogramBuffer{1024, "histogram"};
    RenderGraph graph;
    const GraphBuffer exposure = graph.importBuffer(exposureBuffer, "exposure");
    const GraphTexture sceneColor =
        graph.importTexture(sceneColorTarget, rhi::Format::RGBA16Float, "sceneColor");
    const GraphBuffer histogram = graph.importBuffer(histogramBuffer, "histogram");

    ComputePassDesc seed;
    seed.bufferWrites.push_back(exposure);
    graph.addComputePass("lmx.pass.seed", seed, kNoWork);

    const GraphBuffer exposureRead = nextVersion(exposure);

    PassDesc scene;
    scene.bufferReads.push_back(exposureRead);
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    ComputePassDesc histogramPass;
    histogramPass.bufferReads.push_back(exposureRead);
    histogramPass.bufferWrites.push_back(histogram);
    graph.addComputePass("lmx.pass.histogram", histogramPass, kNoWork);

    graph.exportTexture(nextVersion(sceneColor));
    graph.exportBuffer(nextVersion(histogram));

    RecordingCommandList commands;
    const CompiledFrameRecord record = graph.execute(commands, 1);

    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].bufferTo == rhi::BufferUse::ShaderRead);
    REQUIRE(record.debug.transitions[1].bufferTo == rhi::BufferUse::StorageRead);

    REQUIRE(commands.events ==
            std::vector<std::string>{"begin compute lmx.pass.seed", "end compute",
                                     "barrier exposure StorageWrite->ShaderRead",
                                     "begin lmx.pass.scene", "end",
                                     "barrier exposure StorageWrite->StorageRead",
                                     "begin compute lmx.pass.histogram", "end compute"});
}

//======================================================================================================================
// A frame reading contents an earlier frame produced has no producer inside it to derive a barrier
// from, and the frames in flight give no ordering of their own -- so the import states the producer
// instead, and this frame's first reader is barriered against it as if the write were its own.
TEST_CASE("a buffer imported with a prior producer transitions before its first reader",
          "[render][graph]") {
    FakeBuffer exposureBuffer{16, "exposure"};
    FakeTexture sceneColorTarget{64, 64, "sceneColor"};
    RenderGraph graph;
    const GraphBuffer exposure =
        graph.importBuffer(exposureBuffer, "exposure", rhi::BufferUse::StorageWrite);
    const GraphTexture sceneColor =
        graph.importTexture(sceneColorTarget, rhi::Format::RGBA16Float, "sceneColor");

    PassDesc scene;
    scene.bufferReads.push_back(exposure);
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    RecordingCommandList commands;
    const CompiledFrameRecord record = graph.execute(commands, 1);

    REQUIRE(record.debug.transitions.size() == 1);
    REQUIRE(record.debug.transitions[0].beforePass == 0);
    REQUIRE(record.debug.transitions[0].kind == GraphResourceKind::Buffer);
    REQUIRE(record.debug.transitions[0].bufferFrom == rhi::BufferUse::StorageWrite);
    REQUIRE(record.debug.transitions[0].bufferTo == rhi::BufferUse::ShaderRead);

    // The barrier leads the frame: nothing precedes the pass that consumes it.
    REQUIRE(commands.events == std::vector<std::string>{"barrier exposure StorageWrite->ShaderRead",
                                                        "begin lmx.pass.scene", "end"});
}

//======================================================================================================================
// The other half of that contract, and what keeps manual mode's frame exactly what it was: an
// import that claims no prior producer states no cross-frame edge, so a reader of it is barriered
// against nothing. Same frame as above, differing only in which import overload it used.
TEST_CASE("a buffer imported without a prior producer transitions nothing", "[render][graph]") {
    FakeBuffer exposureBuffer{16, "exposure"};
    FakeTexture sceneColorTarget{64, 64, "sceneColor"};
    RenderGraph graph;
    const GraphBuffer exposure = graph.importBuffer(exposureBuffer, "exposure");
    const GraphTexture sceneColor =
        graph.importTexture(sceneColorTarget, rhi::Format::RGBA16Float, "sceneColor");

    PassDesc scene;
    scene.bufferReads.push_back(exposure);
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    RecordingCommandList commands;
    const CompiledFrameRecord record = graph.execute(commands, 1);

    REQUIRE(record.debug.transitions.empty());
    REQUIRE(commands.events == std::vector<std::string>{"begin lmx.pass.scene", "end"});
}

namespace {

// One transient the fake device sizes to exactly one alignment unit: 64 * 64 texels at four bytes
// each is 16 KiB, which is FakeDevice::kTextureAlignment. Every offset below is therefore a
// multiple of the size, which keeps the packing readable in the assertions.
constexpr TransientTextureDesc kTransientColor{.width = 64,
                                               .height = 64,
                                               .format = rhi::Format::RGBA16Float,
                                               .renderTarget = true,
                                               .sampled = true};

// Two transients used one after the other, with an imported target between them so the first one's
// lifetime closes before the second one's opens. It is the shape aliasing exists for.
struct DisjointFrame {
    FakeDevice device;
    TransientPool pool{device};
    FakeTexture midTarget{64, 64, "mid"};
    FakeTexture outTarget{64, 64, "out"};
    RenderGraph graph{pool};

    //==================================================================================================================
    // `secondDesc` is a parameter so a test can make the second transient incompatible with the
    // first without restating the frame.
    void declare(const TransientTextureDesc& secondDesc = kTransientColor) {
        const GraphTexture first = graph.createTexture(kTransientColor, "lmx.transient.first");
        const GraphTexture second = graph.createTexture(secondDesc, "lmx.transient.second");
        const GraphTexture mid = graph.importTexture(midTarget, rhi::Format::BGRA8Unorm, "mid");
        const GraphTexture out = graph.importTexture(outTarget, rhi::Format::BGRA8Unorm, "out");

        PassDesc writeFirst;
        writeFirst.color = ColorAttachment{.handle = first};
        graph.addPass("lmx.pass.writeFirst", writeFirst, kNoWork);

        PassDesc readFirst;
        readFirst.textureReads.push_back(nextVersion(first));
        readFirst.color = ColorAttachment{.handle = mid};
        graph.addPass("lmx.pass.readFirst", readFirst, kNoWork);

        PassDesc writeSecond;
        writeSecond.color = ColorAttachment{.handle = second};
        graph.addPass("lmx.pass.writeSecond", writeSecond, kNoWork);

        PassDesc readSecond;
        readSecond.textureReads.push_back(nextVersion(second));
        readSecond.color = ColorAttachment{.handle = out};
        graph.addPass("lmx.pass.readSecond", readSecond, kNoWork);

        graph.exportTexture(nextVersion(mid));
        graph.exportTexture(nextVersion(out));
    }
};

} // namespace

//======================================================================================================================
// The whole point of a transient: two of them whose lifetimes do not overlap occupy one set of
// bytes, and the frame's heap is sized to the larger of them rather than to their sum.
TEST_CASE("two transients whose lifetimes do not overlap share one placement", "[render][graph]") {
    DisjointFrame frame;
    frame.declare();

    const auto record = frame.graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());

    REQUIRE(record->debug.transients.size() == 2);
    const DebugTransient& first = record->debug.transients[0];
    const DebugTransient& second = record->debug.transients[1];

    REQUIRE(first.resource == 0);
    REQUIRE(first.used);
    REQUIRE(first.firstPass == 0);
    REQUIRE(first.lastPass == 1);
    REQUIRE(first.size == FakeDevice::kTextureAlignment);
    REQUIRE(first.alignment == FakeDevice::kTextureAlignment);
    REQUIRE(first.offset == 0);
    REQUIRE_FALSE(first.aliases);

    REQUIRE(second.resource == 1);
    REQUIRE(second.firstPass == 2);
    REQUIRE(second.lastPass == 3);
    REQUIRE(second.aliases);
    REQUIRE(second.offset == first.offset);

    REQUIRE(record->debug.memory.requested == 2 * FakeDevice::kTextureAlignment);
    REQUIRE(record->debug.memory.highWater == FakeDevice::kTextureAlignment);
    REQUIRE(record->debug.memory.aliasSavings == FakeDevice::kTextureAlignment);
    REQUIRE(record->debug.poolingEnabled);
}

//======================================================================================================================
// The barrier the version chain cannot state, because its two sides are different logical
// resources: the second transient's first write has to wait on the first one's last read, and it is
// whole-resource whatever the two declared.
TEST_CASE("a transient taking another's bytes is barriered against it", "[render][graph]") {
    DisjointFrame frame;
    frame.declare();

    const auto record = frame.graph.compileFrame(1);
    REQUIRE(record.has_value());

    // The read-after-write of each transient, plus the one reuse boundary between them.
    REQUIRE(record->debug.transitions.size() == 3);
    const DebugTransition& reuse = record->debug.transitions[1];
    REQUIRE(reuse.beforePass == 2);
    REQUIRE(reuse.resource == 1);
    REQUIRE(reuse.aliasedFrom.has_value());
    REQUIRE(*reuse.aliasedFrom == 0);
    REQUIRE(reuse.kind == GraphResourceKind::Texture);
    REQUIRE(reuse.textureFrom == rhi::TextureUse::ShaderRead);
    REQUIRE(reuse.textureTo == rhi::TextureUse::RenderTarget);
    REQUIRE(reuse.range.mipLevelCount == rhi::kAllMipLevels);
    REQUIRE(reuse.range.arrayLayerCount == rhi::kAllArrayLayers);

    // The two ordinary transitions carry no alias, so an observer can tell the two kinds apart.
    REQUIRE_FALSE(record->debug.transitions[0].aliasedFrom.has_value());
    REQUIRE_FALSE(record->debug.transitions[2].aliasedFrom.has_value());
}

//======================================================================================================================
// Lifetime disjointness is the precondition, not a preference: a transient still live when the next
// one starts gets its own bytes, and the heap grows to hold both.
TEST_CASE("two transients whose lifetimes overlap get separate placements", "[render][graph]") {
    FakeDevice device;
    TransientPool pool(device);
    FakeTexture outTarget{64, 64, "out"};
    RenderGraph graph(pool);
    const GraphTexture first = graph.createTexture(kTransientColor, "lmx.transient.first");
    const GraphTexture second = graph.createTexture(kTransientColor, "lmx.transient.second");
    const GraphTexture out = graph.importTexture(outTarget, rhi::Format::BGRA8Unorm, "out");

    PassDesc writeFirst;
    writeFirst.color = ColorAttachment{.handle = first};
    graph.addPass("lmx.pass.writeFirst", writeFirst, kNoWork);

    // Reads the first while writing the second, so the two are live at once.
    PassDesc writeSecond;
    writeSecond.textureReads.push_back(nextVersion(first));
    writeSecond.color = ColorAttachment{.handle = second};
    graph.addPass("lmx.pass.writeSecond", writeSecond, kNoWork);

    PassDesc present;
    present.textureReads.push_back(nextVersion(second));
    present.color = ColorAttachment{.handle = out};
    graph.addPass("lmx.pass.present", present, kNoWork);

    graph.exportTexture(nextVersion(out));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());

    REQUIRE(record->debug.transients[0].offset == 0);
    REQUIRE(record->debug.transients[1].offset == FakeDevice::kTextureAlignment);
    REQUIRE_FALSE(record->debug.transients[1].aliases);
    REQUIRE(record->debug.memory.highWater == 2 * FakeDevice::kTextureAlignment);
    REQUIRE(record->debug.memory.aliasSavings == 0);
}

//======================================================================================================================
// Compatibility is equality on every axis that decides a layout, so a difference in any one of them
// keeps two transients apart however well their lifetimes fit.
TEST_CASE("transients of differing descriptors never share bytes", "[render][graph]") {
    const std::array<std::pair<const char*, TransientTextureDesc>, 3> incompatible = {{
        // Same size and usage, different format.
        {"format",
         {.width = 64,
          .height = 64,
          .format = rhi::Format::RGBA8Unorm,
          .renderTarget = true,
          .sampled = true}},
        // Same format and footprint, one extra usage.
        {"usage",
         {.width = 64,
          .height = 64,
          .format = rhi::Format::RGBA16Float,
          .renderTarget = true,
          .sampled = true,
          .storageWrite = true}},
        // Same format and usage, different extent -- and therefore a different size.
        {"extent",
         {.width = 128,
          .height = 128,
          .format = rhi::Format::RGBA16Float,
          .renderTarget = true,
          .sampled = true}},
    }};

    for (const auto& [axis, desc] : incompatible) {
        INFO("differing axis: " + std::string(axis));
        DisjointFrame frame;
        frame.declare(desc);

        const auto record = frame.graph.compileFrame(1);
        INFO(errorOf(record));
        REQUIRE(record.has_value());

        REQUIRE_FALSE(record->debug.transients[1].aliases);
        REQUIRE(record->debug.transients[1].offset != record->debug.transients[0].offset);
        REQUIRE(record->debug.memory.aliasSavings == 0);
        // Nothing aliased, so nothing needed a reuse boundary either.
        for (const DebugTransition& transition : record->debug.transitions) {
            REQUIRE_FALSE(transition.aliasedFrom.has_value());
        }
    }
}

//======================================================================================================================
// Pooling off is the same frame with the reuse taken away: the same passes in the same order over
// the same declarations, with every transient on its own bytes and no reuse boundary to order.
TEST_CASE("pooling off gives every transient its own bytes", "[render][graph]") {
    DisjointFrame pooled;
    pooled.declare();
    DisjointFrame unpooled;
    unpooled.graph.setPoolingEnabled(false);
    unpooled.declare();

    const auto withPooling = pooled.graph.compileFrame(1);
    const auto withoutPooling = unpooled.graph.compileFrame(1);
    REQUIRE(withPooling.has_value());
    REQUIRE(withoutPooling.has_value());

    REQUIRE(withoutPooling->debug.schedule.passes == withPooling->debug.schedule.passes);
    REQUIRE_FALSE(withoutPooling->debug.poolingEnabled);
    REQUIRE_FALSE(withoutPooling->debug.transients[1].aliases);
    REQUIRE(withoutPooling->debug.transients[1].offset == FakeDevice::kTextureAlignment);
    REQUIRE(withoutPooling->debug.memory.requested == withPooling->debug.memory.requested);
    REQUIRE(withoutPooling->debug.memory.highWater == 2 * FakeDevice::kTextureAlignment);
    REQUIRE(withoutPooling->debug.memory.aliasSavings == 0);

    // One transition fewer, and it is exactly the reuse boundary aliasing needed.
    REQUIRE(withoutPooling->debug.transitions.size() == 2);
    for (const DebugTransition& transition : withoutPooling->debug.transitions) {
        REQUIRE_FALSE(transition.aliasedFrom.has_value());
    }
}

//======================================================================================================================
// A transient only a culled pass named costs nothing: the frame did not need the pass, so it does
// not pay for the memory the pass would have written into.
TEST_CASE("a transient no scheduled pass touches is not placed", "[render][graph]") {
    FakeDevice device;
    TransientPool pool(device);
    FakeTexture outTarget{64, 64, "out"};
    RenderGraph graph(pool);
    const GraphTexture scratch = graph.createTexture(kTransientColor, "lmx.transient.scratch");
    const GraphTexture out = graph.importTexture(outTarget, rhi::Format::BGRA8Unorm, "out");

    PassDesc dead;
    dead.color = ColorAttachment{.handle = scratch};
    graph.addPass("lmx.pass.dead", dead, kNoWork);

    PassDesc live;
    live.color = ColorAttachment{.handle = out};
    graph.addPass("lmx.pass.live", live, kNoWork);

    graph.exportTexture(nextVersion(out));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());

    REQUIRE(record->debug.transients.size() == 1);
    REQUIRE_FALSE(record->debug.transients[0].used);
    REQUIRE(record->debug.transients[0].size == 0);
    REQUIRE(record->debug.memory.requested == 0);
    REQUIRE(record->debug.memory.highWater == 0);
}

//======================================================================================================================
// What makes the picture the same with pooling on and off: a transient's bytes before its first
// write are whatever the previous occupant left, so consuming version 0 is refused rather than
// allowed to depend on the packing.
TEST_CASE("a transient consumed before it is written fails to compile", "[render][graph]") {
    FakeDevice device;
    TransientPool pool(device);
    FakeTexture outTarget{64, 64, "out"};
    RenderGraph graph(pool);
    const GraphTexture scratch = graph.createTexture(kTransientColor, "lmx.transient.scratch");
    const GraphTexture out = graph.importTexture(outTarget, rhi::Format::BGRA8Unorm, "out");

    PassDesc read;
    read.textureReads.push_back(scratch);
    read.color = ColorAttachment{.handle = out};
    graph.addPass("lmx.pass.read", read, kNoWork);

    graph.exportTexture(nextVersion(out));

    const auto record = graph.compileFrame(1);
    REQUIRE_FALSE(record.has_value());
    REQUIRE(record.error().message ==
            "pass 'lmx.pass.read' declares a read of transient texture 'lmx.transient.scratch' "
            "version 0, whose contents no pass produced: a transient holds nothing until a pass "
            "writes it");
}

//======================================================================================================================
// The same rule through the one declaration form that consumes a version while writing it: a loaded
// attachment keeps what is already there, and for a transient there is nothing to keep.
TEST_CASE("a transient loaded as an attachment fails to compile", "[render][graph][checkpoint-a]") {
    FakeDevice device;
    TransientPool pool(device);
    FakeTexture outTarget{64, 64, "out"};
    RenderGraph graph(pool);
    const GraphTexture scratch = graph.createTexture(kTransientColor, "lmx.transient.scratch");
    const GraphTexture out = graph.importTexture(outTarget, rhi::Format::BGRA8Unorm, "out");

    PassDesc load;
    load.color = ColorAttachment{.handle = scratch, .load = LoadOp::Load};
    graph.addPass("lmx.pass.load", load, kNoWork);

    PassDesc present;
    present.textureReads.push_back(nextVersion(scratch));
    present.color = ColorAttachment{.handle = out};
    graph.addPass("lmx.pass.present", present, kNoWork);

    graph.exportTexture(nextVersion(out));

    const auto record = graph.compileFrame(1);
    REQUIRE_FALSE(record.has_value());
    REQUIRE(record.error().message ==
            "pass 'lmx.pass.load' loads transient texture 'lmx.transient.scratch' version 0 as its "
            "color attachment, whose contents no pass produced: a transient holds nothing until a "
            "pass writes it");
}

//======================================================================================================================
// A transient lives for one frame, so every way of taking a result out of a frame refuses one --
// and each says so in the vocabulary of the sink that was declared.
TEST_CASE("a sink naming a transient fails to compile", "[render][graph]") {
    const std::array<std::pair<const char*, void (*)(RenderGraph&, GraphTexture)>, 3> sinks = {{
        {"exported", [](RenderGraph& graph, GraphTexture handle) { graph.exportTexture(handle); }},
        {"presented",
         [](RenderGraph& graph, GraphTexture handle) { graph.presentTexture(handle); }},
        {"read-back",
         [](RenderGraph& graph, GraphTexture handle) { graph.readbackTexture(handle); }},
    }};

    for (const auto& [verb, declareSink] : sinks) {
        FakeDevice device;
        TransientPool pool(device);
        RenderGraph graph(pool);
        const GraphTexture scratch = graph.createTexture(kTransientColor, "lmx.transient.scratch");

        PassDesc write;
        write.color = ColorAttachment{.handle = scratch};
        graph.addPass("lmx.pass.write", write, kNoWork);
        declareSink(graph, nextVersion(scratch));

        const auto record = graph.compileFrame(1);
        REQUIRE_FALSE(record.has_value());
        REQUIRE(record.error().message ==
                std::string(verb) +
                    " texture 'lmx.transient.scratch' is transient: a transient lives for exactly "
                    "one frame, so nothing outside that frame can read it -- import a resource of "
                    "your own for a result that has to survive");
    }
}

//======================================================================================================================
// The property the plan's determinism rests on: the layout comes from the declarations and the
// device's own sizing, so compiling one frame twice assigns the same bytes both times.
TEST_CASE("the same declarations plan the same layout", "[render][graph]") {
    DisjointFrame frame;
    frame.declare();

    const auto first = frame.graph.compileFrame(7);
    const auto second = frame.graph.compileFrame(7);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    REQUIRE(first->debug.transients.size() == second->debug.transients.size());
    for (size_t index = 0; index < first->debug.transients.size(); ++index) {
        const DebugTransient& a = first->debug.transients[index];
        const DebugTransient& b = second->debug.transients[index];
        REQUIRE(a.offset == b.offset);
        REQUIRE(a.size == b.size);
        REQUIRE(a.alignment == b.alignment);
        REQUIRE(a.aliases == b.aliases);
        REQUIRE(a.firstPass == b.firstPass);
        REQUIRE(a.lastPass == b.lastPass);
    }
    REQUIRE(first->debug.memory.highWater == second->debug.memory.highWater);
    REQUIRE(first->debug.memory.aliasSavings == second->debug.memory.aliasSavings);
}

//======================================================================================================================
// A transient buffer takes the buffer path through the same machinery: its own alignment, its own
// reuse boundary in the buffer vocabulary rather than the texture one.
TEST_CASE("transient buffers alias on the buffer path", "[render][graph]") {
    constexpr TransientBufferDesc kBins{.size = 1024, .storageRead = true, .storageWrite = true};

    FakeDevice device;
    TransientPool pool(device);
    FakeBuffer outBuffer{1024, "out"};
    RenderGraph graph(pool);
    const GraphBuffer first = graph.createBuffer(kBins, "lmx.transient.firstBins");
    const GraphBuffer second = graph.createBuffer(kBins, "lmx.transient.secondBins");
    const GraphBuffer out = graph.importBuffer(outBuffer, "out");

    ComputePassDesc writeFirst;
    writeFirst.bufferWrites.push_back(first);
    graph.addComputePass("lmx.pass.writeFirst", writeFirst, kNoWork);

    ComputePassDesc readFirst;
    readFirst.bufferReads.push_back(nextVersion(first));
    readFirst.bufferWrites.push_back(out);
    graph.addComputePass("lmx.pass.readFirst", readFirst, kNoWork);

    ComputePassDesc writeSecond;
    writeSecond.bufferWrites.push_back(second);
    graph.addComputePass("lmx.pass.writeSecond", writeSecond, kNoWork);

    ComputePassDesc readSecond;
    readSecond.bufferReads.push_back(nextVersion(second));
    readSecond.bufferWrites.push_back(nextVersion(out));
    graph.addComputePass("lmx.pass.readSecond", readSecond, kNoWork);

    graph.exportBuffer(GraphBuffer{out.index, 2});

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());

    REQUIRE(record->debug.transients[0].alignment == FakeDevice::kBufferAlignment);
    REQUIRE(record->debug.transients[1].aliases);
    REQUIRE(record->debug.transients[1].offset == record->debug.transients[0].offset);

    const auto reuse =
        std::ranges::find_if(record->debug.transitions, [](const DebugTransition& transition) {
            return transition.aliasedFrom.has_value();
        });
    REQUIRE(reuse != record->debug.transitions.end());
    REQUIRE(reuse->kind == GraphResourceKind::Buffer);
    REQUIRE(reuse->bufferFrom == rhi::BufferUse::StorageRead);
    REQUIRE(reuse->bufferTo == rhi::BufferUse::StorageWrite);
}

//======================================================================================================================
// Renderer::declarePasses() declares the exposure histogram/resolve chain and the bloom
// threshold/downsample/upsample chain every frame regardless of either feature's toggle (spec
// 9/10): what varies is whether anything reaches a sink. This mirrors that exact declaration
// shape -- pass kinds, labels, and use lists -- with both toggles off, and asserts every one of
// the six feature passes is culled while the three passes that are always live are not. It is the
// culling half of the "declare honestly, let the graph decide" pattern the exposure and bloom
// features exercise; Tests/GpuRendererTests.cpp's "pass timings name every pass the graph ran"
// case is the schedule's positive half, with bloom (the default-on feature) live.
TEST_CASE("exposure and bloom passes are culled when both features are off", "[render][graph]") {
    FakeDevice device;
    TransientPool pool(device);

    FakeTexture sceneColorTexture{64, 64, "sceneColor"};
    FakeTexture displayColorTexture{64, 64, "displayColor"};
    FakeBuffer histogramBufferFake{1024, "histogram"};
    FakeBuffer exposureBufferFake{4, "exposure"};

    RenderGraph graph(pool);
    const GraphTexture sceneColor = graph.importTexture(sceneColorTexture, rhi::Format::RGBA16Float,
                                                        "lmx.render.sceneColorHdr");
    const GraphTexture displayColor = graph.importTexture(
        displayColorTexture, rhi::Format::BGRA8Unorm, "lmx.render.displayColor");
    const GraphBuffer histogramBuffer =
        graph.importBuffer(histogramBufferFake, "lmx.render.histogramBuffer");
    const GraphBuffer exposureBuffer =
        graph.importBuffer(exposureBufferFake, "lmx.render.exposureBuffer");

    PassDesc scenePass;
    scenePass.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scenePass, kNoWork);
    const GraphTexture sceneColorRead = nextVersion(sceneColor);

    // Exposure chain: declared every frame, exported only when auto-exposure is on -- here it is
    // not, so nothing roots the resolve pass's write.
    CopyPassDesc clearDesc;
    clearDesc.bufferDestinations.push_back(histogramBuffer);
    graph.addCopyPass("lmx.pass.exposure.clearHistogram", clearDesc, kNoWork);
    const GraphBuffer histogramCleared = nextVersion(histogramBuffer);

    ComputePassDesc histogramDesc;
    histogramDesc.textureReads.push_back(sceneColorRead);
    histogramDesc.bufferWrites.push_back(histogramCleared);
    graph.addComputePass("lmx.pass.exposure.histogram", histogramDesc, kNoWork);
    const GraphBuffer histogramFinal = nextVersion(histogramCleared);

    ComputePassDesc resolveDesc;
    resolveDesc.bufferReads.push_back(histogramFinal);
    resolveDesc.bufferWrites.push_back(exposureBuffer);
    graph.addComputePass("lmx.pass.exposure.resolve", resolveDesc, kNoWork);
    // No graph.exportBuffer(...) here: auto-exposure is off.

    // Bloom chain: declared every frame, only the display pass's read of the result is
    // conditional -- here it is not declared, so nothing roots the upsample pass's write.
    const GraphTexture bloomChain = graph.createTexture({.width = 32,
                                                         .height = 32,
                                                         .format = rhi::Format::RGBA16Float,
                                                         .mipLevels = 2,
                                                         .storageRead = true,
                                                         .storageWrite = true},
                                                        "lmx.render.bloomChain");
    const GraphTexture bloomBlur = graph.createTexture({.width = 32,
                                                        .height = 32,
                                                        .format = rhi::Format::RGBA16Float,
                                                        .mipLevels = 1,
                                                        .storageRead = true,
                                                        .storageWrite = true},
                                                       "lmx.render.bloomBlur");
    constexpr rhi::TextureSubresourceRange kMip0{.baseMipLevel = 0, .mipLevelCount = 1};
    constexpr rhi::TextureSubresourceRange kMip1{.baseMipLevel = 1, .mipLevelCount = 1};

    ComputePassDesc thresholdDesc;
    thresholdDesc.textureReads.push_back(sceneColorRead);
    thresholdDesc.textureWrites.push_back(TextureUseDesc(bloomChain, kMip0));
    graph.addComputePass("lmx.pass.bloom.threshold", thresholdDesc, kNoWork);
    const GraphTexture afterThreshold = nextVersion(bloomChain);

    ComputePassDesc downsampleDesc;
    downsampleDesc.textureReads.push_back(TextureUseDesc(afterThreshold, kMip0));
    downsampleDesc.textureWrites.push_back(TextureUseDesc(afterThreshold, kMip1));
    graph.addComputePass("lmx.pass.bloom.downsample", downsampleDesc, kNoWork);
    const GraphTexture chainFinal = nextVersion(afterThreshold);

    ComputePassDesc upsampleDesc;
    upsampleDesc.textureReads.push_back(TextureUseDesc(chainFinal, kMip0));
    upsampleDesc.textureReads.push_back(TextureUseDesc(chainFinal, kMip1));
    upsampleDesc.textureWrites.push_back(bloomBlur);
    graph.addComputePass("lmx.pass.bloom.upsample", upsampleDesc, kNoWork);
    // bloomResult = nextVersion(bloomBlur) is declared by nothing below: bloom is off.

    PassDesc displayPass;
    displayPass.textureReads.push_back(sceneColorRead);
    displayPass.color = ColorAttachment{.handle = displayColor};
    graph.addPass("lmx.pass.display", displayPass, kNoWork);
    graph.exportTexture(nextVersion(displayColor));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());

    const auto cullReasonOf = [&](std::string_view label) -> std::optional<CullReason> {
        const auto found = std::ranges::find_if(
            record->debug.passes, [&](const DebugPass& pass) { return pass.label == label; });
        REQUIRE(found != record->debug.passes.end());
        return found->cullReason;
    };

    for (std::string_view label :
         {"lmx.pass.exposure.clearHistogram", "lmx.pass.exposure.histogram",
          "lmx.pass.exposure.resolve", "lmx.pass.bloom.threshold", "lmx.pass.bloom.downsample",
          "lmx.pass.bloom.upsample"}) {
        INFO("pass: " << label);
        const std::optional<CullReason> reason = cullReasonOf(label);
        REQUIRE(reason.has_value());
        REQUIRE(*reason == CullReason::NoSinkReachesIt);
    }
    REQUIRE_FALSE(cullReasonOf("lmx.pass.scene").has_value());
    REQUIRE_FALSE(cullReasonOf("lmx.pass.display").has_value());
}
