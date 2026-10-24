#include "Support/GpuTestSupport.h"

#include "App/Model/Graph/FrameRecordRing.h"
#include "Render/Graph/RenderGraph.h"

#include <cstring>

namespace {

constexpr uint32_t kHazardThreadsPerGroup = 64;
constexpr uint32_t kImageThreadsPerGroup = 8;

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
    using namespace rojoRHI;
    using namespace lmx::render;

    constexpr uint32_t kElements = 256;
    constexpr uint32_t kBias = 7;
    // Every byte of the fill is this value, so each uint32 element reads back as 0x03030303.
    constexpr uint8_t kFillValue = 0x03;
    constexpr uint32_t kFilledElement = 0x03030303;

    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);

    auto library = gpu->loadShaderLibrary("BufferHazardSmoke");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto pipeline = lmx::engine::adoptGpu(
        gpu->device(), gpu->device().createPipeline(
                           ComputePipelineDesc{.compute = {library->get(), "computeAddFromBuffer"},
                                               .label = "lmx.test.graph.hazardPipeline"}));
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    const std::vector<uint32_t> sentinel(kElements, 0xFFFFFFFFu);
    auto accumulator = lmx::engine::GpuBuffer::create(gpu->device(),
                                                      {.size = sizeof(uint32_t) * kElements,
                                                       .usage = BufferUsage::ShaderWrite,
                                                       .label = "lmx.test.graph.accumulator"},
                                                      std::as_bytes(std::span(sentinel)));
    INFO(errorOf(accumulator));
    REQUIRE(accumulator.has_value());

    auto resolved =
        lmx::engine::GpuBuffer::create(gpu->device(), {.size = sizeof(uint32_t) * kElements,
                                                       .usage = BufferUsage::ShaderWrite,
                                                       .label = "lmx.test.graph.resolved"});
    INFO(errorOf(resolved));
    REQUIRE(resolved.has_value());

    auto commandsFrame = gpu->beginFrame();
    auto& commands = commandsFrame.commands;

    RenderGraph graph;
    const GraphBuffer bins = graph.importBuffer(*accumulator, "lmx.test.graph.accumulator");
    const GraphBuffer output = graph.importBuffer(*resolved, "lmx.test.graph.resolved");

    CopyPassDesc clear;
    clear.bufferDestinations.push_back(bins);
    graph.addCopyPass("lmx.test.graph.clear", clear, [&](const PassResources& resources) {
        const Buffer buffer = resources.buffer(bins);
        REQUIRE(buffer.bits != 0);
        commands.fillBuffer(buffer, 0, sizeof(uint32_t) * kElements,
                            uint32_t{kFillValue} * 0x01010101u);
    });

    ComputePassDesc accumulate;
    accumulate.bufferReads.push_back(nextVersion(bins));
    accumulate.bufferWrites.push_back(output);
    graph.addComputePass(
        "lmx.test.graph.accumulate", accumulate, [&](const PassResources& resources) {
            const Buffer source = resources.buffer(nextVersion(bins));
            REQUIRE(source.bits != 0);
            const Buffer destination = resources.buffer(output);
            REQUIRE(destination.bits != 0);

            commands.setPipeline(pipeline->get());
            commands.pushRoot(
                lmx::test::BufferHazardRoot{.output = resources.address(output),
                                            .source = resources.address(nextVersion(bins)),
                                            .bias = kBias});
#ifndef NDEBUG
            const std::array accesses{ResourceAccess{.buffer = source,
                                                     .size = sizeof(uint32_t) * kElements,
                                                     .stage = Stage::ComputeShader,
                                                     .access = Access::ShaderRead},
                                      ResourceAccess{.buffer = destination,
                                                     .size = sizeof(uint32_t) * kElements,
                                                     .stage = Stage::ComputeShader,
                                                     .access = Access::ShaderWrite}};
            commands.declareAccess(accesses);
#endif
            commands.dispatch(kElements / kHazardThreadsPerGroup, 1, 1);
        });

    // The readback below is the frame's whole point, and declaring it is what keeps the two passes
    // that produce it out of the cull.
    graph.readbackBuffer(nextVersion(output));

    graph.execute(commands, gpu->frameNumber());
    gpu->submitAndEndFrame(commandsFrame);
    gpu->waitIdle();

    std::vector<uint32_t> values(kElements, 0);
    {
        const auto readbackBytes = lmx::render::readbackNow(
            *gpu, *resolved, {Stage::ComputeShader, Access::ShaderWrite, Layout::Undefined},
            {0, values.size() * sizeof(uint32_t)});
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes);
        std::memcpy(values.data(), readbackBytes->data(), values.size() * sizeof(uint32_t));
    }

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
    using namespace rojoRHI;
    using namespace lmx::render;

    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);

    auto computeLibrary = gpu->loadShaderLibrary("ComputeImageSmoke");
    INFO(errorOf(computeLibrary));
    REQUIRE(computeLibrary.has_value());

    auto computePipeline = lmx::engine::adoptGpu(
        gpu->device(), gpu->device().createPipeline(ComputePipelineDesc{
                           .compute = {computeLibrary->get(), "computeWriteImage"},
                           .label = "lmx.test.graph.imagePipeline"}));
    INFO(errorOf(computePipeline));
    REQUIRE(computePipeline.has_value());

    auto sampleLibrary = gpu->loadShaderLibrary("FullscreenSample");
    INFO(errorOf(sampleLibrary));
    REQUIRE(sampleLibrary.has_value());

    const ColorTarget samplePipelineColor{.format = Format::BGRA8Unorm};
    auto samplePipeline =
        lmx::engine::adoptGpu(gpu->device(), gpu->device().createPipeline(GraphicsPipelineDesc{
                                                 .vertex = {sampleLibrary->get(), "vertexMain"},
                                                 .fragment = {sampleLibrary->get(), "fragmentMain"},
                                                 .raster = {.cull = CullMode::Back},
                                                 .depthStencil = {.depthCompare = CompareOp::Less},
                                                 .colors = std::span(&samplePipelineColor, 1),
                                                 .label = "lmx.test.graph.samplePipeline"}));
    INFO(errorOf(samplePipeline));
    REQUIRE(samplePipeline.has_value());

    auto image =
        lmx::engine::GpuTexture::create(gpu->device(),
                                        {.format = Format::RGBA8Unorm,
                                         .extent = {kSize, kSize, 1},
                                         .usage = TextureUsage::Sampled | TextureUsage::Storage,
                                         .label = "lmx.test.graph.image"},
                                        {}, {.storageMips = true});
    INFO(errorOf(image));
    REQUIRE(image.has_value());

    auto target = makeProbeTarget(*gpu, "lmx.test.graph.target");
    INFO(errorOf(target));
    REQUIRE(target.has_value());

    const uint32_t groups = kSize / kImageThreadsPerGroup;

    auto commandsFrame = gpu->beginFrame();
    auto& commands = commandsFrame.commands;

    TextureStateTable states;
    RenderGraph graph;
    graph.bindTextureStates(states);
    const GraphTexture storage = graph.importPersistentTexture(*image, "lmx.test.graph.image");
    const GraphTexture color = graph.importPersistentTexture(*target, "lmx.test.graph.target");

    ComputePassDesc write;
    write.textureWrites.push_back(storage);
    graph.addComputePass("lmx.test.graph.write", write, [&](const PassResources& resources) {
        const auto texture = resources.texture(storage);
        REQUIRE(texture.texture.bits != 0);
        commands.setPipeline(computePipeline->get());
        commands.pushRoot(lmx::test::ComputeImageRoot{
            .image = static_cast<uint32_t>(resources.storageView(storage, 0)), .extent = kSize});
#ifndef NDEBUG
        const ResourceAccess access{.texture = texture.texture,
                                    .range = {0, 1, 0, 1},
                                    .stage = Stage::ComputeShader,
                                    .access = Access::ShaderWrite};
        commands.declareAccess(std::span(&access, 1));
#endif
        commands.dispatch(groups, groups, 1);
    });

    PassDesc draw;
    draw.textureReads.push_back(nextVersion(storage));
    draw.color =
        lmx::render::ColorAttachment{.handle = color, .clearColor = {1.0f, 0.0f, 1.0f, 1.0f}};
    graph.addPass("lmx.test.graph.sample", draw, [&](const PassResources& resources) {
        const auto texture = resources.texture(nextVersion(storage));
        REQUIRE(texture.texture.bits != 0);
        commands.setPipeline(samplePipeline->get());
        commands.pushRoot(lmx::test::FullscreenSampleRoot{
            .source = static_cast<uint32_t>(resources.view(nextVersion(storage)))});
#ifndef NDEBUG
        const ResourceAccess access{.texture = texture.texture,
                                    .range = {0, 1, 0, 1},
                                    .stage = Stage::FragmentShader,
                                    .access = Access::ShaderRead};
        commands.declareAccess(std::span(&access, 1));
#endif
        commands.draw(3);
    });

    graph.readbackTexture(nextVersion(color));

    graph.execute(commands, gpu->frameNumber());
    gpu->submitAndEndFrame(commandsFrame);
    gpu->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    {
        const auto readbackBytes = lmx::render::readbackNow(*gpu, *target, states);
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes);
        std::memcpy(pixels.data(), readbackBytes->data(), pixels.size());
    }

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
    using namespace rojoRHI;
    using namespace lmx::render;

    constexpr uint32_t kFrames = 6;
    constexpr uint64_t kFillBytes = 256;

    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);

    auto storage =
        lmx::engine::GpuBuffer::create(gpu->device(), {.size = kFillBytes,
                                                       .usage = BufferUsage::ShaderWrite,
                                                       .label = "lmx.test.graph.retained"});
    INFO(errorOf(storage));
    REQUIRE(storage.has_value());

    lmx::app::FrameRecordRing records;
    uint64_t measured = 0;
    const auto joinRetired = [&] {
        const auto published = gpu->passTimings();
        if (published.frame != 0) {
            REQUIRE(records.joinTimings(published.frame, published.passes));
            measured = published.frame;
        }
    };

    const auto declareFrame = [&] {
        auto commandsFrame = gpu->beginFrame();
        auto& commands = commandsFrame.commands;
        RenderGraph graph;
        const GraphBuffer target = graph.importBuffer(*storage, "lmx.test.graph.retained");

        CopyPassDesc clear;
        clear.bufferDestinations.push_back(target);
        graph.addCopyPass("lmx.test.graph.retainedClear", clear,
                          [&](const PassResources& resources) {
                              const Buffer buffer = resources.buffer(target);
                              REQUIRE(buffer.bits != 0);
                              commands.fillBuffer(buffer, 0, kFillBytes, 0);
                          });
        graph.exportBuffer(nextVersion(target));

        records.retain(graph.execute(commands, gpu->frameNumber()));
        gpu->submitAndEndFrame(commandsFrame);
        joinRetired();
    };

    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        declareFrame();
    }

    // Each empty frame publishes one more retired slot; consume it before the next endFrame.
    gpu->waitIdle();
    for (uint32_t slot = 0; slot < 3; ++slot) {
        auto emptyFrame = gpu->beginFrame();
        gpu->submitAndEndFrame(emptyFrame);
        joinRetired();
    }
    gpu->waitIdle();
    REQUIRE(measured == kFrames);

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
    using namespace rojoRHI;
    using namespace lmx::render;

    // The two halves MrtSmoke.slang writes: 0.25 is 2^-2 and -0.5 is 2^-1, both exact in half
    // precision, so the readback is compared bit for bit.
    constexpr uint16_t kWrittenMotionX = 0x3400;
    constexpr uint16_t kWrittenMotionY = 0xB800;

    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);

    auto color = makeProbeTarget(*gpu, "lmx.test.graph.mrtColor");
    INFO(errorOf(color));
    REQUIRE(color.has_value());

    auto motion =
        lmx::engine::GpuTexture::create(gpu->device(), {.format = Format::RG16Float,
                                                        .extent = {kSize, kSize, 1},
                                                        .usage = TextureUsage::RenderTarget,
                                                        .label = "lmx.test.graph.mrtMotion"});
    INFO(errorOf(motion));
    REQUIRE(motion.has_value());

    auto library = gpu->loadShaderLibrary("MrtSmoke");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    const std::array colors{ColorTarget{.format = Format::BGRA8Unorm},
                            ColorTarget{.format = Format::RG16Float}};
    auto pipeline =
        lmx::engine::adoptGpu(gpu->device(), gpu->device().createPipeline(GraphicsPipelineDesc{
                                                 .vertex = {library->get(), "vertexMain"},
                                                 .fragment = {library->get(), "fragmentMain"},
                                                 .raster = {.cull = CullMode::Back},
                                                 .depthStencil = {.depthCompare = CompareOp::Less},
                                                 .colors = colors,
                                                 .label = "lmx.test.graph.mrtPipeline"}));
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    auto commandsFrame = gpu->beginFrame();
    auto& commands = commandsFrame.commands;

    TextureStateTable states;
    RenderGraph graph;
    graph.bindTextureStates(states);
    const GraphTexture sceneColor =
        graph.importPersistentTexture(*color, "lmx.test.graph.mrtColor");
    const GraphTexture motionVectors =
        graph.importPersistentTexture(*motion, "lmx.test.graph.mrtMotion");

    PassDesc scene;
    scene.color =
        lmx::render::ColorAttachment{.handle = sceneColor, .clearColor = {0.0f, 1.0f, 0.0f, 1.0f}};
    scene.extraColor.push_back(lmx::render::ColorAttachment{.handle = motionVectors});
    graph.addPass("lmx.test.graph.mrt", scene, [&](const PassResources&) {
        commands.setPipeline(pipeline->get());
        commands.draw(3);
    });

    // The extra's version is the only sink, so it is also what keeps the pass out of the cull.
    graph.readbackTexture(nextVersion(motionVectors));

    graph.execute(commands, gpu->frameNumber());
    gpu->submitAndEndFrame(commandsFrame);
    gpu->waitIdle();

    std::vector<uint8_t> colorPixels(size_t{kSize} * kSize * 4);
    {
        const auto readbackBytes = lmx::render::readbackNow(*gpu, *color, states);
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes);
        std::memcpy(colorPixels.data(), readbackBytes->data(), colorPixels.size());
    }
    std::vector<uint8_t> motionPixels(size_t{kSize} * kSize * 2 * sizeof(uint16_t));
    {
        const auto readbackBytes = lmx::render::readbackNow(*gpu, *motion, states);
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes);
        std::memcpy(motionPixels.data(), readbackBytes->data(), motionPixels.size());
    }

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
