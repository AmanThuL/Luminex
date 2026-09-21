#include "GpuTestSupport.h"

#include "Core/Math/Scalar.h"
#include "Render/HzbStage.h"
#include "Render/Occlusion.h"

#include <algorithm>
#include <bit>
#include <format>

namespace {
struct HzbProbeBounds {
    glm::vec4 minimum;
    glm::vec4 maximum;
};
static_assert(sizeof(HzbProbeBounds) == 32);
struct HzbReadParams {
    uint32_t width;
    uint32_t height;
    uint32_t level;
    uint32_t offset;
};
static_assert(sizeof(HzbReadParams) == 16);

//======================================================================================================================
std::vector<lmx::Aabb> actualHzbBounds() {
    using lmx::Aabb;
    std::vector<Aabb> result;
    for (const float x : {-0.75f, -0.4f, 0.0f, 0.3f, 0.7f}) {
        for (const float y : {-0.6f, -0.1f, 0.5f}) {
            for (const float radius : {0.005f, 0.035f, 0.125f, 0.3f}) {
                for (const float depth : {0.0001f, 0.2f, 0.95f}) {
                    result.push_back(
                        {{x - radius, y - radius, depth * 0.5f}, {x + radius, y + radius, depth}});
                }
            }
        }
    }
    result.push_back({{-0.01f, -0.01f, 0.9f}, {0.01f, 0.01f, 1.1f}});
    result.push_back({{-0.9f, -0.9f, 0.1f}, {0.9f, 0.9f, 0.2f}});
    return result;
}
} // namespace

//======================================================================================================================
TEST_CASE("occlusion GPU results equal the CPU mirror on actual raster-built read-back HZB levels",
          "[gpu][hzb][occlusion-probe][hzb-occlusion]") {
    using namespace lmx;
    using namespace lmx::render;
    using namespace rojoRHI;
    constexpr uint32_t outputWidth = 129;
    constexpr uint32_t outputHeight = 75;
    auto device = createDevice();
    REQUIRE(device);
    auto stage = HzbStage::create(**device);
    REQUIRE(stage);
    REQUIRE((*stage)->resize(outputWidth, outputHeight));
    auto depth = (*device)->createTexture({.width = outputWidth,
                                           .height = outputHeight,
                                           .format = Format::D32Float,
                                           .renderTarget = true,
                                           .sampled = true,
                                           .label = "lmx.test.hzbParity.depth"});
    REQUIRE(depth);
    auto rasterLibrary = (*device)->loadShaderLibrary("Shaders/HzbDepthFixture");
    auto readLibrary = (*device)->loadShaderLibrary("Shaders/HzbReadbackFixture");
    auto probeLibrary = (*device)->loadShaderLibrary("Shaders/OcclusionProbe");
    REQUIRE(rasterLibrary);
    REQUIRE(readLibrary);
    REQUIRE(probeLibrary);
    auto raster = (*device)->createGraphicsPipeline({.library = rasterLibrary->get(),
                                                     .vertexEntry = "vertexMain",
                                                     .fragmentEntry = "fragmentMain",
                                                     .colorFormat = Format::Unknown,
                                                     .depthFormat = Format::D32Float,
                                                     .depthTestEnable = true,
                                                     .depthWriteEnable = true,
                                                     .cullMode = CullMode::None,
                                                     .depthCompare = DepthCompare::Greater,
                                                     .label = "lmx.test.hzbParity.raster"});
    auto reader = (*device)->createComputePipeline({.library = readLibrary->get(),
                                                    .computeEntry = "computeMain",
                                                    .threadsPerThreadgroup = {8, 8, 1},
                                                    .label = "lmx.test.hzbParity.reader"});
    auto probe = (*device)->createComputePipeline({.library = probeLibrary->get(),
                                                   .computeEntry = "computeMain",
                                                   .threadsPerThreadgroup = {1, 1, 1},
                                                   .label = "lmx.test.hzbParity.probe"});
    REQUIRE(raster);
    REQUIRE(reader);
    REQUIRE(probe);
    const auto bounds = actualHzbBounds();
    std::vector<HzbProbeBounds> inputData;
    for (const auto& box : bounds)
        inputData.push_back({glm::vec4(box.minimum, 0), glm::vec4(box.maximum, 0)});
    auto input = (*device)->createBuffer(
        {.size = inputData.size() * sizeof(HzbProbeBounds), .label = "lmx.test.hzbParity.bounds"},
        inputData.data());
    REQUIRE(input);
    const uint32_t levelCount = (*stage)->layout().levelCount;
    uint32_t frame = 0;
    for (const bool half : {false, true}) {
        for (const uint32_t masked : {0u, 1u}) {
            const uint32_t width = half ? divRoundUp(outputWidth, 2u) : outputWidth;
            const uint32_t height = half ? divRoundUp(outputHeight, 2u) : outputHeight;
            CAPTURE(width, height, masked);
            std::vector<HzbReadParams> readParams;
            uint32_t readCount = 0;
            for (uint32_t level = 0; level < levelCount; ++level) {
                const HzbReadParams params{hzbLevelExtent(width, level),
                                           hzbLevelExtent(height, level), level, readCount};
                readParams.push_back(params);
                readCount += params.width * params.height;
            }
            auto readback = (*device)->createBuffer({.size = readCount * sizeof(float),
                                                     .storageWrite = true,
                                                     .cpuReadback = true,
                                                     .label = "lmx.test.hzbParity.readback"},
                                                    nullptr);
            auto output = (*device)->createBuffer({.size = bounds.size() * 8 * sizeof(uint32_t),
                                                   .storageWrite = true,
                                                   .cpuReadback = true,
                                                   .label = "lmx.test.hzbParity.output"},
                                                  nullptr);
            REQUIRE(readback);
            REQUIRE(output);
            auto& commands = (*device)->beginFrame();
            RenderGraph graph;
            const auto depthInput =
                frame == 0
                    ? graph.importTexture(**depth, Format::D32Float, "lmx.test.hzbParity.depth")
                    : graph.importTexture(**depth, Format::D32Float, "lmx.test.hzbParity.depth",
                                          TextureUse::ShaderRead);
            PassDesc rasterDesc;
            rasterDesc.depth = DepthAttachment{.handle = depthInput, .store = StoreOp::Store};
            rasterDesc.renderAreaWidth = width;
            rasterDesc.renderAreaHeight = height;
            graph.addPass("lmx.test.hzbParity.raster", std::move(rasterDesc),
                          [&](const PassResources&) {
                              commands.bindPipeline(**raster);
                              commands.bindFrameData(0, masked);
                              commands.draw(3);
                          });
            const auto pyramid = (*stage)->build(graph, commands, nextVersion(depthInput),
                                                 {.frameNumber = (*device)->frameNumber(),
                                                  .activeWidth = width,
                                                  .activeHeight = height});
            auto buffer = graph.importBuffer(**readback, "lmx.test.hzbParity.readback");
            for (const auto params : readParams) {
                ComputePassDesc desc;
                desc.shaderTextureReads.emplace_back(
                    pyramid,
                    TextureSubresourceRange{.baseMipLevel = params.level, .mipLevelCount = 1});
                desc.bufferWrites.push_back(buffer);
                graph.addComputePass(
                    std::format("lmx.test.hzbParity.read{}", params.level), std::move(desc),
                    [&, params, buffer](const PassResources& resources) {
                        const auto texture = resources.texture(pyramid);
                        const auto destination = resources.buffer(buffer);
                        REQUIRE(texture);
                        REQUIRE(destination);
                        commands.bindComputePipeline(**reader);
                        commands.bindTexture(0, **texture);
                        commands.bindStorageBuffer(1, **destination, StorageAccess::Write);
                        commands.bindFrameData(0, params);
                        commands.dispatch(divRoundUp(params.width, 8u),
                                          divRoundUp(params.height, 8u), 1);
                    });
                buffer = nextVersion(buffer);
            }
            graph.readbackBuffer(buffer);
            const auto inputHandle = graph.importBuffer(**input, "lmx.test.hzbParity.bounds");
            const auto outputHandle = graph.importBuffer(**output, "lmx.test.hzbParity.output");
            const auto params =
                makeOcclusionParams(glm::mat4(1), width, height, levelCount, true, true);
            ComputePassDesc probeDesc;
            probeDesc.shaderTextureReads.push_back(pyramid);
            probeDesc.shaderBufferReads.push_back(inputHandle);
            probeDesc.bufferWrites.push_back(outputHandle);
            graph.addComputePass("lmx.test.hzbParity.classify", std::move(probeDesc),
                                 [&](const PassResources& resources) {
                                     const auto texture = resources.texture(pyramid);
                                     const auto source = resources.buffer(inputHandle);
                                     const auto destination = resources.buffer(outputHandle);
                                     REQUIRE(texture);
                                     REQUIRE(source);
                                     REQUIRE(destination);
                                     commands.bindComputePipeline(**probe);
                                     commands.bindTexture(0, **texture);
                                     commands.bindBuffer(0, **source);
                                     commands.bindStorageBuffer(1, **destination,
                                                                StorageAccess::Write);
                                     commands.bindFrameData(13, params);
                                     commands.dispatch(static_cast<uint32_t>(bounds.size()), 1, 1);
                                 });
            graph.readbackBuffer(nextVersion(outputHandle));
            const auto compiled = graph.compile();
            INFO((compiled ? "" : compiled.error().message));
            REQUIRE(compiled);
            graph.execute(commands, (*device)->frameNumber());
            (*device)->endFrame(nullptr);
            (*device)->waitIdle();
            std::vector<float> values(readCount);
            (*readback)->readback(values.data(), values.size() * sizeof(float));
            std::vector<uint32_t> actual(bounds.size() * 8);
            (*output)->readback(actual.data(), actual.size() * sizeof(uint32_t));
            std::vector<OcclusionLevel> levels;
            for (const auto read : readParams)
                levels.push_back(
                    {std::span(values).subspan(read.offset, read.width * read.height), read.width});
            uint32_t occluded = 0;
            uint32_t retained = 0;
            for (size_t i = 0; i < bounds.size(); ++i) {
                CAPTURE(i);
                const auto expected = testOcclusionBounds(bounds[i], params, levels);
                const std::array<uint32_t, 8> words{std::bit_cast<uint32_t>(expected.rectangle[0]),
                                                    std::bit_cast<uint32_t>(expected.rectangle[1]),
                                                    std::bit_cast<uint32_t>(expected.rectangle[2]),
                                                    std::bit_cast<uint32_t>(expected.rectangle[3]),
                                                    expected.level,
                                                    std::bit_cast<uint32_t>(expected.zBox),
                                                    static_cast<uint32_t>(expected.outcome),
                                                    expected.occluded ? 1u : 0u};
                for (size_t word = 0; word < words.size(); ++word) {
                    CAPTURE(word);
                    REQUIRE(actual[i * 8 + word] == words[word]);
                }
                occluded += expected.occluded ? 1u : 0u;
                retained +=
                    expected.outcome == OcclusionOutcome::Retained && !expected.occluded ? 1u : 0u;
            }
            REQUIRE(occluded > 0);
            REQUIRE(retained > 0);
            ++frame;
        }
    }
}
