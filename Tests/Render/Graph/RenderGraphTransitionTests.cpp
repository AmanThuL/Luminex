#include "Support/RenderGraphTestSupport.h"
#include "Support/TestGpu.h"

//======================================================================================================================
// The whole encoding contract in one frame's shape: the declared order is the reverse of the
// scheduled one, each pass becomes one labelled render pass carrying its own attachments, the body
// runs inside it, and the shadow map's render-target-to-sampled transition lands between the two.
TEST_CASE("execute encodes the schedule as labelled render passes", "[render][graph][device]") {
    auto gpu = createTestGpu();
    auto shadowMap = createTestTexture(
        *gpu, rojoRHI::Format::D32Float,
        rojoRHI::TextureUsage::DepthStencil | rojoRHI::TextureUsage::Sampled, 1024, 1024);
    auto color =
        createTestTexture(*gpu, rojoRHI::Format::BGRA8Unorm, rojoRHI::TextureUsage::RenderTarget);
    auto depth =
        createTestTexture(*gpu, rojoRHI::Format::D32Float, rojoRHI::TextureUsage::DepthStencil);
    RenderGraph graph;
    const GraphTexture shadow = graph.importTexture(shadowMap.ref(), "shadowMap");
    const GraphTexture sceneColor = graph.importTexture(color.ref(), "sceneColor");
    const GraphTexture sceneDepth = graph.importTexture(depth.ref(), "sceneDepth");

    std::vector<std::string> bodies;
    rojoRHI::Texture resolvedShadow;

    PassDesc scene;
    scene.textureReads.push_back(nextVersion(shadow));
    scene.color = ColorAttachment{.handle = sceneColor, .clearColor = {0.05f, 0.07f, 0.10f, 1.0f}};
    scene.depth = DepthAttachment{.handle = sceneDepth};
    graph.addPass("lmx.pass.scene", scene, [&](const PassResources& resources) {
        bodies.push_back("body scene");
        const auto texture = resources.texture(nextVersion(shadow));
        resolvedShadow = texture.texture;
    });

    PassDesc shadowPass;
    shadowPass.depth = DepthAttachment{.handle = shadow, .store = StoreOp::Store};
    graph.addPass("lmx.pass.shadow", shadowPass,
                  [&bodies](const PassResources&) { bodies.push_back("body shadow"); });

    graph.exportTexture(nextVersion(sceneColor));

    const auto snapshot = lowered(graph);
    REQUIRE(passEvents(snapshot) ==
            std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.shadow",
                                     "run lmx.pass.shadow", "end", "begin lmx.pass.scene",
                                     "run lmx.pass.scene", "end", "end scope"});
    REQUIRE(logicalTransitions(snapshot.record) ==
            std::vector<std::string>{"barrier shadowMap RenderTarget->ShaderRead"});

    auto recording = gpu->beginFrame();
    graph.execute(recording.commands, recording.number);
    gpu->submitAndEndFrame(recording);
    gpu->waitIdle();
    REQUIRE(bodies == std::vector<std::string>{"body shadow", "body scene"});
    REQUIRE(resolvedShadow == shadowMap.rhi());
    const auto passes = renderPasses(snapshot);
    REQUIRE(passes.size() == 2);
    REQUIRE_FALSE(passes[0].color.has_value());
    REQUIRE(snapshot.resources[passes[0].depth->resource].texture.texture == shadowMap.rhi());
    REQUIRE(passes[0].depth->store == StoreOp::Store);
    REQUIRE(passes[0].depth->clearDepth == 0.0f);
    REQUIRE(snapshot.resources[passes[1].color->resource].texture.texture == color.rhi());
    REQUIRE(snapshot.resources[passes[1].depth->resource].texture.texture == depth.rhi());
    REQUIRE(passes[1].color->load == LoadOp::Clear);
    REQUIRE(passes[1].depth->store == StoreOp::Discard);
    REQUIRE(passes[1].color->clearColor == std::array<float, 4>{0.05f, 0.07f, 0.10f, 1.0f});
    const auto barriers = textureBarriers(snapshot);
    REQUIRE(barriers.size() == 4);
    const auto read = std::ranges::find_if(barriers, [&](const auto& barrier) {
        return barrier.resource == shadow.index &&
               barrier.to.layout == rojoRHI::Layout::DepthStencilRead;
    });
    REQUIRE(read != barriers.end());
    REQUIRE(read->from.access ==
            (rojoRHI::Access::DepthStencilRead | rojoRHI::Access::DepthStencilWrite));
    REQUIRE(read->to.access == rojoRHI::Access::ShaderRead);
}

//======================================================================================================================
// The barrier is a state transition of the texture, not a property of one read: the second reader
// finds the shadow map already readable, so a second barrier would be pure cost.
TEST_CASE("execute transitions a sampled render target once", "[render][graph]") {
    auto shadowMap = textureFixture(1024, 1024, "shadowMap");
    auto firstColor = textureFixture(64, 64, "first");
    auto secondColor = textureFixture(64, 64, "second");
    RenderGraph graph;
    const GraphTexture shadow =
        graph.importTexture(formatted(shadowMap, rojoRHI::Format::D32Float), "shadowMap");
    const GraphTexture first =
        graph.importTexture(formatted(firstColor, rojoRHI::Format::BGRA8Unorm), "first");
    const GraphTexture second =
        graph.importTexture(formatted(secondColor, rojoRHI::Format::BGRA8Unorm), "second");

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

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.shadow",
                                     "run lmx.pass.shadow", "end", "begin lmx.pass.first",
                                     "run lmx.pass.first", "end", "begin lmx.pass.second",
                                     "run lmx.pass.second", "end", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier shadowMap RenderTarget->ShaderRead"});
}

//======================================================================================================================
// Exporting roots a result for the caller to read once the queue drains, which is not a shader read
// by another pass -- so it is not what the barrier is for.
TEST_CASE("export adds no barrier after the texture's producing pass", "[render][graph]") {
    auto color = textureFixture(64, 64, "sceneColor");
    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(formatted(color, rojoRHI::Format::BGRA8Unorm), "sceneColor");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) == std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.scene",
                                                          "run lmx.pass.scene", "end",
                                                          "end scope"});
    REQUIRE(logicalTransitions(frame.record) == std::vector<std::string>{});
    const auto barriers = textureBarriers(frame);
    REQUIRE(barriers.size() == 1);
    REQUIRE(barriers.front().from.layout == rojoRHI::Layout::Undefined);
    REQUIRE(barriers.front().to.layout == rojoRHI::Layout::ColorAttachment);
    const auto begin = std::ranges::find_if(
        frame.ops, [](const auto& op) { return std::holds_alternative<BeginRenderPassOp>(op); });
    REQUIRE(begin != frame.ops.end());
    REQUIRE(begin != frame.ops.begin());
    REQUIRE(std::holds_alternative<BarriersOp>(*std::prev(begin)));
}

//======================================================================================================================
// A pass that renders into a texture it earlier sampled needs the transition again: one barrier per
// transition is not one barrier per texture. It also needs a barrier *before* that pass, ordering
// its write after the earlier read: "pingPong" was left in ShaderRead by "sample", so "rewrite"
// owes a write-after-read barrier before it overwrites the texture, and "other" symmetrically owes
// one before "resample" for the same reason.
TEST_CASE("execute transitions a render target again after it is rewritten", "[render][graph]") {
    auto pingPong = textureFixture(64, 64, "pingPong");
    auto other = textureFixture(64, 64, "other");
    RenderGraph graph;
    const GraphTexture target =
        graph.importTexture(formatted(pingPong, rojoRHI::Format::BGRA8Unorm), "pingPong");
    const GraphTexture scratch =
        graph.importTexture(formatted(other, rojoRHI::Format::BGRA8Unorm), "other");

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

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.write",
                                     "run lmx.pass.write", "end", "begin lmx.pass.sample",
                                     "run lmx.pass.sample", "end", "begin lmx.pass.rewrite",
                                     "run lmx.pass.rewrite", "end", "begin lmx.pass.resample",
                                     "run lmx.pass.resample", "end", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier pingPong RenderTarget->ShaderRead",
                                     "barrier other RenderTarget->ShaderRead",
                                     "barrier pingPong RenderTarget->RenderTarget",
                                     "barrier pingPong ShaderRead->RenderTarget",
                                     "barrier pingPong RenderTarget->ShaderRead",
                                     "barrier other RenderTarget->RenderTarget",
                                     "barrier other ShaderRead->RenderTarget"});
}

//======================================================================================================================
// The exposure-buffer shape finding 1 named: one pass reads a buffer version and a later pass
// writes the next version of it, with no other resource ordering them transitively. Without a
// write-after-read barrier the write could retire before the read that depends on the prior
// contents does; deriveTransitions now owes one before the writing pass.
TEST_CASE("execute barriers a buffer write after an earlier read", "[render][graph]") {
    auto exposureBuffer = bufferFixture(16, "exposure");
    auto sceneColorBuffer = bufferFixture(16, "sceneColor");
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

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "scope lmx.pass.scene",
                                     "run lmx.pass.scene", "end scope", "scope lmx.pass.resolve",
                                     "run lmx.pass.resolve", "end scope", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier exposure StorageRead->StorageWrite"});
}

//======================================================================================================================
// Ledger L60, on the buffer path: the exposure buffer is read by the scene pass through a sampled
// binding and by the histogram dispatch through a storage one, and only then overwritten. A single
// remembered reader would name whichever read last, leaving the other unordered against the write;
// the write owes a barrier from *each* distinct reading use, in the order the reads happened.
TEST_CASE("a buffer write is barriered after every kind of earlier read", "[render][graph]") {
    auto exposureBuffer = bufferFixture(16, "exposure");
    auto sceneColorTarget = textureFixture(64, 64, "sceneColor");
    auto histogramBuffer = bufferFixture(1024, "histogram");
    RenderGraph graph;
    const GraphBuffer exposure = graph.importBuffer(exposureBuffer, "exposure");
    const GraphTexture sceneColor = graph.importTexture(
        formatted(sceneColorTarget, rojoRHI::Format::RGBA16Float), "sceneColor");
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

    const LoweredFrame frame = lowered(graph);
    const CompiledFrameRecord& record = frame.record;

    // Two write-after-read transitions before the resolve, one per reading use.
    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].bufferFrom == lmx::render::BufferUse::ShaderRead);
    REQUIRE(record.debug.transitions[1].bufferFrom == lmx::render::BufferUse::StorageRead);
    REQUIRE(record.debug.transitions[0].beforePass == 2);
    REQUIRE(record.debug.transitions[1].beforePass == 2);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{
                "scope lmx.frame", "begin lmx.pass.scene", "run lmx.pass.scene", "end",
                "scope lmx.pass.histogram", "run lmx.pass.histogram", "end scope",
                "scope lmx.pass.resolve", "run lmx.pass.resolve", "end scope", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier exposure ShaderRead->StorageWrite",
                                     "barrier exposure StorageRead->StorageWrite"});
}

//======================================================================================================================
// The same rule on the texture path, where the pending reads carry subresource ranges: each range a
// write overlaps keeps the use that read it, so a write over both of them names both.
TEST_CASE("a texture write is barriered after every kind of earlier read", "[render][graph]") {
    auto sceneColorTarget = textureFixture(64, 64, "sceneColor");
    auto histogramBuffer = bufferFixture(1024, "histogram");
    auto displayTarget = textureFixture(64, 64, "display");
    RenderGraph graph;
    const GraphTexture sceneColor = graph.importTexture(
        formatted(sceneColorTarget, rojoRHI::Format::RGBA16Float), "sceneColor");
    const GraphBuffer histogram = graph.importBuffer(histogramBuffer, "histogram");
    const GraphTexture display =
        graph.importTexture(formatted(displayTarget, rojoRHI::Format::BGRA8Unorm), "display");

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

    const LoweredFrame frame = lowered(graph);
    const CompiledFrameRecord& record = frame.record;

    // Two read-after-write transitions (one per reading stage class), the write-after-write edge
    // from the original render target producer, then two write-after-read edges from the readers.
    REQUIRE(record.debug.transitions.size() == 5);
    REQUIRE(record.debug.transitions[2].beforePass == 3);
    REQUIRE(record.debug.transitions[2].textureFrom == lmx::render::TextureUse::RenderTarget);
    REQUIRE(record.debug.transitions[3].beforePass == 3);
    REQUIRE(record.debug.transitions[3].textureFrom == lmx::render::TextureUse::StorageRead);
    REQUIRE(record.debug.transitions[4].beforePass == 3);
    REQUIRE(record.debug.transitions[4].textureFrom == lmx::render::TextureUse::ShaderRead);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{
                "scope lmx.frame", "begin lmx.pass.scene", "run lmx.pass.scene", "end",
                "scope lmx.pass.histogram", "run lmx.pass.histogram", "end scope",
                "begin lmx.pass.display", "run lmx.pass.display", "end", "scope lmx.pass.overwrite",
                "run lmx.pass.overwrite", "end scope", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier sceneColor RenderTarget->StorageRead",
                                     "barrier sceneColor RenderTarget->ShaderRead",
                                     "barrier sceneColor RenderTarget->StorageWrite",
                                     "barrier sceneColor StorageRead->StorageWrite",
                                     "barrier sceneColor ShaderRead->StorageWrite"});
}

//======================================================================================================================
// A pass reading and writing one buffer in the same dispatch (the histogram accumulate shape) must
// not be barriered against itself: the read this pass records is not visible to its own write
// check, only to a later pass's.
TEST_CASE("a pass reading and writing one buffer owes itself no barrier", "[render][graph]") {
    auto histogram = bufferFixture(1024, "histogram");
    RenderGraph graph;
    const GraphBuffer bins = graph.importBuffer(histogram, "histogram");

    ComputePassDesc accumulate;
    accumulate.bufferReads.push_back(bins);
    accumulate.bufferWrites.push_back(bins);
    graph.addComputePass("lmx.pass.accumulate", accumulate, kNoWork);
    graph.exportBuffer(nextVersion(bins));

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "scope lmx.pass.accumulate",
                                     "run lmx.pass.accumulate", "end scope", "end scope"});
    REQUIRE(logicalTransitions(frame.record) == std::vector<std::string>{});
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
    auto chain = textureFixture(64, 64, "chain", 4);
    auto other = textureFixture(64, 64, "other");
    RenderGraph graph;
    const GraphTexture bloom =
        graph.importTexture(formatted(chain, rojoRHI::Format::RGBA16Float), "bloom");
    const GraphTexture sink =
        graph.importTexture(formatted(other, rojoRHI::Format::BGRA8Unorm), "other");

    static constexpr rojoRHI::SubresourceRange kMip0{.baseMip = 0, .mipCount = 1};
    static constexpr rojoRHI::SubresourceRange kMip3{.baseMip = 3, .mipCount = 1};

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

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.a", "run lmx.pass.a", "end",
                                     "scope lmx.pass.b", "run lmx.pass.b", "end scope",
                                     "scope lmx.pass.c", "run lmx.pass.c", "end scope",
                                     "end scope"});
    REQUIRE(
        logicalTransitions(frame.record) ==
        std::vector<std::string>{"barrier bloom mips[0..0] layers[0..] ShaderRead->StorageWrite"});
}

//======================================================================================================================
// A write-after-read discharge must shrink a pending read to what the write did not touch, not drop
// the whole entry the moment any part of it overlaps: A reads mips 0-3 in one declaration, D writes
// mip 0 alone (overlaps, so it owes a barrier -- and the read shrinks to mips 1-3, not to nothing),
// then E writes mip 2 and must still be barriered, because A's read of mip 2 was never discharged
// -- only mip 0 was. Erasing the whole 0-3 entry on D's overlap (rather than subtracting mip 0 from
// it) would leave E wrongly finding nothing owed.
TEST_CASE("a write-after-read barrier survives a partial-overlap discharge", "[render][graph]") {
    auto chain = textureFixture(64, 64, "chain", 4);
    auto other = textureFixture(64, 64, "other");
    RenderGraph graph;
    const GraphTexture bloom =
        graph.importTexture(formatted(chain, rojoRHI::Format::RGBA16Float), "bloom");
    const GraphTexture sink =
        graph.importTexture(formatted(other, rojoRHI::Format::BGRA8Unorm), "other");

    static constexpr rojoRHI::SubresourceRange kMips0to3{.baseMip = 0, .mipCount = 4};
    static constexpr rojoRHI::SubresourceRange kMip0{.baseMip = 0, .mipCount = 1};
    static constexpr rojoRHI::SubresourceRange kMip2{.baseMip = 2, .mipCount = 1};

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

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.a", "run lmx.pass.a", "end",
                                     "scope lmx.pass.d", "run lmx.pass.d", "end scope",
                                     "scope lmx.pass.e", "run lmx.pass.e", "end scope",
                                     "end scope"});
    REQUIRE(
        logicalTransitions(frame.record) ==
        std::vector<std::string>{"barrier bloom mips[0..0] layers[0..] ShaderRead->StorageWrite",
                                 "barrier bloom mips[2..2] layers[0..] ShaderRead->StorageWrite"});
}

//======================================================================================================================
// The companion to the case above: when a write fully covers a pending read (rather than only part
// of it), the read is completely discharged and a later write of a different, disjoint range owes
// nothing -- confirming the subtraction in the case above does not manufacture a spurious barrier
// when there is truly nothing left pending.
TEST_CASE("a fully covered read is completely discharged", "[render][graph]") {
    auto chain = textureFixture(64, 64, "chain", 4);
    auto other = textureFixture(64, 64, "other");
    RenderGraph graph;
    const GraphTexture bloom =
        graph.importTexture(formatted(chain, rojoRHI::Format::RGBA16Float), "bloom");
    const GraphTexture sink =
        graph.importTexture(formatted(other, rojoRHI::Format::BGRA8Unorm), "other");

    static constexpr rojoRHI::SubresourceRange kMip0{.baseMip = 0, .mipCount = 1};
    static constexpr rojoRHI::SubresourceRange kMip1{.baseMip = 1, .mipCount = 1};

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

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.a", "run lmx.pass.a", "end",
                                     "scope lmx.pass.d", "run lmx.pass.d", "end scope",
                                     "scope lmx.pass.e", "run lmx.pass.e", "end scope",
                                     "end scope"});
    REQUIRE(
        logicalTransitions(frame.record) ==
        std::vector<std::string>{"barrier bloom mips[0..0] layers[0..] ShaderRead->StorageWrite"});
}

//======================================================================================================================
// A barrier orders the passes it sits between, so the range it names has to cover the reader it
// sits in front of -- one of the two axes a barrier is scoped on
// (rojoRHI::CommandList::textureBarrier states the model; the consuming stage class is the other,
// two cases below). One writer of the whole chain and two readers of different mips is the case
// that tells the two range rules apart: "one transition per write" would leave the second reader
// unordered, and only the second reader's own barrier states the dependency it actually has.
TEST_CASE("each reader of a distinct range gets its own transition", "[render][graph]") {
    auto chain = textureFixture(64, 64, "chain", 4);
    auto first = textureFixture(64, 64, "first");
    auto second = textureFixture(64, 64, "second");
    RenderGraph graph;
    const GraphTexture bloom =
        graph.importTexture(formatted(chain, rojoRHI::Format::RGBA16Float), "bloom");
    const GraphTexture a =
        graph.importTexture(formatted(first, rojoRHI::Format::BGRA8Unorm), "first");
    const GraphTexture b =
        graph.importTexture(formatted(second, rojoRHI::Format::BGRA8Unorm), "second");

    ComputePassDesc write;
    write.textureWrites.push_back(bloom);
    graph.addComputePass("lmx.pass.write", write, kNoWork);

    PassDesc readMip0;
    readMip0.textureReads.push_back({nextVersion(bloom), {.baseMip = 0, .mipCount = 1}});
    readMip0.color = ColorAttachment{.handle = a};
    graph.addPass("lmx.pass.readMip0", readMip0, kNoWork);

    PassDesc readMip1;
    readMip1.textureReads.push_back({nextVersion(bloom), {.baseMip = 1, .mipCount = 1}});
    readMip1.color = ColorAttachment{.handle = b};
    graph.addPass("lmx.pass.readMip1", readMip1, kNoWork);

    graph.exportTexture(nextVersion(a));
    graph.exportTexture(nextVersion(b));

    const LoweredFrame frame = lowered(graph);
    const CompiledFrameRecord& record = frame.record;

    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].beforePass == 1);
    REQUIRE(record.debug.transitions[0].range.baseMip == 0);
    REQUIRE(record.debug.transitions[1].beforePass == 2);
    REQUIRE(record.debug.transitions[1].range.baseMip == 1);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "scope lmx.pass.write",
                                     "run lmx.pass.write", "end scope", "begin lmx.pass.readMip0",
                                     "run lmx.pass.readMip0", "end", "begin lmx.pass.readMip1",
                                     "run lmx.pass.readMip1", "end", "end scope"});
    REQUIRE(
        logicalTransitions(frame.record) ==
        std::vector<std::string>{"barrier bloom mips[0..0] layers[0..] StorageWrite->ShaderRead",
                                 "barrier bloom mips[1..1] layers[0..] StorageWrite->ShaderRead"});
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
    auto chain = textureFixture(64, 64, "chain", 4);
    auto first = textureFixture(64, 64, "first");
    auto second = textureFixture(64, 64, "second");
    RenderGraph graph;
    const GraphTexture bloom =
        graph.importTexture(formatted(chain, rojoRHI::Format::RGBA16Float), "bloom");
    const GraphTexture a =
        graph.importTexture(formatted(first, rojoRHI::Format::BGRA8Unorm), "first");
    const GraphTexture b =
        graph.importTexture(formatted(second, rojoRHI::Format::BGRA8Unorm), "second");

    ComputePassDesc write;
    write.textureWrites.push_back(bloom);
    graph.addComputePass("lmx.pass.write", write, kNoWork);

    PassDesc readAll;
    readAll.textureReads.push_back(nextVersion(bloom));
    readAll.color = ColorAttachment{.handle = a};
    graph.addPass("lmx.pass.readAll", readAll, kNoWork);

    PassDesc readMip2;
    readMip2.textureReads.push_back({nextVersion(bloom), {.baseMip = 2, .mipCount = 1}});
    readMip2.color = ColorAttachment{.handle = b};
    graph.addPass("lmx.pass.readMip2", readMip2, kNoWork);

    graph.exportTexture(nextVersion(a));
    graph.exportTexture(nextVersion(b));

    const LoweredFrame frame = lowered(graph);
    const CompiledFrameRecord& record = frame.record;

    REQUIRE(record.debug.transitions.size() == 1);
    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "scope lmx.pass.write",
                                     "run lmx.pass.write", "end scope", "begin lmx.pass.readAll",
                                     "run lmx.pass.readAll", "end", "begin lmx.pass.readMip2",
                                     "run lmx.pass.readMip2", "end", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier bloom StorageWrite->ShaderRead"});
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
    auto sceneColorTarget = textureFixture(64, 64, "sceneColor");
    auto histogramBuffer = bufferFixture(1024, "histogram");
    auto displayTarget = textureFixture(64, 64, "display");
    RenderGraph graph;
    const GraphTexture sceneColor = graph.importTexture(
        formatted(sceneColorTarget, rojoRHI::Format::RGBA16Float), "sceneColor");
    const GraphBuffer histogram = graph.importBuffer(histogramBuffer, "histogram");
    const GraphTexture display =
        graph.importTexture(formatted(displayTarget, rojoRHI::Format::BGRA8Unorm), "display");

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

    const LoweredFrame frame = lowered(graph);
    const CompiledFrameRecord& record = frame.record;

    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].beforePass == 1);
    REQUIRE(record.debug.transitions[0].textureTo == lmx::render::TextureUse::StorageRead);
    REQUIRE(record.debug.transitions[1].beforePass == 2);
    REQUIRE(record.debug.transitions[1].textureTo == lmx::render::TextureUse::ShaderRead);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{
                "scope lmx.frame", "begin lmx.pass.scene", "run lmx.pass.scene", "end",
                "scope lmx.pass.histogram", "run lmx.pass.histogram", "end scope",
                "begin lmx.pass.display", "run lmx.pass.display", "end", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier sceneColor RenderTarget->StorageRead",
                                     "barrier sceneColor RenderTarget->ShaderRead"});
}

//======================================================================================================================
// The same rule on the buffer path, which has no ranges to fall back on: a buffer's first barrier
// covers every byte, so the stage class is the only thing that can tell two readers apart. The
// exposure buffer's seeded value is read by the scene raster pass and then by the histogram
// dispatch, and each owes a barrier of its own.
TEST_CASE("a buffer reader of another stage class is not covered either", "[render][graph]") {
    auto exposureBuffer = bufferFixture(16, "exposure");
    auto sceneColorTarget = textureFixture(64, 64, "sceneColor");
    auto histogramBuffer = bufferFixture(1024, "histogram");
    RenderGraph graph;
    const GraphBuffer exposure = graph.importBuffer(exposureBuffer, "exposure");
    const GraphTexture sceneColor = graph.importTexture(
        formatted(sceneColorTarget, rojoRHI::Format::RGBA16Float), "sceneColor");
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

    const LoweredFrame frame = lowered(graph);
    const CompiledFrameRecord& record = frame.record;

    REQUIRE(record.debug.transitions.size() == 2);
    REQUIRE(record.debug.transitions[0].bufferTo == lmx::render::BufferUse::ShaderRead);
    REQUIRE(record.debug.transitions[1].bufferTo == lmx::render::BufferUse::StorageRead);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "scope lmx.pass.seed", "run lmx.pass.seed",
                                     "end scope", "begin lmx.pass.scene", "run lmx.pass.scene",
                                     "end", "scope lmx.pass.histogram", "run lmx.pass.histogram",
                                     "end scope", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier exposure StorageWrite->ShaderRead",
                                     "barrier exposure StorageWrite->StorageRead"});
}

//======================================================================================================================
// A frame reading contents an earlier frame produced has no producer inside it to derive a barrier
// from, and the frames in flight give no ordering of their own -- so the import states the producer
// instead, and this frame's first reader is barriered against it as if the write were its own.
TEST_CASE("a buffer imported with a prior producer transitions before its first reader",
          "[render][graph]") {
    auto exposureBuffer = bufferFixture(16, "exposure");
    auto sceneColorTarget = textureFixture(64, 64, "sceneColor");
    RenderGraph graph;
    const GraphBuffer exposure =
        graph.importBuffer(exposureBuffer, "exposure", lmx::render::BufferUse::StorageWrite);
    const GraphTexture sceneColor = graph.importTexture(
        formatted(sceneColorTarget, rojoRHI::Format::RGBA16Float), "sceneColor");

    PassDesc scene;
    scene.bufferReads.push_back(exposure);
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    const LoweredFrame frame = lowered(graph);
    const CompiledFrameRecord& record = frame.record;

    REQUIRE(record.debug.transitions.size() == 1);
    REQUIRE(record.debug.transitions[0].beforePass == 0);
    REQUIRE(record.debug.transitions[0].kind == GraphResourceKind::Buffer);
    REQUIRE(record.debug.transitions[0].bufferFrom == lmx::render::BufferUse::StorageWrite);
    REQUIRE(record.debug.transitions[0].bufferTo == lmx::render::BufferUse::ShaderRead);

    // The barrier leads the frame: nothing precedes the pass that consumes it.
    REQUIRE(passEvents(frame) == std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.scene",
                                                          "run lmx.pass.scene", "end",
                                                          "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier exposure StorageWrite->ShaderRead"});
}

//======================================================================================================================
// The other half of that contract, and what keeps manual mode's frame exactly what it was: an
// import that claims no prior producer states no cross-frame edge, so a reader of it is barriered
// against nothing. Same frame as above, differing only in which import overload it used.
TEST_CASE("a buffer imported without a prior producer transitions nothing", "[render][graph]") {
    auto exposureBuffer = bufferFixture(16, "exposure");
    auto sceneColorTarget = textureFixture(64, 64, "sceneColor");
    RenderGraph graph;
    const GraphBuffer exposure = graph.importBuffer(exposureBuffer, "exposure");
    const GraphTexture sceneColor = graph.importTexture(
        formatted(sceneColorTarget, rojoRHI::Format::RGBA16Float), "sceneColor");

    PassDesc scene;
    scene.bufferReads.push_back(exposure);
    scene.color = ColorAttachment{.handle = sceneColor};
    graph.addPass("lmx.pass.scene", scene, kNoWork);
    graph.exportTexture(nextVersion(sceneColor));

    const LoweredFrame frame = lowered(graph);
    const CompiledFrameRecord& record = frame.record;

    REQUIRE(record.debug.transitions.empty());
    REQUIRE(passEvents(frame) == std::vector<std::string>{"scope lmx.frame", "begin lmx.pass.scene",
                                                          "run lmx.pass.scene", "end",
                                                          "end scope"});
    REQUIRE(logicalTransitions(frame.record) == std::vector<std::string>{});
}

//======================================================================================================================
// The histogram's terminal access is a resolve read, not its earlier accumulate write. Carry that
// exact use across frames so a following clear gets a WAR barrier and another read gets none.
TEST_CASE("a buffer import carries its previous-frame read", "[render][graph]") {
    auto persistent = bufferFixture(256, "persistent");
    auto output = bufferFixture(256, "output");

    SECTION("prior read before current write") {
        RenderGraph graph;
        const GraphBuffer buffer =
            graph.importBuffer(persistent, "persistent", lmx::render::BufferUse::StorageRead);
        ComputePassDesc overwrite;
        overwrite.bufferWrites.push_back(buffer);
        graph.addComputePass("lmx.pass.overwrite", overwrite, kNoWork);
        graph.exportBuffer(nextVersion(buffer));

        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.size() == 1);
        REQUIRE(record->debug.transitions[0].bufferFrom == lmx::render::BufferUse::StorageRead);
        REQUIRE(record->debug.transitions[0].bufferTo == lmx::render::BufferUse::StorageWrite);
    }

    SECTION("prior read before current read") {
        RenderGraph graph;
        const GraphBuffer buffer =
            graph.importBuffer(persistent, "persistent", lmx::render::BufferUse::StorageRead);
        const GraphBuffer result = graph.importBuffer(output, "output");
        ComputePassDesc read;
        read.bufferReads.push_back(buffer);
        read.bufferWrites.push_back(result);
        graph.addComputePass("lmx.pass.read", read, kNoWork);
        graph.exportBuffer(nextVersion(result));

        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.empty());
    }
}

//======================================================================================================================
// Persistent render targets are reused while earlier command buffers remain in flight. Their last
// access seeds the fresh graph: read/write conflicts cross that boundary, while two reads do not.
TEST_CASE("a texture import carries its previous-frame access", "[render][graph]") {
    auto persistent = textureFixture(64, 64, "persistent");
    auto output = textureFixture(64, 64, "output");

    SECTION("prior read before current write") {
        TextureStateTable states;
        states.setLogicalUse(persistent.texture, lmx::render::TextureUse::ShaderRead);
        RenderGraph graph;
        graph.bindTextureStates(states);
        const GraphTexture texture = graph.importPersistentTexture(
            formatted(persistent, rojoRHI::Format::BGRA8Unorm), "persistent");
        PassDesc overwrite;
        overwrite.color = ColorAttachment{.handle = texture};
        graph.addPass("lmx.pass.overwrite", overwrite, kNoWork);
        graph.exportTexture(nextVersion(texture));

        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.size() == 1);
        REQUIRE(record->debug.transitions[0].textureFrom == lmx::render::TextureUse::ShaderRead);
        REQUIRE(record->debug.transitions[0].textureTo == lmx::render::TextureUse::RenderTarget);
    }

    SECTION("prior read before current loaded attachment") {
        TextureStateTable states;
        states.setLogicalUse(persistent.texture, lmx::render::TextureUse::ShaderRead);
        RenderGraph graph;
        graph.bindTextureStates(states);
        const GraphTexture texture = graph.importPersistentTexture(
            formatted(persistent, rojoRHI::Format::BGRA8Unorm), "persistent");
        PassDesc loadAndOverwrite;
        loadAndOverwrite.color =
            ColorAttachment{.handle = texture, .load = LoadOp::Load, .store = StoreOp::Store};
        graph.addPass("lmx.pass.loadAndOverwrite", loadAndOverwrite, kNoWork);
        graph.exportTexture(nextVersion(texture));

        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.size() == 1);
        REQUIRE(record->debug.transitions[0].textureFrom == lmx::render::TextureUse::ShaderRead);
        REQUIRE(record->debug.transitions[0].textureTo == lmx::render::TextureUse::RenderTarget);
    }

    SECTION("prior write before current read") {
        TextureStateTable states;
        states.setLogicalUse(persistent.texture, lmx::render::TextureUse::RenderTarget);
        RenderGraph graph;
        graph.bindTextureStates(states);
        const GraphTexture texture = graph.importPersistentTexture(
            formatted(persistent, rojoRHI::Format::BGRA8Unorm), "persistent");
        const GraphTexture color =
            graph.importTexture(formatted(output, rojoRHI::Format::BGRA8Unorm), "output");
        PassDesc sample;
        sample.textureReads.push_back(texture);
        sample.color = ColorAttachment{.handle = color};
        graph.addPass("lmx.pass.sample", sample, kNoWork);
        graph.exportTexture(nextVersion(color));

        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.size() == 1);
        REQUIRE(record->debug.transitions[0].textureFrom == lmx::render::TextureUse::RenderTarget);
        REQUIRE(record->debug.transitions[0].textureTo == lmx::render::TextureUse::ShaderRead);
    }

    SECTION("prior read before current read") {
        TextureStateTable states;
        states.setLogicalUse(persistent.texture, lmx::render::TextureUse::ShaderRead);
        RenderGraph graph;
        graph.bindTextureStates(states);
        const GraphTexture texture = graph.importPersistentTexture(
            formatted(persistent, rojoRHI::Format::BGRA8Unorm), "persistent");
        const GraphTexture color =
            graph.importTexture(formatted(output, rojoRHI::Format::BGRA8Unorm), "output");
        PassDesc sample;
        sample.textureReads.push_back(texture);
        sample.color = ColorAttachment{.handle = color};
        graph.addPass("lmx.pass.sample", sample, kNoWork);
        graph.exportTexture(nextVersion(color));

        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.empty());
    }
}
