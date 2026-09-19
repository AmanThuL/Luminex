#include "GpuTestSupport.h"

#include "Core/Math.h"
#include "Render/HzbStage.h"

#include <algorithm>
#include <format>

namespace {
struct ReadbackParams {
    uint32_t width;
    uint32_t height;
    uint32_t level;
    uint32_t offset;
};
static_assert(sizeof(ReadbackParams) == 16);

//======================================================================================================================
void validatePyramid(uint32_t outputWidth, uint32_t outputHeight) {
    using namespace lmx;
    using namespace lmx::render;
    auto device = rojoRHI::createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    auto stage = HzbStage::create(**device, true);
    INFO(errorOf(stage));
    REQUIRE(stage);
    REQUIRE((*stage)->resize(outputWidth, outputHeight));
    auto depth = (*device)->createTexture({.width = outputWidth,
                                           .height = outputHeight,
                                           .format = rojoRHI::Format::D32Float,
                                           .renderTarget = true,
                                           .sampled = true,
                                           .label = "lmx.test.hzbDepth"});
    REQUIRE(depth);
    auto depthLibrary = (*device)->loadShaderLibrary("Shaders/HzbDepthFixture");
    REQUIRE(depthLibrary);
    auto raster = (*device)->createGraphicsPipeline({.library = depthLibrary->get(),
                                                     .vertexEntry = "vertexMain",
                                                     .fragmentEntry = "fragmentMain",
                                                     .colorFormat = rojoRHI::Format::Unknown,
                                                     .depthFormat = rojoRHI::Format::D32Float,
                                                     .depthTestEnable = true,
                                                     .depthWriteEnable = true,
                                                     .cullMode = rojoRHI::CullMode::None,
                                                     .depthCompare = rojoRHI::DepthCompare::Greater,
                                                     .label = "lmx.test.hzbDepthPipeline"});
    INFO(errorOf(raster));
    REQUIRE(raster);
    auto readLibrary = (*device)->loadShaderLibrary("Shaders/HzbReadbackFixture");
    REQUIRE(readLibrary);
    auto reader = (*device)->createComputePipeline({.library = readLibrary->get(),
                                                    .computeEntry = "computeMain",
                                                    .threadsPerThreadgroup = {8, 8, 1},
                                                    .label = "lmx.test.hzbReadPipeline"});
    REQUIRE(reader);
    const HzbLayout layout = (*stage)->layout();
    const uint64_t capacity = uint64_t{outputWidth} * outputHeight + layout.bytes / 8;
    auto readback = (*device)->createBuffer({.size = capacity * sizeof(float),
                                             .storageWrite = true,
                                             .cpuReadback = true,
                                             .label = "lmx.test.hzbReadback"},
                                            nullptr);
    REQUIRE(readback);
    std::array<rojoRHI::Texture*, 2> identities{};
    for (uint32_t frame = 0; frame < 6; ++frame) {
        const bool half = frame == 1 || frame == 3;
        const bool sky = frame == 4;
        const uint32_t masked = frame == 2 || frame == 3;
        const uint32_t width = half ? divRoundUp(outputWidth, 2u) : outputWidth;
        const uint32_t height = half ? divRoundUp(outputHeight, 2u) : outputHeight;
        INFO(std::format("output {}x{}, frame {}, active {}x{}, mask {}, sky {}", outputWidth,
                         outputHeight, frame, width, height, masked, sky));
        REQUIRE((*stage)->resize(outputWidth, outputHeight));
        auto& commands = (*device)->beginFrame();
        RenderGraph graph;
        const auto depthInput =
            frame == 0 ? graph.importTexture(**depth, rojoRHI::Format::D32Float, "lmx.test.depth")
                       : graph.importTexture(**depth, rojoRHI::Format::D32Float, "lmx.test.depth",
                                             rojoRHI::TextureUse::ShaderRead);
        PassDesc rasterDesc;
        rasterDesc.depth = DepthAttachment{.handle = depthInput, .store = StoreOp::Store};
        rasterDesc.renderAreaWidth = width;
        rasterDesc.renderAreaHeight = height;
        graph.addPass("lmx.test.hzb.raster", std::move(rasterDesc), [&](const PassResources&) {
            if (!sky) {
                commands.bindPipeline(**raster);
                commands.bindFrameData(0, masked);
                commands.draw(3);
            }
        });
        const auto source = nextVersion(depthInput);
        const auto pyramid =
            (*stage)->build(graph, commands, source,
                            {.frameNumber = frame, .activeWidth = width, .activeHeight = height});
        auto* identity = &(*stage)->previousTexture();
        if (frame < 2)
            identities[frame] = identity;
        else
            REQUIRE(identity == identities[frame % 2]);
        REQUIRE((*stage)->previousSource().frameNumber == frame);
        REQUIRE((*stage)->previousSource().activeWidth == width);
        REQUIRE((*stage)->previousSource().built);
        GraphBuffer buffer = frame == 0 ? graph.importBuffer(**readback, "lmx.test.hzbReadback")
                                        : graph.importBuffer(**readback, "lmx.test.hzbReadback",
                                                             rojoRHI::BufferUse::StorageWrite);
        std::vector<uint32_t> offsets;
        uint32_t offset = 0;
        for (uint32_t input = 0; input <= layout.levelCount; ++input) {
            const bool sourceDepth = input == 0;
            const uint32_t level = sourceDepth ? 0 : input - 1;
            const ReadbackParams params{.width = sourceDepth ? width : hzbLevelExtent(width, level),
                                        .height =
                                            sourceDepth ? height : hzbLevelExtent(height, level),
                                        .level = level,
                                        .offset = offset};
            offsets.push_back(offset);
            offset += params.width * params.height;
            const auto texture = sourceDepth ? source : pyramid;
            ComputePassDesc desc;
            desc.shaderTextureReads.emplace_back(
                texture,
                rojoRHI::TextureSubresourceRange{.baseMipLevel = level, .mipLevelCount = 1});
            desc.bufferWrites.push_back(buffer);
            graph.addComputePass(std::format("lmx.test.hzb.read{}", input), std::move(desc),
                                 [&, params, texture, buffer](const PassResources& resources) {
                                     auto t = resources.texture(texture);
                                     auto b = resources.buffer(buffer);
                                     REQUIRE(t);
                                     REQUIRE(b);
                                     commands.bindComputePipeline(**reader);
                                     commands.bindTexture(0, **t);
                                     commands.bindStorageBuffer(1, **b,
                                                                rojoRHI::StorageAccess::Write);
                                     commands.bindFrameData(0, params);
                                     commands.dispatch(divRoundUp(params.width, 8u),
                                                       divRoundUp(params.height, 8u), 1);
                                 });
            buffer = nextVersion(buffer);
        }
        graph.readbackBuffer(buffer);
        const auto compiled = graph.compile();
        INFO((compiled ? "" : compiled.error().message));
        REQUIRE(compiled);
        graph.execute(commands, frame);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        std::vector<float> values(capacity);
        (*readback)->readback(values.data(), capacity * sizeof(float));
        // The oracle derives each level from rasterized depth, never from another pyramid level.
        for (uint32_t level = 0; level < layout.levelCount; ++level) {
            const uint32_t levelWidth = hzbLevelExtent(width, level);
            const uint32_t levelHeight = hzbLevelExtent(height, level);
            const uint32_t footprint = 1u << (level + 1);
            for (uint32_t y = 0; y < levelHeight; ++y) {
                for (uint32_t x = 0; x < levelWidth; ++x) {
                    float expected = 1.0f;
                    for (uint32_t sy = y * footprint; sy < std::min((y + 1) * footprint, height);
                         ++sy) {
                        for (uint32_t sx = x * footprint; sx < std::min((x + 1) * footprint, width);
                             ++sx) {
                            expected = std::min(expected, values[sy * width + sx]);
                        }
                    }
                    const float actual = values[offsets[level + 1] + y * levelWidth + x];
                    if (actual != expected) {
                        INFO(std::format("level {} texel {},{}", level, x, y));
                        REQUIRE(actual == expected);
                    }
                }
            }
        }
        if (sky)
            REQUIRE(values[0] == 0.0f);
        else
            REQUIRE(std::ranges::any_of(std::span(values).first(width * height),
                                        [](float value) { return value > 0.0f; }));
    }
}
} // namespace

//======================================================================================================================
TEST_CASE("every HZB level exactly matches raster depth including odd edges and scale reuse",
          "[gpu][hzb]") {
    validatePyramid(129, 75);
    validatePyramid(1280, 720);
}
