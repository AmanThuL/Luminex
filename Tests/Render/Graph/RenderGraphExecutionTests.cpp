#include "Support/RenderGraphTestSupport.h"
#include "Support/TestGpu.h"

//======================================================================================================================
// Each declaration path opens its own RHI scope, and the barrier between them is derived from the
// declared uses on both sides: a storage write followed by a sampled read.
TEST_CASE("execute encodes each pass kind in its own scope", "[render][graph][device]") {
    auto gpu = createTestGpu();
    auto storage =
        createTestTexture(*gpu, rojoRHI::Format::RGBA16Float,
                          rojoRHI::TextureUsage::Storage | rojoRHI::TextureUsage::Sampled);
    auto target =
        createTestTexture(*gpu, rojoRHI::Format::BGRA8Unorm, rojoRHI::TextureUsage::RenderTarget);
    RenderGraph graph;
    const GraphTexture written = graph.importTexture(storage.ref(), "storage");
    const GraphTexture color = graph.importTexture(target.ref(), "target");

    ComputePassDesc fill;
    fill.textureWrites.push_back(written);
    std::vector<std::string> bodies;
    graph.addComputePass("lmx.pass.fill", fill, [&](const PassResources& resources) {
        REQUIRE(resources.texture(written).texture == storage.rhi());
        bodies.push_back("fill");
    });

    PassDesc present;
    present.textureReads.push_back(nextVersion(written));
    present.color = ColorAttachment{.handle = color};
    graph.addPass("lmx.pass.present", present, [&](const PassResources& resources) {
        REQUIRE(resources.texture(nextVersion(written)).texture == storage.rhi());
        REQUIRE(resources.texture(color).texture == target.rhi());
        bodies.push_back("present");
    });
    graph.exportTexture(nextVersion(color));

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "scope lmx.pass.fill", "run lmx.pass.fill",
                                     "end scope", "begin lmx.pass.present", "run lmx.pass.present",
                                     "end", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier storage StorageWrite->ShaderRead"});

    auto recording = gpu->beginFrame();
    graph.execute(recording.commands, recording.number);
    gpu->submitAndEndFrame(recording);
    gpu->waitIdle();
    REQUIRE(bodies == std::vector<std::string>{"fill", "present"});
    const auto barriers = textureBarriers(frame);
    REQUIRE(barriers.size() == 3);
    REQUIRE(barriers[0].from.layout == rojoRHI::Layout::Undefined);
    REQUIRE(barriers[0].to.layout == rojoRHI::Layout::ShaderWrite);
    REQUIRE(barriers[1].resource == written.index);
    REQUIRE(barriers[1].from.access == rojoRHI::Access::ShaderWrite);
    REQUIRE(barriers[1].to.access == rojoRHI::Access::ShaderRead);
    REQUIRE(barriers[1].to.stage ==
            (rojoRHI::Stage::VertexShader | rojoRHI::Stage::FragmentShader));
}

//======================================================================================================================
// A copy pass's destination is a write like any other, so a compute pass reading it afterwards gets
// the copy-to-storage transition -- and buffers get one of their own, because a hazard on bytes has
// no texture edge to borrow.
TEST_CASE("execute derives a buffer barrier from a copy destination", "[render][graph][device]") {
    auto gpu = createTestGpu();
    auto histogram = createTestBuffer(*gpu, 1024, rojoRHI::BufferUsage::ShaderWrite);
    RenderGraph graph;
    const GraphBuffer bins = graph.importBuffer(histogram, "histogram");

    CopyPassDesc clear;
    clear.bufferDestinations.push_back(bins);
    std::vector<std::string> bodies;
    graph.addCopyPass("lmx.pass.clear", clear, [&](const PassResources& resources) {
        REQUIRE(resources.buffer(bins) == histogram.rhi());
        bodies.push_back("clear");
    });

    ComputePassDesc accumulate;
    accumulate.bufferReads.push_back(nextVersion(bins));
    accumulate.bufferWrites.push_back(nextVersion(bins));
    graph.addComputePass("lmx.pass.accumulate", accumulate, [&](const PassResources& resources) {
        REQUIRE(resources.buffer(nextVersion(bins)) == histogram.rhi());
        bodies.push_back("accumulate");
    });
    graph.exportBuffer(GraphBuffer{bins.index, 2});

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "scope lmx.pass.clear",
                                     "run lmx.pass.clear", "end scope", "scope lmx.pass.accumulate",
                                     "run lmx.pass.accumulate", "end scope", "end scope"});
    REQUIRE(logicalTransitions(frame.record) ==
            std::vector<std::string>{"barrier histogram CopyDestination->StorageRead",
                                     "barrier histogram CopyDestination->StorageWrite"});

    auto recording = gpu->beginFrame();
    graph.execute(recording.commands, recording.number);
    gpu->submitAndEndFrame(recording);
    gpu->waitIdle();
    REQUIRE(bodies == std::vector<std::string>{"clear", "accumulate"});
    const auto barriers = bufferBarriers(frame);
    REQUIRE(barriers.size() == 1);
    REQUIRE(barriers[0].from.stage == rojoRHI::Stage::Copy);
    REQUIRE(barriers[0].from.access == rojoRHI::Access::CopyWrite);
    REQUIRE(barriers[0].to.stage == rojoRHI::Stage::ComputeShader);
    REQUIRE(barriers[0].to.access == (rojoRHI::Access::ShaderRead | rojoRHI::Access::ShaderWrite));
}

//======================================================================================================================
// A compute callback may bind ordinary SRV/CBV inputs or an indirect argument; those uses are not
// UAV reads. The declaration has to preserve that distinction so stateful backends derive the same
// resource state the callback actually binds.
TEST_CASE("compute read roles map to their explicit RHI uses", "[render][graph]") {
    auto sampledTexture = textureFixture(64, 64, "sampled");
    auto constantsBuffer = bufferFixture(256, "constants");
    auto argumentsBuffer = bufferFixture(256, "arguments");
    auto outputBuffer = bufferFixture(256, "output");
    RenderGraph graph;
    const GraphTexture sampled =
        graph.importTexture(formatted(sampledTexture, rojoRHI::Format::RGBA16Float), "sampled");
    const GraphBuffer constants = graph.importBuffer(constantsBuffer, "constants");
    const GraphBuffer arguments = graph.importBuffer(argumentsBuffer, "arguments");
    const GraphBuffer output = graph.importBuffer(outputBuffer, "output");

    CopyPassDesc upload;
    upload.textureDestinations.push_back(sampled);
    upload.bufferDestinations.push_back(constants);
    upload.bufferDestinations.push_back(arguments);
    graph.addCopyPass("lmx.pass.upload", upload, kNoWork);

    ComputePassDesc dispatch;
    dispatch.shaderTextureReads.push_back(nextVersion(sampled));
    dispatch.shaderBufferReads.push_back(nextVersion(constants));
    dispatch.indirectBufferReads.push_back(nextVersion(arguments));
    dispatch.bufferWrites.push_back(output);
    graph.addComputePass("lmx.pass.dispatch", dispatch, kNoWork);
    graph.exportBuffer(nextVersion(output));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.transitions.size() == 3);
    REQUIRE(record->debug.transitions[0].textureTo == lmx::render::TextureUse::ShaderRead);
    REQUIRE(record->debug.transitions[1].bufferTo == lmx::render::BufferUse::ShaderRead);
    REQUIRE(record->debug.transitions[2].bufferTo == lmx::render::BufferUse::IndirectArgument);
}

//======================================================================================================================
// A ranged write replaces only the producer of the subresources it touches. Mip 0 therefore keeps
// the copy pass as its producer when the next version writes mip 1, and the later read must wait on
// CopyDestination rather than on the unrelated dispatch.
TEST_CASE("an untouched mip keeps its earlier writer use", "[render][graph]") {
    auto chain = textureFixture(64, 64, "chain", 4);
    auto output = textureFixture(64, 64, "output");
    RenderGraph graph;
    const GraphTexture texture =
        graph.importTexture(formatted(chain, rojoRHI::Format::RGBA16Float), "chain");
    const GraphTexture target =
        graph.importTexture(formatted(output, rojoRHI::Format::BGRA8Unorm), "output");

    CopyPassDesc copy;
    copy.textureDestinations.push_back({texture, {.baseMip = 0, .mipCount = 1}});
    graph.addCopyPass("lmx.pass.copyMip0", copy, kNoWork);

    ComputePassDesc write;
    write.textureWrites.push_back({nextVersion(texture), {.baseMip = 1, .mipCount = 1}});
    graph.addComputePass("lmx.pass.writeMip1", write, kNoWork);

    PassDesc read;
    read.textureReads.push_back({GraphTexture{texture.index, 2}, {.baseMip = 0, .mipCount = 1}});
    read.color = ColorAttachment{.handle = target};
    graph.addPass("lmx.pass.readMip0", read, kNoWork);
    graph.exportTexture(nextVersion(target));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.transitions.size() == 1);
    REQUIRE(record->debug.transitions[0].beforePass == 2);
    REQUIRE(record->debug.transitions[0].range.baseMip == 0);
    REQUIRE(record->debug.transitions[0].textureFrom == lmx::render::TextureUse::CopyDestination);
    REQUIRE(record->debug.transitions[0].textureTo == lmx::render::TextureUse::ShaderRead);
}

//======================================================================================================================
// Encoding order is not a dependency for Metal 4's untracked resources: two writes to the same
// subresource still need a queue-stage barrier between their passes.
TEST_CASE("a texture write is barriered after an earlier write", "[render][graph]") {
    auto chain = textureFixture(64, 64, "chain", 4);
    RenderGraph graph;
    const GraphTexture texture =
        graph.importTexture(formatted(chain, rojoRHI::Format::RGBA16Float), "chain");
    const rojoRHI::SubresourceRange mip0{.baseMip = 0, .mipCount = 1};

    CopyPassDesc copy;
    copy.textureDestinations.push_back({texture, mip0});
    graph.addCopyPass("lmx.pass.copy", copy, kNoWork);

    ComputePassDesc overwrite;
    overwrite.textureWrites.push_back({nextVersion(texture), mip0});
    graph.addComputePass("lmx.pass.overwrite", overwrite, kNoWork);
    graph.exportTexture(GraphTexture{texture.index, 2});

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.transitions.size() == 1);
    REQUIRE(record->debug.transitions[0].beforePass == 1);
    REQUIRE(record->debug.transitions[0].textureFrom == lmx::render::TextureUse::CopyDestination);
    REQUIRE(record->debug.transitions[0].textureTo == lmx::render::TextureUse::StorageWrite);
}

//======================================================================================================================
// Buffers have no subresource split, so every write conflicts with the preceding whole-buffer
// writer, including a producer imported from an earlier frame.
TEST_CASE("a buffer write is barriered after its preceding writer", "[render][graph]") {
    auto storage = bufferFixture(256, "storage");

    SECTION("copy destination") {
        RenderGraph graph;
        const GraphBuffer buffer = graph.importBuffer(storage, "storage");
        CopyPassDesc copy;
        copy.bufferDestinations.push_back(buffer);
        graph.addCopyPass("lmx.pass.copy", copy, kNoWork);
        ComputePassDesc overwrite;
        overwrite.bufferWrites.push_back(nextVersion(buffer));
        graph.addComputePass("lmx.pass.overwrite", overwrite, kNoWork);
        graph.exportBuffer(GraphBuffer{buffer.index, 2});

        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.size() == 1);
        REQUIRE(record->debug.transitions[0].bufferFrom == lmx::render::BufferUse::CopyDestination);
        REQUIRE(record->debug.transitions[0].bufferTo == lmx::render::BufferUse::StorageWrite);
    }

    SECTION("prior-frame producer") {
        RenderGraph graph;
        const GraphBuffer buffer =
            graph.importBuffer(storage, "storage", lmx::render::BufferUse::StorageWrite);
        ComputePassDesc overwrite;
        overwrite.bufferWrites.push_back(buffer);
        graph.addComputePass("lmx.pass.overwrite", overwrite, kNoWork);
        graph.exportBuffer(nextVersion(buffer));

        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.size() == 1);
        REQUIRE(record->debug.transitions[0].bufferFrom == lmx::render::BufferUse::StorageWrite);
        REQUIRE(record->debug.transitions[0].bufferTo == lmx::render::BufferUse::StorageWrite);
    }
}

//======================================================================================================================
// The derived barrier carries the subresources the consumer declared, so a reader of one mip does
// not describe itself as depending on the whole chain.
TEST_CASE("a derived barrier carries the range the reader declared", "[render][graph]") {
    auto chain = textureFixture(64, 64, "chain", 4);
    RenderGraph graph;
    const GraphTexture bloom =
        graph.importTexture(formatted(chain, rojoRHI::Format::RGBA16Float), "bloom");

    ComputePassDesc write;
    write.textureWrites.push_back({bloom, {.baseMip = 0, .mipCount = 1}});
    graph.addComputePass("lmx.pass.write", write, kNoWork);

    ComputePassDesc downsample;
    downsample.textureReads.push_back({nextVersion(bloom), {.baseMip = 0, .mipCount = 1}});
    downsample.textureWrites.push_back({nextVersion(bloom), {.baseMip = 1, .mipCount = 1}});
    graph.addComputePass("lmx.pass.downsample", downsample, kNoWork);
    graph.exportTexture(GraphTexture{bloom.index, 2});

    const LoweredFrame frame = lowered(graph);

    REQUIRE(passEvents(frame) ==
            std::vector<std::string>{"scope lmx.frame", "scope lmx.pass.write",
                                     "run lmx.pass.write", "end scope", "scope lmx.pass.downsample",
                                     "run lmx.pass.downsample", "end scope", "end scope"});
    REQUIRE(
        logicalTransitions(frame.record) ==
        std::vector<std::string>{"barrier bloom mips[0..0] layers[0..] StorageWrite->StorageRead"});

    const auto barriers = textureBarriers(frame);
    const auto dependency = std::ranges::find_if(barriers, [&](const auto& barrier) {
        return barrier.resource == bloom.index &&
               barrier.from.access == rojoRHI::Access::ShaderWrite;
    });
    REQUIRE(dependency != barriers.end());
    REQUIRE(dependency->range.baseMip == 0);
    REQUIRE(dependency->range.mipCount == 1);
    REQUIRE(dependency->to.access == rojoRHI::Access::ShaderRead);
    REQUIRE(dependency->to.layout == rojoRHI::Layout::ShaderWrite);
}
