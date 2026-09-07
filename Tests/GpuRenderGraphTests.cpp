#include "GpuTestSupport.h"

#include "App/FrameRecordRing.h"
#include "Render/RenderGraph.h"

#include <cstring>

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
            commands.bindFrameData(kHazardParamsSlot, params);
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
        commands.bindFrameData(kImageParamsSlot, params);
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

//======================================================================================================================
// The join the editor's observability rests on, driven by a real device rather than by values a
// test made up: several frames are declared and retained, and the frame the RHI eventually
// publishes timings for is found in the ring by its number, with the labels of the passes that
// frame declared. A ring that retained only the frames in flight would have evicted it.
TEST_CASE("a retained frame record joins the timings of the frame it describes", "[gpu]") {
    using namespace lmx::rhi;
    using namespace lmx::render;

    constexpr uint32_t kFrames = 6;
    constexpr uint64_t kFillBytes = 256;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto storage = (*device)->createBuffer(
        {.size = kFillBytes, .storageRead = true, .label = "lmx.test.graph.retained"}, nullptr);
    INFO(errorOf(storage));
    REQUIRE(storage.has_value());

    lmx::app::FrameRecordRing records;

    const auto declareFrame = [&] {
        CommandList& commands = (*device)->beginFrame();
        RenderGraph graph;
        const GraphBuffer target = graph.importBuffer(**storage, "lmx.test.graph.retained");

        CopyPassDesc clear;
        clear.bufferDestinations.push_back(target);
        graph.addCopyPass("lmx.test.graph.retainedClear", clear,
                          [&](const PassResources& resources) {
                              const GraphResult<Buffer*> buffer = resources.buffer(target);
                              REQUIRE(buffer.has_value());
                              commands.fillBuffer(**buffer, 0, kFillBytes, 0);
                          });
        graph.exportBuffer(nextVersion(target));

        records.retain(graph.execute(commands, (*device)->frameNumber()));
        records.joinTimings((*device)->passTimingsFrame(), (*device)->passTimings());
        (*device)->endFrame(nullptr);
    };

    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        declareFrame();
    }

    // The one sequence passTimings() documents as publishing a specific frame: drain, then open one
    // more frame, which publishes the frame that just retired.
    (*device)->waitIdle();
    (*device)->beginFrame();
    const uint64_t measured = (*device)->passTimingsFrame();
    REQUIRE(records.joinTimings(measured, (*device)->passTimings()));
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    const lmx::app::RetainedFrame* newest = records.newestTimedFrame();
    REQUIRE(newest != nullptr);
    REQUIRE(newest->record.frameId == measured);
    REQUIRE(newest->timed);

    // The record and the timings describe the same frame, so the pass the record scheduled is the
    // pass the GPU measured.
    REQUIRE(newest->record.debug.schedule.passes.size() == 1);
    const DebugPass& scheduled =
        newest->record.debug.passes[newest->record.debug.schedule.passes[0]];
    REQUIRE(scheduled.label == "lmx.test.graph.retainedClear");
    REQUIRE(newest->timings.size() == 1);
    REQUIRE(newest->timings[0].label == scheduled.label);
    REQUIRE(newest->timings[0].gpuMilliseconds >= 0.0);
}

//======================================================================================================================
// The declaration path proved end to end: the graph is the only thing that fills the pass
// descriptor, so a readback of the second attachment showing MrtSmoke's own values is what says the
// extra reached the hardware as attachment one rather than being dropped or aliased onto the
// primary.
TEST_CASE("a graph-declared pass writes an extra color attachment", "[gpu]") {
    using namespace lmx::rhi;
    using namespace lmx::render;

    // The two halves MrtSmoke.slang writes: 0.25 is 2^-2 and -0.5 is 2^-1, both exact in half
    // precision, so the readback is compared bit for bit.
    constexpr uint16_t kWrittenMotionX = 0x3400;
    constexpr uint16_t kWrittenMotionY = 0xB800;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto color = makeProbeTarget(**device, "lmx.test.graph.mrtColor");
    INFO(errorOf(color));
    REQUIRE(color.has_value());

    auto motion = (*device)->createTexture({.width = kSize,
                                            .height = kSize,
                                            .format = Format::RG16Float,
                                            .renderTarget = true,
                                            .cpuReadback = true,
                                            .label = "lmx.test.graph.mrtMotion"});
    INFO(errorOf(motion));
    REQUIRE(motion.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/MrtSmoke");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto pipeline = (*device)->createGraphicsPipeline(
        {.library = library->get(),
         .vertexEntry = "vertexMain",
         .fragmentEntry = "fragmentMain",
         .colorFormat = Format::BGRA8Unorm,
         .extraColorFormats = {Format::RG16Float, Format::Unknown, Format::Unknown},
         .extraColorCount = 1,
         .label = "lmx.test.graph.mrtPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    CommandList& commands = (*device)->beginFrame();

    RenderGraph graph;
    const GraphTexture sceneColor =
        graph.importTexture(**color, Format::BGRA8Unorm, "lmx.test.graph.mrtColor");
    const GraphTexture motionVectors =
        graph.importTexture(**motion, Format::RG16Float, "lmx.test.graph.mrtMotion");

    PassDesc scene;
    scene.color = ColorAttachment{.handle = sceneColor, .clearColor = {0.0f, 1.0f, 0.0f, 1.0f}};
    scene.extraColor.push_back(ColorAttachment{.handle = motionVectors});
    graph.addPass("lmx.test.graph.mrt", scene, [&](const PassResources&) {
        commands.bindPipeline(**pipeline);
        commands.draw(3);
    });

    // The extra's version is the only sink, so it is also what keeps the pass out of the cull.
    graph.readbackTexture(nextVersion(motionVectors));

    graph.execute(commands, (*device)->frameNumber());
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> colorPixels(size_t{kSize} * kSize * 4);
    (*color)->readback(colorPixels.data(), colorPixels.size());
    std::vector<uint8_t> motionPixels(size_t{kSize} * kSize * 2 * sizeof(uint16_t));
    (*motion)->readback(motionPixels.data(), motionPixels.size());

    const Pixel pixel = pixelAt(colorPixels, kSize / 2, kSize / 2);
    INFO(describe("color", kSize / 2, kSize / 2, pixel));
    REQUIRE(pixel.r == 255);
    REQUIRE(pixel.g == 0);
    REQUIRE(pixel.b == 0);

    uint16_t motionX = 0;
    uint16_t motionY = 0;
    const size_t offset = (size_t{kSize / 2} * kSize + kSize / 2) * 2 * sizeof(uint16_t);
    std::memcpy(&motionX, motionPixels.data() + offset, sizeof(uint16_t));
    std::memcpy(&motionY, motionPixels.data() + offset + sizeof(uint16_t), sizeof(uint16_t));
    INFO("motion R=" + std::to_string(motionX) + " G=" + std::to_string(motionY));
    REQUIRE(motionX == kWrittenMotionX);
    REQUIRE(motionY == kWrittenMotionY);
}
