#include "RenderGraphTestSupport.h"

//======================================================================================================================
// Each declaration path opens its own RHI scope, and the barrier between them is derived from the
// declared uses on both sides: a storage write followed by a sampled read.
TEST_CASE("execute encodes each pass kind in its own scope", "[render][graph]") {
    FakeTexture storage{64, 64, "storage"};
    FakeTexture target{64, 64, "target"};
    RenderGraph graph;
    const GraphTexture written =
        graph.importTexture(storage, rojoRHI::Format::RGBA16Float, "storage");
    const GraphTexture color = graph.importTexture(target, rojoRHI::Format::BGRA8Unorm, "target");

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
                                     "barrier histogram CopyDestination->StorageWrite",
                                     "begin compute lmx.pass.accumulate", "end compute"});
}

//======================================================================================================================
// A compute callback may bind ordinary SRV/CBV inputs or an indirect argument; those uses are not
// UAV reads. The declaration has to preserve that distinction so stateful backends derive the same
// resource state the callback actually binds.
TEST_CASE("compute read roles map to their explicit RHI uses", "[render][graph]") {
    FakeTexture sampledTexture{64, 64, "sampled"};
    FakeBuffer constantsBuffer{256, "constants"};
    FakeBuffer argumentsBuffer{256, "arguments"};
    FakeBuffer outputBuffer{256, "output"};
    RenderGraph graph;
    const GraphTexture sampled =
        graph.importTexture(sampledTexture, rojoRHI::Format::RGBA16Float, "sampled");
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
    REQUIRE(record->debug.transitions[0].textureTo == rojoRHI::TextureUse::ShaderRead);
    REQUIRE(record->debug.transitions[1].bufferTo == rojoRHI::BufferUse::ShaderRead);
    REQUIRE(record->debug.transitions[2].bufferTo == rojoRHI::BufferUse::IndirectArgument);
}

//======================================================================================================================
// A ranged write replaces only the producer of the subresources it touches. Mip 0 therefore keeps
// the copy pass as its producer when the next version writes mip 1, and the later read must wait on
// CopyDestination rather than on the unrelated dispatch.
TEST_CASE("an untouched mip keeps its earlier writer use", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    FakeTexture output{64, 64, "output"};
    RenderGraph graph;
    const GraphTexture texture = graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "chain");
    const GraphTexture target = graph.importTexture(output, rojoRHI::Format::BGRA8Unorm, "output");

    CopyPassDesc copy;
    copy.textureDestinations.push_back({texture, {.baseMipLevel = 0, .mipLevelCount = 1}});
    graph.addCopyPass("lmx.pass.copyMip0", copy, kNoWork);

    ComputePassDesc write;
    write.textureWrites.push_back({nextVersion(texture), {.baseMipLevel = 1, .mipLevelCount = 1}});
    graph.addComputePass("lmx.pass.writeMip1", write, kNoWork);

    PassDesc read;
    read.textureReads.push_back(
        {GraphTexture{texture.index, 2}, {.baseMipLevel = 0, .mipLevelCount = 1}});
    read.color = ColorAttachment{.handle = target};
    graph.addPass("lmx.pass.readMip0", read, kNoWork);
    graph.exportTexture(nextVersion(target));

    const auto record = graph.compileFrame(1);
    INFO(errorOf(record));
    REQUIRE(record.has_value());
    REQUIRE(record->debug.transitions.size() == 1);
    REQUIRE(record->debug.transitions[0].beforePass == 2);
    REQUIRE(record->debug.transitions[0].range.baseMipLevel == 0);
    REQUIRE(record->debug.transitions[0].textureFrom == rojoRHI::TextureUse::CopyDestination);
    REQUIRE(record->debug.transitions[0].textureTo == rojoRHI::TextureUse::ShaderRead);
}

//======================================================================================================================
// Encoding order is not a dependency for Metal 4's untracked resources: two writes to the same
// subresource still need a queue-stage barrier between their passes.
TEST_CASE("a texture write is barriered after an earlier write", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    RenderGraph graph;
    const GraphTexture texture = graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "chain");
    const rojoRHI::TextureSubresourceRange mip0{.baseMipLevel = 0, .mipLevelCount = 1};

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
    REQUIRE(record->debug.transitions[0].textureFrom == rojoRHI::TextureUse::CopyDestination);
    REQUIRE(record->debug.transitions[0].textureTo == rojoRHI::TextureUse::StorageWrite);
}

//======================================================================================================================
// Buffers have no subresource split, so every write conflicts with the preceding whole-buffer
// writer, including a producer imported from an earlier frame.
TEST_CASE("a buffer write is barriered after its preceding writer", "[render][graph]") {
    FakeBuffer storage{256, "storage"};

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
        REQUIRE(record->debug.transitions[0].bufferFrom == rojoRHI::BufferUse::CopyDestination);
        REQUIRE(record->debug.transitions[0].bufferTo == rojoRHI::BufferUse::StorageWrite);
    }

    SECTION("prior-frame producer") {
        RenderGraph graph;
        const GraphBuffer buffer =
            graph.importBuffer(storage, "storage", rojoRHI::BufferUse::StorageWrite);
        ComputePassDesc overwrite;
        overwrite.bufferWrites.push_back(buffer);
        graph.addComputePass("lmx.pass.overwrite", overwrite, kNoWork);
        graph.exportBuffer(nextVersion(buffer));

        const auto record = graph.compileFrame(1);
        REQUIRE(record.has_value());
        REQUIRE(record->debug.transitions.size() == 1);
        REQUIRE(record->debug.transitions[0].bufferFrom == rojoRHI::BufferUse::StorageWrite);
        REQUIRE(record->debug.transitions[0].bufferTo == rojoRHI::BufferUse::StorageWrite);
    }
}

//======================================================================================================================
// The derived barrier carries the subresources the consumer declared, so a reader of one mip does
// not describe itself as depending on the whole chain.
TEST_CASE("a derived barrier carries the range the reader declared", "[render][graph]") {
    FakeTexture chain{64, 64, "chain", 4};
    RenderGraph graph;
    const GraphTexture bloom = graph.importTexture(chain, rojoRHI::Format::RGBA16Float, "bloom");

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
