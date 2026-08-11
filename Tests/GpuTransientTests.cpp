#include "GpuTestSupport.h"

#include "Render/RenderGraph.h"
#include "Render/TransientPool.h"

#include <ranges>

namespace {

// Mirrors ComputeImageSmoke.slang's ImageParams and BufferHazardSmoke.slang's HazardParams.
struct ImageParams {
    uint32_t extent = 0;
};
struct HazardParams {
    uint32_t bias = 0;
};

constexpr uint32_t kImageThreadsPerGroup = 8;
constexpr uint32_t kHazardThreadsPerGroup = 64;

// The argument-table slots the two smoke modules' globals compile to.
constexpr uint32_t kImageSlot = 0;
constexpr uint32_t kImageSourceSlot = 1;
constexpr uint32_t kImageParamsSlot = 1;
constexpr uint32_t kHazardOutputSlot = 0;
constexpr uint32_t kHazardSourceSlot = 1;
constexpr uint32_t kHazardParamsSlot = 2;

// Storage-capable and identical for every transient here, so lifetime is the only thing that
// decides whether two of them share bytes.
constexpr lmx::render::TransientTextureDesc kStorageImage{.width = kSize,
                                                          .height = kSize,
                                                          .format = lmx::rhi::Format::RGBA8Unorm,
                                                          .sampled = true,
                                                          .storageRead = true,
                                                          .storageWrite = true};

//======================================================================================================================
// The gradient computeWriteImage writes, as the 8-bit channel value at a texel of a square image.
// Inverting an 8-bit unorm twice is exact, so a doubly inverted gradient is the gradient.
uint8_t gradientChannel(uint32_t coordinate) {
    const float value = static_cast<float>(coordinate) / static_cast<float>(kSize - 1);
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

} // namespace

//======================================================================================================================
// Pooling parity, on the picture rather than on the plan: the same frame, declared identically and
// rendered twice, has to read back the same bytes whether or not its transients were allowed to
// share memory. Three transients with one aliasing pair, so the pooled run really does put two
// logical resources in one set of bytes -- and the two runs' plans are asserted to differ, or the
// comparison would be proving nothing.
TEST_CASE("a pooled frame and an unpooled frame render the same image", "[gpu]") {
    using namespace lmx::rhi;
    using namespace lmx::render;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto computeLibrary = (*device)->loadShaderLibrary("Shaders/ComputeImageSmoke");
    INFO(errorOf(computeLibrary));
    REQUIRE(computeLibrary.has_value());

    auto writePipeline = (*device)->createComputePipeline(
        {.library = computeLibrary->get(),
         .computeEntry = "computeWriteImage",
         .threadsPerThreadgroup = {kImageThreadsPerGroup, kImageThreadsPerGroup, 1},
         .label = "lmx.test.transient.writePipeline"});
    INFO(errorOf(writePipeline));
    REQUIRE(writePipeline.has_value());

    auto invertPipeline = (*device)->createComputePipeline(
        {.library = computeLibrary->get(),
         .computeEntry = "computeInvertImage",
         .threadsPerThreadgroup = {kImageThreadsPerGroup, kImageThreadsPerGroup, 1},
         .label = "lmx.test.transient.invertPipeline"});
    INFO(errorOf(invertPipeline));
    REQUIRE(invertPipeline.has_value());

    auto sampleLibrary = (*device)->loadShaderLibrary("Shaders/FullscreenSample");
    INFO(errorOf(sampleLibrary));
    REQUIRE(sampleLibrary.has_value());

    auto samplePipeline =
        (*device)->createGraphicsPipeline({.library = sampleLibrary->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = Format::BGRA8Unorm,
                                           .label = "lmx.test.transient.samplePipeline"});
    INFO(errorOf(samplePipeline));
    REQUIRE(samplePipeline.has_value());

    auto target = makeProbeTarget(**device, "lmx.test.transient.target");
    INFO(errorOf(target));
    REQUIRE(target.has_value());

    const ImageParams params{.extent = kSize};
    const uint32_t groups = kSize / kImageThreadsPerGroup;

    TransientPool pool(**device);

    // One frame, declared the same way both times; only the pooling switch differs.
    const auto renderOnce = [&](bool pooling) {
        CommandList& commands = (*device)->beginFrame();
        pool.beginFrame();

        RenderGraph graph(pool);
        graph.setPoolingEnabled(pooling);
        const GraphTexture first = graph.createTexture(kStorageImage, "lmx.test.transient.first");
        const GraphTexture second = graph.createTexture(kStorageImage, "lmx.test.transient.second");
        const GraphTexture third = graph.createTexture(kStorageImage, "lmx.test.transient.third");
        const GraphTexture color =
            graph.importTexture(**target, Format::BGRA8Unorm, "lmx.test.transient.target");

        ComputePassDesc write;
        write.textureWrites.push_back(first);
        graph.addComputePass(
            "lmx.test.transient.write", write, [&](const PassResources& resources) {
                const GraphResult<Texture*> texture = resources.texture(first);
                REQUIRE(texture.has_value());
                commands.bindComputePipeline(**writePipeline);
                commands.bindStorageTexture(kImageSlot, **texture, {}, StorageAccess::Write);
                commands.setUniforms(kImageParamsSlot, &params, sizeof(params));
                commands.dispatch(groups, groups, 1);
            });

        // Reads the first while writing the second, so those two are live at once and cannot share
        // bytes -- which is what leaves the first's memory free for the third below.
        ComputePassDesc invertFirst;
        invertFirst.textureReads.push_back(nextVersion(first));
        invertFirst.textureWrites.push_back(second);
        graph.addComputePass(
            "lmx.test.transient.invertFirst", invertFirst, [&](const PassResources& resources) {
                const GraphResult<Texture*> source = resources.texture(nextVersion(first));
                const GraphResult<Texture*> destination = resources.texture(second);
                REQUIRE(source.has_value());
                REQUIRE(destination.has_value());
                commands.bindComputePipeline(**invertPipeline);
                commands.bindStorageTexture(kImageSlot, **destination, {}, StorageAccess::Write);
                commands.bindStorageTexture(kImageSourceSlot, **source, {}, StorageAccess::Read);
                commands.setUniforms(kImageParamsSlot, &params, sizeof(params));
                commands.dispatch(groups, groups, 1);
            });

        // The first is dead by now, so with pooling on this writes into its bytes.
        ComputePassDesc invertSecond;
        invertSecond.textureReads.push_back(nextVersion(second));
        invertSecond.textureWrites.push_back(third);
        graph.addComputePass(
            "lmx.test.transient.invertSecond", invertSecond, [&](const PassResources& resources) {
                const GraphResult<Texture*> source = resources.texture(nextVersion(second));
                const GraphResult<Texture*> destination = resources.texture(third);
                REQUIRE(source.has_value());
                REQUIRE(destination.has_value());
                commands.bindComputePipeline(**invertPipeline);
                commands.bindStorageTexture(kImageSlot, **destination, {}, StorageAccess::Write);
                commands.bindStorageTexture(kImageSourceSlot, **source, {}, StorageAccess::Read);
                commands.setUniforms(kImageParamsSlot, &params, sizeof(params));
                commands.dispatch(groups, groups, 1);
            });

        PassDesc resolve;
        resolve.textureReads.push_back(nextVersion(third));
        resolve.color = ColorAttachment{.handle = color, .clearColor = {1.0f, 0.0f, 1.0f, 1.0f}};
        graph.addPass("lmx.test.transient.resolve", resolve, [&](const PassResources& resources) {
            const GraphResult<Texture*> texture = resources.texture(nextVersion(third));
            REQUIRE(texture.has_value());
            commands.bindPipeline(**samplePipeline);
            commands.bindTexture(0, **texture);
            commands.draw(3);
        });

        graph.readbackTexture(nextVersion(color));

        const CompiledFrameRecord record = graph.execute(commands, (*device)->frameNumber());
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();

        std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
        (*target)->readback(pixels.data(), pixels.size());
        return std::pair{record, pixels};
    };

    const auto [pooledRecord, pooledPixels] = renderOnce(true);
    const auto [unpooledRecord, unpooledPixels] = renderOnce(false);

    // The two runs planned different memory, so the comparison below is comparing two different
    // physical arrangements of the same frame rather than the same one twice.
    REQUIRE(pooledRecord.debug.transients[2].aliases);
    REQUIRE(pooledRecord.debug.memory.aliasSavings > 0);
    REQUIRE_FALSE(unpooledRecord.debug.transients[2].aliases);
    REQUIRE(unpooledRecord.debug.memory.aliasSavings == 0);
    REQUIRE(unpooledRecord.debug.memory.highWater > pooledRecord.debug.memory.highWater);

    REQUIRE(pooledPixels == unpooledPixels);

    // And the picture is the one the frame describes rather than two identical failures: inverting
    // an 8-bit gradient twice returns it.
    const std::array<std::pair<uint32_t, uint32_t>, 3> probes = {
        {{0, 0}, {kSize - 1, 0}, {21, 42}}};
    for (const auto& [x, y] : probes) {
        const Pixel texel = pixelAt(pooledPixels, x, y);
        INFO(describe("pooled", x, y, texel));
        REQUIRE(channelNear(texel.r, gradientChannel(x), 1));
        REQUIRE(channelNear(texel.g, gradientChannel(y), 1));
        REQUIRE(channelNear(texel.b, 64, 1));
    }
}

//======================================================================================================================
// The reuse boundary itself, with a sentinel on both sides. The second transient takes the first's
// bytes and is filled with a different pattern; if the barrier the graph derives did not order that
// fill after the first transient's last read, the first result would come back carrying the second
// one's fill. Both readbacks are checked, so a bleed in either direction is visible.
TEST_CASE("a transient cannot read what the transient it replaced left behind",
          "[gpu][checkpoint-a]") {
    using namespace lmx::rhi;
    using namespace lmx::render;

    constexpr uint32_t kElements = 256;
    constexpr uint32_t kBias = 7;
    constexpr uint8_t kFirstFill = 0xAA;
    constexpr uint8_t kSecondFill = 0x11;
    constexpr uint64_t kBytes = sizeof(uint32_t) * kElements;
    constexpr TransientBufferDesc kBins{.size = kBytes, .storageRead = true};

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
                                          .label = "lmx.test.transient.hazardPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    const auto makeOutput = [&](const char* label) {
        return (*device)->createBuffer(
            {.size = kBytes, .storageWrite = true, .cpuReadback = true, .label = label}, nullptr);
    };
    auto firstOut = makeOutput("lmx.test.transient.firstOut");
    auto secondOut = makeOutput("lmx.test.transient.secondOut");
    INFO(errorOf(firstOut));
    REQUIRE(firstOut.has_value());
    INFO(errorOf(secondOut));
    REQUIRE(secondOut.has_value());

    const HazardParams params{.bias = kBias};
    TransientPool pool(**device);

    CommandList& commands = (*device)->beginFrame();
    pool.beginFrame();

    RenderGraph graph(pool);
    const GraphBuffer first = graph.createBuffer(kBins, "lmx.test.transient.firstBins");
    const GraphBuffer second = graph.createBuffer(kBins, "lmx.test.transient.secondBins");
    const GraphBuffer firstResult = graph.importBuffer(**firstOut, "lmx.test.transient.firstOut");
    const GraphBuffer secondResult =
        graph.importBuffer(**secondOut, "lmx.test.transient.secondOut");

    const auto declareFill = [&](std::string_view label, GraphBuffer bins, uint8_t value) {
        CopyPassDesc fill;
        fill.bufferDestinations.push_back(bins);
        graph.addCopyPass(label, fill, [&, bins, value](const PassResources& resources) {
            const GraphResult<Buffer*> buffer = resources.buffer(bins);
            REQUIRE(buffer.has_value());
            commands.fillBuffer(**buffer, 0, kBytes, value);
        });
    };
    const auto declareAdd = [&](std::string_view label, GraphBuffer bins, GraphBuffer out) {
        ComputePassDesc add;
        add.bufferReads.push_back(nextVersion(bins));
        add.bufferWrites.push_back(out);
        graph.addComputePass(label, add, [&, bins, out](const PassResources& resources) {
            const GraphResult<Buffer*> source = resources.buffer(nextVersion(bins));
            const GraphResult<Buffer*> destination = resources.buffer(out);
            REQUIRE(source.has_value());
            REQUIRE(destination.has_value());
            commands.bindComputePipeline(**pipeline);
            commands.bindStorageBuffer(kHazardOutputSlot, **destination, StorageAccess::Write);
            commands.bindStorageBuffer(kHazardSourceSlot, **source, StorageAccess::Read);
            commands.setUniforms(kHazardParamsSlot, &params, sizeof(params));
            commands.dispatch(kElements / kHazardThreadsPerGroup, 1, 1);
        });
    };

    declareFill("lmx.test.transient.fillFirst", first, kFirstFill);
    declareAdd("lmx.test.transient.addFirst", first, firstResult);
    declareFill("lmx.test.transient.fillSecond", second, kSecondFill);
    declareAdd("lmx.test.transient.addSecond", second, secondResult);

    graph.readbackBuffer(nextVersion(firstResult));
    graph.readbackBuffer(nextVersion(secondResult));

    const CompiledFrameRecord record = graph.execute(commands, (*device)->frameNumber());
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    // The premise: the two transients really are one set of bytes, and the graph really did put a
    // barrier at the handover.
    REQUIRE(record.debug.transients[1].aliases);
    REQUIRE(record.debug.transients[1].offset == record.debug.transients[0].offset);
    const auto reuse =
        std::ranges::find_if(record.debug.transitions, [](const DebugTransition& transition) {
            return transition.aliasedFrom.has_value();
        });
    REQUIRE(reuse != record.debug.transitions.end());

    const auto readValues = [](Buffer& buffer) {
        std::vector<uint32_t> values(kElements, 0);
        buffer.readback(values.data(), values.size() * sizeof(uint32_t));
        return values;
    };
    const std::vector<uint32_t> firstValues = readValues(**firstOut);
    const std::vector<uint32_t> secondValues = readValues(**secondOut);

    constexpr uint32_t kFirstElement = 0xAAAAAAAA + kBias;
    constexpr uint32_t kSecondElement = 0x11111111 + kBias;
    for (uint32_t index = 0; index < kElements; ++index) {
        INFO("element " + std::to_string(index));
        REQUIRE(firstValues[index] == kFirstElement);
        REQUIRE(secondValues[index] == kSecondElement);
    }
}

//======================================================================================================================
// Resize and feature toggles across the three-frame pipeline, which is what the pool's whole
// generation rule exists for. Every cycle changes the transient extent and flips pooling, and every
// cycle runs a full pipeline's worth of frames so each of the three slots sees the new shape.
//
// What is asserted is the accounting rather than a picture: the heap is exactly the compiled
// high-water mark, the number of live generations never grows past one per slot plus one retiring
// per slot, and a run that settles releases every retired generation. Metal validation is what
// covers the other half -- a generation released while a frame still reads it would fault here.
TEST_CASE("resizing and toggling transients leaks no heap generation", "[gpu][checkpoint-a]") {
    using namespace lmx::rhi;
    using namespace lmx::render;

    constexpr uint32_t kCycles = 4;
    constexpr std::array<uint32_t, kCycles> kExtents = {32, 64, 128, 64};

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto computeLibrary = (*device)->loadShaderLibrary("Shaders/ComputeImageSmoke");
    INFO(errorOf(computeLibrary));
    REQUIRE(computeLibrary.has_value());

    auto writePipeline = (*device)->createComputePipeline(
        {.library = computeLibrary->get(),
         .computeEntry = "computeWriteImage",
         .threadsPerThreadgroup = {kImageThreadsPerGroup, kImageThreadsPerGroup, 1},
         .label = "lmx.test.transient.stressPipeline"});
    INFO(errorOf(writePipeline));
    REQUIRE(writePipeline.has_value());

    auto sampleLibrary = (*device)->loadShaderLibrary("Shaders/FullscreenSample");
    INFO(errorOf(sampleLibrary));
    REQUIRE(sampleLibrary.has_value());

    auto samplePipeline =
        (*device)->createGraphicsPipeline({.library = sampleLibrary->get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = Format::BGRA8Unorm,
                                           .label = "lmx.test.transient.stressSamplePipeline"});
    INFO(errorOf(samplePipeline));
    REQUIRE(samplePipeline.has_value());

    auto firstTarget = makeProbeTarget(**device, "lmx.test.transient.stressFirst");
    auto secondTarget = makeProbeTarget(**device, "lmx.test.transient.stressSecond");
    INFO(errorOf(firstTarget));
    REQUIRE(firstTarget.has_value());
    INFO(errorOf(secondTarget));
    REQUIRE(secondTarget.has_value());

    TransientPool pool(**device);

    // Two transients whose lifetimes do not overlap, at whatever extent this cycle asked for.
    const auto renderFrame = [&](uint32_t extent, bool pooling) {
        const ImageParams params{.extent = extent};
        const uint32_t groups = extent / kImageThreadsPerGroup;
        const TransientTextureDesc desc{.width = extent,
                                        .height = extent,
                                        .format = Format::RGBA8Unorm,
                                        .sampled = true,
                                        .storageWrite = true};

        CommandList& commands = (*device)->beginFrame();
        pool.beginFrame();

        RenderGraph graph(pool);
        graph.setPoolingEnabled(pooling);
        const GraphTexture first = graph.createTexture(desc, "lmx.test.transient.stressFirst");
        const GraphTexture second = graph.createTexture(desc, "lmx.test.transient.stressSecond");
        const GraphTexture firstColor =
            graph.importTexture(**firstTarget, Format::BGRA8Unorm, "lmx.test.transient.firstColor");
        const GraphTexture secondColor = graph.importTexture(**secondTarget, Format::BGRA8Unorm,
                                                             "lmx.test.transient.secondColor");

        const auto declarePair = [&](std::string_view writeLabel, std::string_view resolveLabel,
                                     GraphTexture storage, GraphTexture color) {
            ComputePassDesc write;
            write.textureWrites.push_back(storage);
            graph.addComputePass(writeLabel, write, [&, storage](const PassResources& resources) {
                const GraphResult<Texture*> texture = resources.texture(storage);
                REQUIRE(texture.has_value());
                commands.bindComputePipeline(**writePipeline);
                commands.bindStorageTexture(kImageSlot, **texture, {}, StorageAccess::Write);
                commands.setUniforms(kImageParamsSlot, &params, sizeof(params));
                commands.dispatch(groups, groups, 1);
            });

            PassDesc resolve;
            resolve.textureReads.push_back(nextVersion(storage));
            resolve.color = ColorAttachment{.handle = color};
            graph.addPass(resolveLabel, resolve, [&, storage](const PassResources& resources) {
                const GraphResult<Texture*> texture = resources.texture(nextVersion(storage));
                REQUIRE(texture.has_value());
                commands.bindPipeline(**samplePipeline);
                commands.bindTexture(0, **texture);
                commands.draw(3);
            });
            graph.exportTexture(nextVersion(color));
        };

        declarePair("lmx.test.transient.stressWriteFirst", "lmx.test.transient.stressResolveFirst",
                    first, firstColor);
        declarePair("lmx.test.transient.stressWriteSecond",
                    "lmx.test.transient.stressResolveSecond", second, secondColor);

        const CompiledFrameRecord record = graph.execute(commands, (*device)->frameNumber());
        (*device)->endFrame(nullptr);
        return record;
    };

    for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
        const bool pooling = cycle % 2 == 0;
        for (uint32_t frame = 0; frame < kTransientFrameSlots; ++frame) {
            INFO("cycle " + std::to_string(cycle) + " frame " + std::to_string(frame));
            const CompiledFrameRecord record = renderFrame(kExtents[cycle], pooling);

            // The heap the pool holds is exactly what the plan asked for, so a shape change really
            // did resize it rather than leaving the frame in a heap sized for something else.
            REQUIRE(pool.heapBytes() == record.debug.memory.highWater);
            REQUIRE(record.debug.memory.highWater > 0);
            REQUIRE(record.debug.transients[1].aliases == pooling);
            REQUIRE((record.debug.memory.aliasSavings > 0) == pooling);

            // One live generation per slot, plus at most one retiring per slot: anything above
            // that is a generation nothing ever released.
            REQUIRE(pool.liveGenerationCount() <= 2 * kTransientFrameSlots);
        }
    }

    // A settled run: every generation retired by the cycles above is released once the frames that
    // could still have been reading it have gone by.
    for (uint32_t frame = 0; frame < 2 * kTransientFrameSlots; ++frame) {
        renderFrame(kExtents.back(), true);
    }
    (*device)->waitIdle();

    REQUIRE(pool.retiringGenerationCount() == 0);
    REQUIRE(pool.liveGenerationCount() == kTransientFrameSlots);
}
