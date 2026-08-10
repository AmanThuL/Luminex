#include <catch2/catch_test_macros.hpp>

#include "Render/RenderGraph.h"

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
// transition is not one barrier per texture.
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
                "begin lmx.pass.rewrite", "end", "barrier pingPong RenderTarget->ShaderRead",
                "begin lmx.pass.resample", "end"});
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
