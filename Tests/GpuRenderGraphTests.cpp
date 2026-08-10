#include "GpuTestSupport.h"

#include "Render/RenderGraph.h"

namespace {

// Mirrors BufferHazardSmoke.slang's HazardParams.
struct HazardParams {
    uint32_t bias = 0;
};

// Mirrors ComputeImageSmoke.slang's ImageParams.
struct ImageParams {
    uint32_t extent = 0;
};

constexpr uint32_t kHazardThreadsPerGroup = 64;
constexpr uint32_t kImageThreadsPerGroup = 8;

// The argument-table slots the two smoke modules' globals compile to.
constexpr uint32_t kHazardOutputSlot = 0;
constexpr uint32_t kHazardSourceSlot = 1;
constexpr uint32_t kHazardParamsSlot = 2;
constexpr uint32_t kImageSlot = 0;
constexpr uint32_t kImageParamsSlot = 1;

//======================================================================================================================
// The gradient computeWriteImage writes, as the 8-bit channel value at a texel of a square image.
uint8_t gradientChannel(uint32_t coordinate) {
    const float value = static_cast<float>(coordinate) / static_cast<float>(kSize - 1);
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

} // namespace

//======================================================================================================================
// The graph's derived buffer barrier, proved on the GPU. Nothing here records a barrier: the copy
// pass declares the buffer as a destination, the compute pass declares the version it produced as a
// read, and the ordering between the two is entirely the graph's answer. The sentinel the
// accumulator starts from is what a missing barrier reads back instead of the filled value.
TEST_CASE("a graph-declared copy pass feeds a graph-declared compute pass", "[gpu]") {
    using namespace lmx::rhi;
    using namespace lmx::render;

    constexpr uint32_t kElements = 256;
    constexpr uint32_t kBias = 7;
    // Every byte of the fill is this value, so each uint32 element reads back as 0x03030303.
    constexpr uint8_t kFillValue = 0x03;
    constexpr uint32_t kFilledElement = 0x03030303;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/BufferHazardSmoke");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto pipeline =
        (*device)->createComputePipeline({.library = library->get(),
                                          .computeEntry = "computeAddFromBuffer",
                                          .threadsPerThreadgroup = {kHazardThreadsPerGroup, 1, 1},
                                          .label = "lmx.test.graph.hazardPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    const std::vector<uint32_t> sentinel(kElements, 0xFFFFFFFFu);
    auto accumulator = (*device)->createBuffer({.size = sizeof(uint32_t) * kElements,
                                                .storageRead = true,
                                                .label = "lmx.test.graph.accumulator"},
                                               sentinel.data());
    INFO(errorOf(accumulator));
    REQUIRE(accumulator.has_value());

    auto resolved = (*device)->createBuffer({.size = sizeof(uint32_t) * kElements,
                                             .storageWrite = true,
                                             .cpuReadback = true,
                                             .label = "lmx.test.graph.resolved"},
                                            nullptr);
    INFO(errorOf(resolved));
    REQUIRE(resolved.has_value());

    const HazardParams params{.bias = kBias};

    CommandList& commands = (*device)->beginFrame();

    RenderGraph graph;
    const GraphBuffer bins = graph.importBuffer(**accumulator, "lmx.test.graph.accumulator");
    const GraphBuffer output = graph.importBuffer(**resolved, "lmx.test.graph.resolved");

    CopyPassDesc clear;
    clear.bufferDestinations.push_back(bins);
    graph.addCopyPass("lmx.test.graph.clear", clear, [&](const PassResources& resources) {
        const GraphResult<Buffer*> buffer = resources.buffer(bins);
        REQUIRE(buffer.has_value());
        commands.fillBuffer(**buffer, 0, sizeof(uint32_t) * kElements, kFillValue);
    });

    ComputePassDesc accumulate;
    accumulate.bufferReads.push_back(nextVersion(bins));
    accumulate.bufferWrites.push_back(output);
    graph.addComputePass(
        "lmx.test.graph.accumulate", accumulate, [&](const PassResources& resources) {
            const GraphResult<Buffer*> source = resources.buffer(nextVersion(bins));
            REQUIRE(source.has_value());
            const GraphResult<Buffer*> destination = resources.buffer(output);
            REQUIRE(destination.has_value());

            commands.bindComputePipeline(**pipeline);
            commands.bindStorageBuffer(kHazardOutputSlot, **destination, StorageAccess::Write);
            commands.bindStorageBuffer(kHazardSourceSlot, **source, StorageAccess::Read);
            commands.setUniforms(kHazardParamsSlot, &params, sizeof(params));
            commands.dispatch(kElements / kHazardThreadsPerGroup, 1, 1);
        });

    // The readback below is the frame's whole point, and declaring it is what keeps the two passes
    // that produce it out of the cull.
    graph.readbackBuffer(nextVersion(output));

    graph.execute(commands, (*device)->frameNumber());
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint32_t> values(kElements, 0);
    (*resolved)->readback(values.data(), values.size() * sizeof(uint32_t));

    for (uint32_t index = 0; index < kElements; ++index) {
        INFO("element " + std::to_string(index));
        REQUIRE(values[index] == kFilledElement + kBias);
    }
}

//======================================================================================================================
// The other direction across pass kinds: a compute pass writes a storage texture and a raster pass
// samples it, with the storage-write-to-shader-read transition derived from the two declarations.
// The magenta clear is what a draw that ran before the dispatch would leave behind.
TEST_CASE("a graph-declared compute pass feeds a raster pass", "[gpu]") {
    using namespace lmx::rhi;
    using namespace lmx::render;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto computeLibrary = (*device)->loadShaderLibrary("Shaders/ComputeImageSmoke");
    INFO(errorOf(computeLibrary));
    REQUIRE(computeLibrary.has_value());

    auto computePipeline = (*device)->createComputePipeline(
        {.library = computeLibrary->get(),
         .computeEntry = "computeWriteImage",
         .threadsPerThreadgroup = {kImageThreadsPerGroup, kImageThreadsPerGroup, 1},
         .label = "lmx.test.graph.imagePipeline"});
    INFO(errorOf(computePipeline));
    REQUIRE(computePipeline.has_value());

    auto sampleLibrary = (*device)->loadShaderLibrary("Shaders/FullscreenSample");
    INFO(errorOf(sampleLibrary));
    REQUIRE(sampleLibrary.has_value());

    auto samplePipeline =
        (*device)->createGraphicsPipeline({.library = sampleLibrary->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = Format::BGRA8Unorm,
                                           .label = "lmx.test.graph.samplePipeline"});
    INFO(errorOf(samplePipeline));
    REQUIRE(samplePipeline.has_value());

    auto image = (*device)->createTexture({.width = kSize,
                                           .height = kSize,
                                           .format = Format::RGBA8Unorm,
                                           .sampled = true,
                                           .storageWrite = true,
                                           .label = "lmx.test.graph.image"});
    INFO(errorOf(image));
    REQUIRE(image.has_value());

    auto target = makeProbeTarget(**device, "lmx.test.graph.target");
    INFO(errorOf(target));
    REQUIRE(target.has_value());

    const ImageParams params{.extent = kSize};
    const uint32_t groups = kSize / kImageThreadsPerGroup;

    CommandList& commands = (*device)->beginFrame();

    RenderGraph graph;
    const GraphTexture storage =
        graph.importTexture(**image, Format::RGBA8Unorm, "lmx.test.graph.image");
    const GraphTexture color =
        graph.importTexture(**target, Format::BGRA8Unorm, "lmx.test.graph.target");

    ComputePassDesc write;
    write.textureWrites.push_back(storage);
    graph.addComputePass("lmx.test.graph.write", write, [&](const PassResources& resources) {
        const GraphResult<Texture*> texture = resources.texture(storage);
        REQUIRE(texture.has_value());
        commands.bindComputePipeline(**computePipeline);
        commands.bindStorageTexture(kImageSlot, **texture, {}, StorageAccess::Write);
        commands.setUniforms(kImageParamsSlot, &params, sizeof(params));
        commands.dispatch(groups, groups, 1);
    });

    PassDesc draw;
    draw.textureReads.push_back(nextVersion(storage));
    draw.color = ColorAttachment{.handle = color, .clearColor = {1.0f, 0.0f, 1.0f, 1.0f}};
    graph.addPass("lmx.test.graph.sample", draw, [&](const PassResources& resources) {
        const GraphResult<Texture*> texture = resources.texture(nextVersion(storage));
        REQUIRE(texture.has_value());
        commands.bindPipeline(**samplePipeline);
        commands.bindTexture(0, **texture);
        commands.draw(3);
    });

    graph.readbackTexture(nextVersion(color));

    graph.execute(commands, (*device)->frameNumber());
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*target)->readback(pixels.data(), pixels.size());

    const std::array<std::pair<uint32_t, uint32_t>, 3> probes = {
        {{0, 0}, {kSize - 1, 0}, {21, 42}}};
    for (const auto& [x, y] : probes) {
        const Pixel texel = pixelAt(pixels, x, y);
        INFO(describe("sampled", x, y, texel));
        REQUIRE(channelNear(texel.r, gradientChannel(x), 1));
        REQUIRE(channelNear(texel.g, gradientChannel(y), 1));
        REQUIRE(channelNear(texel.b, 64, 1));
        REQUIRE(texel.a == 255);
    }
}
