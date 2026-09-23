#include "Support/RenderGraphTestSupport.h"

//======================================================================================================================
TEST_CASE("an imported texture enters the graph at version 0", "[render][graph]") {
    FakeTexture color{64, 64};
    RenderGraph graph;

    const GraphTexture handle =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

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
    const GraphTexture shadow =
        graph.importTexture(shadowMap, rojoRHI::Format::D32Float, "shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(shadow));
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Store};
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
    const GraphTexture a = graph.importTexture(first, rojoRHI::Format::BGRA8Unorm, "first");
    const GraphTexture b = graph.importTexture(second, rojoRHI::Format::BGRA8Unorm, "second");

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
    const GraphTexture shadow =
        graph.importTexture(shadowMap, rojoRHI::Format::D32Float, "shadowMap");

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
// Discard records a write for version-chain validation, but its resulting contents do not exist.
// Neither a later pass nor an external sink may turn an attachment the GPU threw away into a
// producer.
TEST_CASE("a discarded attachment cannot be consumed", "[render][graph]") {
    FakeTexture shadowMap{1024, 1024};
    FakeTexture output{64, 64};

    SECTION("later pass") {
        RenderGraph graph;
        const GraphTexture shadow =
            graph.importTexture(shadowMap, rojoRHI::Format::D32Float, "shadowMap");
        const GraphTexture color =
            graph.importTexture(output, rojoRHI::Format::BGRA8Unorm, "output");

        PassDesc shadowPass;
        shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Discard};
        graph.addPass("lmx.pass.shadow", shadowPass, kNoWork);

        PassDesc sample;
        sample.textureReads.push_back(nextVersion(shadow));
        sample.color = ColorAttachment{.handle = color};
        graph.addPass("lmx.pass.sampleDiscarded", sample, kNoWork);
        graph.exportTexture(nextVersion(color));

        const auto schedule = graph.compile();
        REQUIRE_FALSE(schedule.has_value());
        REQUIRE(schedule.error().message.contains("lmx.pass.sampleDiscarded"));
        REQUIRE(schedule.error().message.contains("no pass writes"));
    }

    SECTION("external sink") {
        RenderGraph graph;
        const GraphTexture shadow =
            graph.importTexture(shadowMap, rojoRHI::Format::D32Float, "shadowMap");
        PassDesc shadowPass;
        shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Discard};
        graph.addPass("lmx.pass.shadow", shadowPass, kNoWork);
        graph.exportTexture(nextVersion(shadow));

        const auto schedule = graph.compile();
        REQUIRE_FALSE(schedule.has_value());
        REQUIRE(schedule.error().message.contains("shadowMap"));
        REQUIRE(schedule.error().message.contains("not written by any pass"));
    }
}

//======================================================================================================================
// Every version here is produced, so this is not a missing-producer failure: each pass consumes
// what the other one makes, and no serial order satisfies both.
TEST_CASE("mutually dependent passes are rejected as a cycle", "[render][graph]") {
    FakeTexture first{64, 64};
    FakeTexture second{64, 64};
    RenderGraph graph;
    const GraphTexture a = graph.importTexture(first, rojoRHI::Format::BGRA8Unorm, "first");
    const GraphTexture b = graph.importTexture(second, rojoRHI::Format::BGRA8Unorm, "second");

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
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture sceneDepth =
        graph.importTexture(depth, rojoRHI::Format::D32Float, "sceneDepth");

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
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    const GraphTexture sceneDepth =
        graph.importTexture(depth, rojoRHI::Format::D32Float, "sceneDepth");

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
    const GraphTexture shadow =
        graph.importTexture(shadowMap, rojoRHI::Format::D32Float, "shadowMap");

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
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

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
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

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
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

    graph.exportTexture(sceneColor);

    const auto imported = graph.compile();
    REQUIRE_FALSE(imported.has_value());
    REQUIRE(imported.error().message.contains("sceneColor"));
    REQUIRE(imported.error().message.contains("not written by any pass"));

    RenderGraph beyond;
    const GraphTexture beyondColor =
        beyond.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");
    beyond.exportTexture(nextVersion(beyondColor));
    REQUIRE_FALSE(beyond.compile().has_value());
}

//======================================================================================================================
TEST_CASE("exporting a version a pass wrote is accepted", "[render][graph]") {
    FakeTexture color{64, 64};
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

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
    const GraphTexture shadow =
        graph.importTexture(shadowMap, rojoRHI::Format::D32Float, "shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

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
    const GraphTexture shadow =
        graph.importTexture(shadowMap, rojoRHI::Format::D32Float, "shadowMap");
    const GraphTexture sceneColor =
        graph.importTexture(color, rojoRHI::Format::BGRA8Unorm, "sceneColor");

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
// The bloom step's shape: one pass reads mip 1 and writes mip 2 of the same chain. Under
// whole-resource versions the write still produces the next version of the whole texture, and mip 1
// carries forward into it untouched.
TEST_CASE("a compute pass reads and writes disjoint mips of one texture", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "bloom");

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
    const GraphTexture bloom = graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "bloom");

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
    const GraphTexture bloom = graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "bloom");

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
    const GraphTexture faces = graph.importTexture(cube, rojoRHI::Format::RGBA16Float, "faces");

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
    const GraphTexture bloom = graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "bloom");

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
// Bounds checks use subtraction rather than base + count, so an attacker-sized count cannot wrap
// its last mip back into a small, apparently valid index.
TEST_CASE("an overflowing subresource range is rejected", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "bloom");

    ComputePassDesc overflow;
    overflow.textureWrites.push_back(
        {bloom, {.baseMipLevel = std::numeric_limits<uint32_t>::max() - 1, .mipLevelCount = 4}});
    graph.addComputePass("lmx.pass.overflow", overflow, kNoWork);

    const auto schedule = graph.compile();
    REQUIRE_FALSE(schedule.has_value());
    REQUIRE(schedule.error().message.contains("lmx.pass.overflow"));
    REQUIRE(schedule.error().message.contains("runs past"));
}

//======================================================================================================================
// A zero count is not "the whole resource", it is nothing at all -- a binding addressing no
// subresource is a declaration that says the pass touches something while touching nothing.
TEST_CASE("an empty subresource range is rejected", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 3};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "bloom");

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
    const GraphTexture bloom = graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "bloom");

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
    const GraphTexture bloom = graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "bloom");
    const GraphTexture target = graph.importTexture(output, rojoRHI::Format::BGRA8Unorm, "output");

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
