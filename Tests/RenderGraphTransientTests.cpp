#include "RenderGraphTestSupport.h"

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
    REQUIRE(reuse.textureFrom == rojoRHI::TextureUse::ShaderRead);
    REQUIRE(reuse.textureTo == rojoRHI::TextureUse::RenderTarget);
    REQUIRE(reuse.range.mipLevelCount == rojoRHI::kAllMipLevels);
    REQUIRE(reuse.range.arrayLayerCount == rojoRHI::kAllArrayLayers);

    // The two ordinary transitions carry no alias, so an observer can tell the two kinds apart.
    REQUIRE_FALSE(record->debug.transitions[0].aliasedFrom.has_value());
    REQUIRE_FALSE(record->debug.transitions[2].aliasedFrom.has_value());
}

//======================================================================================================================
// One terminal dispatch may finish reading one mip while writing another. Reusing the allocation
// must close both stage uses; keeping only the last declaration would leave half of that dispatch
// outside the alias boundary.
TEST_CASE("an alias boundary includes every use of the closing pass", "[render][graph]") {
    FakeDevice device;
    TransientPool pool(device);
    FakeBuffer orderingBuffer{256, "ordering"};
    FakeTexture outputTexture{64, 64, "output"};
    RenderGraph graph(pool);

    const TransientTextureDesc desc{.width = 64,
                                    .height = 64,
                                    .format = rojoRHI::Format::RGBA16Float,
                                    .mipLevels = 2,
                                    .storageRead = true,
                                    .storageWrite = true};
    const GraphTexture first = graph.createTexture(desc, "lmx.transient.first");
    const GraphTexture second = graph.createTexture(desc, "lmx.transient.second");
    const GraphBuffer ordering = graph.importBuffer(orderingBuffer, "ordering");
    const GraphTexture output =
        graph.importTexture(outputTexture, rojoRHI::Format::BGRA8Unorm, "output");
    constexpr rojoRHI::TextureSubresourceRange kMip0{.baseMipLevel = 0, .mipLevelCount = 1};
    constexpr rojoRHI::TextureSubresourceRange kMip1{.baseMipLevel = 1, .mipLevelCount = 1};

    ComputePassDesc openFirst;
    openFirst.textureWrites.push_back({first, kMip0});
    openFirst.bufferWrites.push_back(ordering);
    graph.addComputePass("lmx.pass.openFirst", openFirst, kNoWork);

    ComputePassDesc closeFirst;
    closeFirst.shaderTextureReads.push_back({nextVersion(first), kMip0});
    closeFirst.textureWrites.push_back({nextVersion(first), kMip1});
    closeFirst.bufferWrites.push_back(nextVersion(ordering));
    graph.addComputePass("lmx.pass.closeFirst", closeFirst, kNoWork);

    ComputePassDesc openSecond;
    openSecond.textureWrites.push_back({second, kMip0});
    openSecond.bufferWrites.push_back(GraphBuffer{ordering.index, 2});
    graph.addComputePass("lmx.pass.openSecond", openSecond, kNoWork);

    PassDesc consumeSecond;
    consumeSecond.textureReads.push_back({nextVersion(second), kMip0});
    consumeSecond.color = ColorAttachment{.handle = output};
    graph.addPass("lmx.pass.consumeSecond", consumeSecond, kNoWork);
    graph.exportBuffer(GraphBuffer{ordering.index, 3});
    graph.exportTexture(nextVersion(output));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());

    std::vector<rojoRHI::TextureUse> closingUses;
    for (const DebugTransition& transition : record->debug.transitions) {
        if (transition.aliasedFrom == std::optional{first.index}) {
            closingUses.push_back(transition.textureFrom);
            REQUIRE(transition.beforePass == 2);
            REQUIRE(transition.textureTo == rojoRHI::TextureUse::StorageWrite);
        }
    }
    REQUIRE(closingUses == std::vector<rojoRHI::TextureUse>{rojoRHI::TextureUse::ShaderRead,
                                                            rojoRHI::TextureUse::StorageWrite});
}

//======================================================================================================================
// A feature can leave transient declarations behind while culling every pass that uses them. That
// zero-footprint frame still reserves zero bytes so the slot gives back the heap the live feature
// used the last time this frame-in-flight slot came around.
TEST_CASE("a culled transient releases the frame slot heap", "[render][graph]") {
    FakeDevice device;
    TransientPool pool(device);

    device.frame = 1;
    pool.beginFrame();
    REQUIRE(pool.reserve(FakeDevice::kTextureAlignment).has_value());
    REQUIRE(pool.heapBytes() == FakeDevice::kTextureAlignment);

    device.frame = 1 + kTransientFrameSlots;
    pool.beginFrame();

    FakeTexture outputTexture{64, 64, "output"};
    RenderGraph graph(pool);
    graph.createTexture(kTransientColor, "lmx.transient.culled");
    const GraphTexture output =
        graph.importTexture(outputTexture, rojoRHI::Format::BGRA8Unorm, "output");
    PassDesc display;
    display.color = ColorAttachment{.handle = output};
    graph.addPass("lmx.pass.display", display, kNoWork);
    graph.exportTexture(nextVersion(output));

    RecordingCommandList commands;
    const CompiledFrameRecord record = graph.execute(commands, device.frame);
    REQUIRE(record.debug.transients.size() == 1);
    REQUIRE_FALSE(record.debug.transients[0].used);
    REQUIRE(record.debug.memory.highWater == 0);
    REQUIRE(pool.heapBytes() == 0);
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
    const GraphTexture out = graph.importTexture(outTarget, rojoRHI::Format::BGRA8Unorm, "out");

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
          .format = rojoRHI::Format::RGBA8Unorm,
          .renderTarget = true,
          .sampled = true}},
        // Same format and footprint, one extra usage.
        {"usage",
         {.width = 64,
          .height = 64,
          .format = rojoRHI::Format::RGBA16Float,
          .renderTarget = true,
          .sampled = true,
          .storageWrite = true}},
        // Same format and usage, different extent -- and therefore a different size.
        {"extent",
         {.width = 128,
          .height = 128,
          .format = rojoRHI::Format::RGBA16Float,
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
    const GraphTexture out = graph.importTexture(outTarget, rojoRHI::Format::BGRA8Unorm, "out");

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
    const GraphTexture out = graph.importTexture(outTarget, rojoRHI::Format::BGRA8Unorm, "out");

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
    const GraphTexture out = graph.importTexture(outTarget, rojoRHI::Format::BGRA8Unorm, "out");

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
    REQUIRE(reuse->bufferFrom == rojoRHI::BufferUse::StorageRead);
    REQUIRE(reuse->bufferTo == rojoRHI::BufferUse::StorageWrite);
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
    const GraphTexture sceneColor = graph.importTexture(
        sceneColorTexture, rojoRHI::Format::RGBA16Float, "lmx.render.sceneColorHdr");
    const GraphTexture displayColor = graph.importTexture(
        displayColorTexture, rojoRHI::Format::BGRA8Unorm, "lmx.render.displayColor");
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
                                                         .format = rojoRHI::Format::RGBA16Float,
                                                         .mipLevels = 2,
                                                         .storageRead = true,
                                                         .storageWrite = true},
                                                        "lmx.render.bloomChain");
    const GraphTexture bloomBlur = graph.createTexture({.width = 32,
                                                        .height = 32,
                                                        .format = rojoRHI::Format::RGBA16Float,
                                                        .mipLevels = 1,
                                                        .storageRead = true,
                                                        .storageWrite = true},
                                                       "lmx.render.bloomBlur");
    constexpr rojoRHI::TextureSubresourceRange kMip0{.baseMipLevel = 0, .mipLevelCount = 1};
    constexpr rojoRHI::TextureSubresourceRange kMip1{.baseMipLevel = 1, .mipLevelCount = 1};

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
