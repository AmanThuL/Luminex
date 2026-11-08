#include "Support/GpuTestSupport.h"
#include "Support/VisibilityOracleRoots.h"

#include "Core/Math/Scalar.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Render/Common/FrameData.h"
#include "Render/Passes/Occlusion/HzbStage.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace {
using lmx::test::ReadbackParams;
static_assert(sizeof(ReadbackParams) == 16);

//======================================================================================================================
void validatePyramid(uint32_t outputWidth, uint32_t outputHeight) {
    using namespace lmx;
    using namespace lmx::render;
    auto gpu = createTestGpu();
    auto stage = HzbStage::create(*gpu, true);
    INFO(errorOf(stage));
    REQUIRE(stage);
    TextureStateTable states;
    REQUIRE((*stage)->resize(outputWidth, outputHeight, states));
    auto depth =
        engine::GpuTexture::create(gpu->device(), {.format = rojoRHI::Format::D32Float,
                                                   .extent = {outputWidth, outputHeight, 1},
                                                   .usage = rojoRHI::TextureUsage::DepthStencil |
                                                            rojoRHI::TextureUsage::Sampled,
                                                   .label = "lmx.test.hzbDepth"});
    REQUIRE(depth);
    auto depthLibrary = gpu->loadShaderLibrary("HzbDepthFixture");
    REQUIRE(depthLibrary);
    auto raster = engine::adoptGpu(
        gpu->device(), gpu->device().createPipeline(rojoRHI::GraphicsPipelineDesc{
                           .vertex = {depthLibrary->get(), "vertexMain"},
                           .fragment = {depthLibrary->get(), "fragmentMain"},
                           .raster = {.cull = rojoRHI::CullMode::None},
                           .depthStencil = {.depthTest = true,
                                            .depthWrite = true,
                                            .depthCompare = rojoRHI::CompareOp::Greater},
                           .depthFormat = rojoRHI::Format::D32Float,
                           .label = "lmx.test.hzbDepthPipeline"}));
    INFO(errorOf(raster));
    REQUIRE(raster);
    auto readLibrary = gpu->loadShaderLibrary("HzbReadbackFixture");
    REQUIRE(readLibrary);
    auto reader = engine::adoptGpu(
        gpu->device(),
        gpu->device().createPipeline(rojoRHI::ComputePipelineDesc{
            .compute = {readLibrary->get(), "computeMain"}, .label = "lmx.test.hzbReadPipeline"}));
    REQUIRE(reader);
    const HzbLayout layout = (*stage)->layout();
    const uint64_t capacity = uint64_t{outputWidth} * outputHeight + layout.bytes / 8;
    auto readback = lmx::engine::GpuBuffer::create(
        gpu->device(), {.size = capacity * sizeof(float), .label = "lmx.test.hzbReadback"});
    REQUIRE(readback);
    std::array<rojoRHI::Texture, 2> identities{};
    for (uint32_t frame = 0; frame < 6; ++frame) {
        const bool half = frame == 1 || frame == 3;
        const bool sky = frame == 4;
        const uint32_t masked = frame == 2 || frame == 3;
        const uint32_t width = half ? divRoundUp(outputWidth, 2u) : outputWidth;
        const uint32_t height = half ? divRoundUp(outputHeight, 2u) : outputHeight;
        INFO(std::format("output {}x{}, frame {}, active {}x{}, mask {}, sky {}", outputWidth,
                         outputHeight, frame, width, height, masked, sky));
        REQUIRE((*stage)->resize(outputWidth, outputHeight, states));
        auto gpuFrame1 = gpu->beginFrame();
        auto& commands = gpuFrame1.commands;
        RenderGraph graph;
        graph.bindTextureStates(states);
        if (frame > 0) {
            states.setLogicalUse(depth->rhi(), lmx::render::TextureUse::ShaderRead);
        }
        const auto depthInput = frame == 0
                                    ? graph.importTexture(depth->ref(), "lmx.test.depth")
                                    : graph.importPersistentTexture(*depth, "lmx.test.depth");
        PassDesc rasterDesc;
        rasterDesc.depth = lmx::render::DepthAttachment{.handle = depthInput,
                                                        .store = lmx::render::StoreOp::Store};
        rasterDesc.renderAreaWidth = width;
        rasterDesc.renderAreaHeight = height;
        graph.addPass("lmx.test.hzb.raster", std::move(rasterDesc), [&](const PassResources&) {
            if (!sky) {
                commands.setPipeline(raster->get());
                commands.pushRoot(lmx::test::HzbDepthRoot{masked, 0, 0, 0});
                commands.draw(3);
            }
        });
        const auto source = nextVersion(depthInput);
        const auto pyramid =
            (*stage)->declare(graph, commands, source,
                              {.frameNumber = frame, .activeWidth = width, .activeHeight = height});
        const auto identity = (*stage)->previousTexture();
        if (frame < 2)
            identities[frame] = identity;
        else
            REQUIRE(identity == identities[frame % 2]);
        REQUIRE((*stage)->previousSource().frameNumber == frame);
        REQUIRE((*stage)->previousSource().activeWidth == width);
        REQUIRE((*stage)->previousSource().built);
        GraphBuffer buffer = frame == 0 ? graph.importBuffer(*readback, "lmx.test.hzbReadback")
                                        : graph.importBuffer(*readback, "lmx.test.hzbReadback",
                                                             lmx::render::BufferUse::StorageWrite);
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
                texture, rojoRHI::SubresourceRange{.baseMip = level, .mipCount = 1});
            desc.bufferWrites.push_back(buffer);
            graph.addComputePass(std::format("lmx.test.hzb.read{}", input), std::move(desc),
                                 [&, params, texture, buffer](const PassResources& resources) {
                                     auto t = resources.texture(texture);
                                     auto b = resources.buffer(buffer);
                                     REQUIRE(t.texture.bits != 0);
                                     REQUIRE(b.bits != 0);
                                     commands.setPipeline(reader->get());
                                     commands.pushRoot(lmx::test::HzbReadbackRoot{
                                         frameData(commands, params), resources.address(buffer),
                                         static_cast<uint32_t>(resources.view(texture)), 0, 0, 0});
#ifndef NDEBUG
                                     const std::array<rojoRHI::ResourceAccess, 2> accesses{
                                         {{.texture = t.texture,
                                           .range = {params.level, 1, 0, 1},
                                           .stage = rojoRHI::Stage::ComputeShader,
                                           .access = rojoRHI::Access::ShaderRead},
                                          {.buffer = b,
                                           .offset = uint64_t{params.offset} * 4,
                                           .size = uint64_t{params.width} * params.height * 4,
                                           .stage = rojoRHI::Stage::ComputeShader,
                                           .access = rojoRHI::Access::ShaderWrite}}};
                                     commands.declareAccess(accesses);
#endif
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
        gpu->submitAndEndFrame(gpuFrame1);
        gpu->waitIdle();
        std::vector<float> values(capacity);
        {
            const auto readbackBytes = lmx::render::readbackNow(
                *gpu, *readback,
                lmx::render::SyncPoint{rojoRHI::Stage::ComputeShader, rojoRHI::Access::ShaderWrite,
                                       rojoRHI::Layout::Undefined},
                lmx::render::wholeBuffer(*readback));
            REQUIRE(readbackBytes);
            std::memcpy(values.data(), readbackBytes->data(), capacity * sizeof(float));
        }
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
