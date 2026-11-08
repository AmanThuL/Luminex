#include <catch2/catch_test_macros.hpp>

#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Gpu/Readback.h"
#include "Render/Graph/RenderGraph.h"
#include "Support/OracleRoots.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

//======================================================================================================================
std::unique_ptr<lmx::engine::GpuContext> createReadbackGpu() {
#ifndef NDEBUG
    constexpr bool kValidation = true;
#else
    constexpr bool kValidation = false;
#endif
    auto gpu = lmx::engine::GpuContext::create(
        {.validation = kValidation, .scopeTimings = false, .enableCapture = false});
    INFO((gpu ? "" : gpu.error().message));
    REQUIRE(gpu.has_value());
    return std::move(*gpu);
}

//======================================================================================================================
std::vector<uint8_t> patternBytes(uint32_t width, uint32_t height) {
    std::vector<uint8_t> bytes(uint64_t{width} * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t offset = (uint64_t{y} * width + x) * 4;
            bytes[offset] = static_cast<uint8_t>(x * 7 + y);
            bytes[offset + 1] = static_cast<uint8_t>(y * 31 + x);
            bytes[offset + 2] = static_cast<uint8_t>(x ^ y);
            bytes[offset + 3] = 255;
        }
    }
    return bytes;
}

} // namespace

//======================================================================================================================
TEST_CASE("readbackNow de-pads a 33-texel-wide texture", "[render][device][gpu][readback]") {
    auto gpu = createReadbackGpu();
    const auto bytes = patternBytes(33, 5);
    const rojoRHI::TextureData initial{.bytes = std::as_bytes(std::span{bytes}),
                                       .bytesPerRow = 132};
    auto texture = lmx::engine::GpuTexture::create(gpu->device(),
                                                   {.format = rojoRHI::Format::RGBA8Unorm,
                                                    .extent = {33, 5, 1},
                                                    .usage = rojoRHI::TextureUsage::Sampled,
                                                    .label = "readback.oddWidth"},
                                                   std::span{&initial, 1});
    REQUIRE(texture.has_value());
    lmx::render::TextureStateTable states;
    states.seedPhysical(texture->rhi(), 1, 1, texture->initialLayout());
    const auto before = states.find(texture->rhi(), {0, 1, 0, 1});

    auto result = lmx::render::readbackNow(*gpu, *texture, states);

    INFO((result ? "" : result.error().message));
    REQUIRE(result.has_value());
    CHECK(*result == bytes);
    CHECK(gpu->frameNumber() == 1);
    CHECK(states.find(texture->rhi(), {0, 1, 0, 1}) == before);
}

//======================================================================================================================
TEST_CASE("a recorded readback resolves after its frame's fence",
          "[render][device][gpu][readback]") {
    auto gpu = createReadbackGpu();
    auto buffer =
        lmx::engine::GpuBuffer::create(gpu->device(), {.size = 16,
                                                       .memory = rojoRHI::Memory::Gpu,
                                                       .usage = rojoRHI::BufferUsage::ShaderWrite,
                                                       .label = "readback.filled"});
    REQUIRE(buffer.has_value());
    auto frame = gpu->beginFrame();
    frame.commands.fillBuffer(buffer->rhi(), 0, 16, 0x11111111u);
    const auto ticket = lmx::render::recordReadback(
        *gpu, frame.commands, *buffer,
        {rojoRHI::Stage::Copy, rojoRHI::Access::CopyWrite, rojoRHI::Layout::Undefined}, {0, 16});
    const auto sharedTicket = ticket;
    gpu->submitAndEndFrame(frame);
    std::array<uint8_t, 16> bytes{};

    auto result = lmx::render::resolve(*gpu, sharedTicket, bytes);

    REQUIRE(result.has_value());
    CHECK(ticket.frame == frame.number);
    CHECK(gpu->completedFrame() >= frame.number);
    for (uint8_t byte : bytes) {
        CHECK(byte == 0x11);
    }
}

//======================================================================================================================
TEST_CASE("mapped Readback inspection waits without consuming a frame",
          "[render][device][gpu][readback]") {
    auto gpu = createReadbackGpu();
    const std::array<uint8_t, 12> initial{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    auto source = lmx::engine::GpuBuffer::create(
        gpu->device(), {.size = initial.size(), .label = "readback.copySource"},
        std::as_bytes(std::span{initial}));
    auto destination =
        lmx::engine::GpuBuffer::create(gpu->device(), {.size = initial.size(),
                                                       .memory = rojoRHI::Memory::Readback,
                                                       .label = "readback.mappedDestination"});
    REQUIRE(source.has_value());
    REQUIRE(destination.has_value());
    auto frame = gpu->beginFrame();
    frame.commands.copyBuffer(source->rhi(), 0, destination->rhi(), 0, initial.size());
    const auto ticket = lmx::render::recordReadback(
        *gpu, frame.commands, *destination,
        {rojoRHI::Stage::Copy, rojoRHI::Access::CopyWrite, rojoRHI::Layout::Undefined}, {1, 7});
    gpu->submitAndEndFrame(frame);
    std::array<uint8_t, 7> recorded{};
    REQUIRE(lmx::render::resolve(*gpu, ticket, recorded).has_value());
    CHECK(recorded == std::array<uint8_t, 7>{1, 2, 3, 4, 5, 6, 7});
    const auto frameBefore = gpu->frameNumber();
    auto immediate = lmx::render::readbackNow(
        *gpu, *destination,
        {rojoRHI::Stage::Copy, rojoRHI::Access::CopyWrite, rojoRHI::Layout::Undefined}, {3, 5});
    REQUIRE(immediate.has_value());
    CHECK(*immediate == std::vector<uint8_t>{3, 4, 5, 6, 7});
    CHECK(gpu->frameNumber() == frameBefore);
}

//======================================================================================================================
TEST_CASE("initialized depth bytes survive a preserving shader-read transition",
          "[render][device][gpu][readback]") {
    auto gpu = createReadbackGpu();
    const auto caps = gpu->device().formatCapabilities(rojoRHI::Format::D32Float);
    if (!caps.sampled || !caps.depthAttachment) {
        SKIP("the device does not support sampled D32Float depth attachments");
    }
    const std::array<float, 4> initial{0.0f, 0.25f, 0.5f, 1.0f};
    const rojoRHI::TextureData data{.bytes = std::as_bytes(std::span{initial}), .bytesPerRow = 8};
    auto texture = lmx::engine::GpuTexture::create(
        gpu->device(),
        {.format = rojoRHI::Format::D32Float,
         .extent = {2, 2, 1},
         .usage = rojoRHI::TextureUsage::DepthStencil | rojoRHI::TextureUsage::Sampled,
         .label = "readback.initialDepth"},
        std::span{&data, 1});
    REQUIRE(texture.has_value());
    auto frame = gpu->beginFrame();
    const rojoRHI::TextureBarrier transition{.texture = texture->rhi(),
                                             .range = {0, 1, 0, 1},
                                             .dstStage = rojoRHI::Stage::FragmentShader,
                                             .dstAccess = rojoRHI::Access::ShaderRead,
                                             .oldLayout = rojoRHI::Layout::ShaderRead,
                                             .newLayout = rojoRHI::Layout::DepthStencilRead};
    frame.commands.barrier({}, {}, std::span{&transition, 1});
    const lmx::render::TextureRangeState current{{0, 1, 0, 1},
                                                 {rojoRHI::Stage::FragmentShader,
                                                  rojoRHI::Access::ShaderRead,
                                                  rojoRHI::Layout::DepthStencilRead}};
    const auto ticket =
        lmx::render::recordReadback(*gpu, frame.commands, *texture, std::span{&current, 1});
    gpu->submitAndEndFrame(frame);
    std::array<float, 4> actual{};
    REQUIRE(lmx::render::resolve(*gpu, ticket, lmx::render::resolveBytes(std::span{actual}))
                .has_value());
    CHECK(actual == initial);
}

//======================================================================================================================
TEST_CASE("readback restores the copied cell of mixed mip states",
          "[render][device][gpu][readback]") {
    auto gpu = createReadbackGpu();
    const auto level0 = patternBytes(8, 4);
    const auto level1 = patternBytes(4, 2);
    const std::array<rojoRHI::TextureData, 2> data{
        {{.bytes = std::as_bytes(std::span{level0}), .bytesPerRow = 32},
         {.bytes = std::as_bytes(std::span{level1}), .bytesPerRow = 16}}};
    auto texture = lmx::engine::GpuTexture::create(
        gpu->device(),
        {.format = rojoRHI::Format::RGBA8Unorm,
         .extent = {8, 4, 1},
         .mips = 2,
         .usage = rojoRHI::TextureUsage::Sampled | rojoRHI::TextureUsage::RenderTarget,
         .label = "readback.mixedMips"},
        data);
    REQUIRE(texture.has_value());
    lmx::render::TextureStateTable states;
    states.seedPhysical(texture->rhi(), 2, 1, texture->initialLayout());
    auto frame = gpu->beginFrame();
    const rojoRHI::TextureBarrier transition{.texture = texture->rhi(),
                                             .range = {1, 1, 0, 1},
                                             .dstStage = rojoRHI::Stage::Copy,
                                             .dstAccess = rojoRHI::Access::CopyWrite,
                                             .oldLayout = rojoRHI::Layout::ShaderRead,
                                             .newLayout = rojoRHI::Layout::CopyDst};
    frame.commands.barrier({}, {}, std::span{&transition, 1});
    frame.commands.clearTexture(texture->rhi(), {1, 1, 0, 1},
                                {.color = {0.25f, 0.5f, 0.75f, 1.0f}});
    states.set(texture->rhi(), {1, 1, 0, 1},
               {rojoRHI::Stage::Copy, rojoRHI::Access::CopyWrite, rojoRHI::Layout::CopyDst});
    const auto before0 = states.find(texture->rhi(), {0, 1, 0, 1});
    const auto before1 = states.find(texture->rhi(), {1, 1, 0, 1});
    const auto ticket = lmx::render::recordReadback(*gpu, frame.commands, *texture, states);
    gpu->submitAndEndFrame(frame);
    std::vector<uint8_t> actual(level0.size());
    REQUIRE(lmx::render::resolve(*gpu, ticket, actual).has_value());
    CHECK(actual == level0);
    CHECK(states.find(texture->rhi(), {0, 1, 0, 1}) == before0);
    CHECK(states.find(texture->rhi(), {1, 1, 0, 1}) == before1);
    auto repeated = lmx::render::readbackNow(*gpu, *texture, states);
    INFO((repeated ? "" : repeated.error().message));
    REQUIRE(repeated.has_value());
    CHECK(*repeated == level0);
    CHECK(states.find(texture->rhi(), {0, 1, 0, 1}) == before0);
    CHECK(states.find(texture->rhi(), {1, 1, 0, 1}) == before1);
}

//======================================================================================================================
TEST_CASE("initialized color remains sampleable through its first graph use",
          "[render][device][gpu][readback]") {
    auto gpu = createReadbackGpu();
    const auto bytes = patternBytes(33, 5);
    const rojoRHI::TextureData data{.bytes = std::as_bytes(std::span{bytes}), .bytesPerRow = 132};
    auto source = lmx::engine::GpuTexture::create(gpu->device(),
                                                  {.format = rojoRHI::Format::RGBA8Unorm,
                                                   .extent = {33, 5, 1},
                                                   .usage = rojoRHI::TextureUsage::Sampled,
                                                   .label = "readback.sampleSource"},
                                                  std::span{&data, 1});
    auto output = lmx::engine::GpuTexture::create(
        gpu->device(),
        {.format = rojoRHI::Format::RGBA8Unorm,
         .extent = {33, 5, 1},
         .usage = rojoRHI::TextureUsage::RenderTarget | rojoRHI::TextureUsage::Sampled,
         .label = "readback.sampleOutput"});
    REQUIRE(source.has_value());
    REQUIRE(output.has_value());
    auto library = gpu->loadShaderLibrary("FullscreenSample");
    REQUIRE(library.has_value());
    const std::array<rojoRHI::ColorTarget, 1> colors{{{.format = rojoRHI::Format::RGBA8Unorm}}};
    auto pipeline = lmx::engine::adoptGpu(
        gpu->device(),
        gpu->device().createPipeline({.vertex = {library->get(), "vertexMain"},
                                      .fragment = {library->get(), "fragmentMain"},
                                      .raster = {.cull = rojoRHI::CullMode::None},
                                      .depthStencil = {.depthTest = false,
                                                       .depthWrite = false,
                                                       .depthCompare = rojoRHI::CompareOp::Always},
                                      .colors = colors,
                                      .label = "readback.samplePipeline"}));
    REQUIRE(pipeline.has_value());
    lmx::render::TextureStateTable states;
    auto frame = gpu->beginFrame();
    lmx::render::RenderGraph graph;
    graph.bindTextureStates(states);
    const auto input = graph.importPersistentTexture(*source, "readback.sampleSource");
    const auto target = graph.importPersistentTexture(*output, "readback.sampleOutput");
    lmx::render::PassDesc pass;
    pass.textureReads.push_back(input);
    pass.color = lmx::render::ColorAttachment{
        .handle = target, .load = lmx::render::LoadOp::Clear, .store = lmx::render::StoreOp::Store};
    graph.addPass(
        "readback.sample", std::move(pass), [&](const lmx::render::PassResources& resources) {
            frame.commands.setPipeline(pipeline->get());
            frame.commands.pushRoot(lmx::test::FullscreenSampleRoot{
                .source = static_cast<uint32_t>(resources.view(input))});
#ifndef NDEBUG
            const rojoRHI::ResourceAccess access{.texture = resources.texture(input).texture,
                                                 .range = {0, 1, 0, 1},
                                                 .stage = rojoRHI::Stage::FragmentShader,
                                                 .access = rojoRHI::Access::ShaderRead};
            frame.commands.declareAccess(std::span{&access, 1});
#endif
            frame.commands.draw(3);
        });
    graph.exportTexture(lmx::render::nextVersion(target));
    graph.execute(frame.commands, frame.number);
    gpu->submitAndEndFrame(frame);
    auto actual = lmx::render::readbackNow(*gpu, *output, states);
    REQUIRE(actual.has_value());
    CHECK(*actual == bytes);
}
