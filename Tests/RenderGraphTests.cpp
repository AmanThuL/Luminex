#include <catch2/catch_test_macros.hpp>

#include "Render/RenderGraph.h"

#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::render;

namespace {
// Texture is an interface, and the graph reads nothing from it but width()/height(): the extent is
// the only texture property the attachment rules inspect, since the format is declared at import.
// readback() is never reached, so it is left empty rather than faked.
//
// `name` is the test's own label for the texture, so a recorded barrier says which resource it
// transitioned rather than printing a pointer.
struct FakeTexture final : rhi::Texture {
    std::string name;

    //==================================================================================================================
    FakeTexture(uint32_t width, uint32_t height, std::string label = {})
        : name(std::move(label)), m_width(width), m_height(height) {}

    //==================================================================================================================
    uint32_t width() const override { return m_width; }

    //==================================================================================================================
    uint32_t height() const override { return m_height; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

// The graph never reads a buffer's size either -- buffers carry no attachment rules at all -- so
// this exists only to give importBuffer a real object to borrow.
struct FakeBuffer final : rhi::Buffer {

    //==================================================================================================================
    explicit FakeBuffer(uint64_t size) : m_size(size) {}

    //==================================================================================================================
    uint64_t size() const override { return m_size; }

private:
    uint64_t m_size = 0;
};

// Passes here are declarations and nothing else: this layer stores the body without running it, so
// every pass gets the same empty one.
const ExecuteFn kNoWork = [](const PassResources&) {};

//======================================================================================================================
std::string useName(rhi::TextureUse use) {
    return use == rhi::TextureUse::RenderTarget ? "RenderTarget" : "ShaderRead";
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

    //==================================================================================================================
    void textureBarrier(rhi::Texture& texture, rhi::TextureUse from, rhi::TextureUse to) override {
        events.push_back("barrier " + static_cast<FakeTexture&>(texture).name + " " +
                         useName(from) + "->" + useName(to));
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

    const auto schedule = graph.compile();
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

    const auto schedule = graph.compile();
    REQUIRE(schedule.has_value());
    REQUIRE(schedule->passes == std::vector<uint32_t>{0, 1});
}

//======================================================================================================================
// Buffers carry the same version chain as textures, so a buffer write orders its reader too.
TEST_CASE("a buffer write orders the pass that reads its result", "[render][graph]") {
    FakeBuffer storage{256};
    RenderGraph graph;
    const GraphBuffer buffer = graph.importBuffer(storage, "instances");

    PassDesc consumer;
    consumer.bufferReads.push_back(nextVersion(buffer));
    graph.addPass("lmx.pass.consumer", consumer, kNoWork);

    PassDesc producer;
    producer.bufferWrites.push_back(buffer);
    graph.addPass("lmx.pass.producer", producer, kNoWork);

    const auto schedule = graph.compile();
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

    graph.execute(commands);

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

    RecordingCommandList commands;
    graph.execute(commands);

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
    graph.execute(commands);

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

    RecordingCommandList commands;
    graph.execute(commands);

    REQUIRE(commands.events ==
            std::vector<std::string>{
                "begin lmx.pass.write", "end", "barrier pingPong RenderTarget->ShaderRead",
                "begin lmx.pass.sample", "end", "barrier other RenderTarget->ShaderRead",
                "begin lmx.pass.rewrite", "end", "barrier pingPong RenderTarget->ShaderRead",
                "begin lmx.pass.resample", "end"});
}
