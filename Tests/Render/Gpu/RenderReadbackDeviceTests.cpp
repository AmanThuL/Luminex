#include <catch2/catch_test_macros.hpp>

#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Gpu/Readback.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Renderer/Renderer.h"

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

} // namespace

//======================================================================================================================
TEST_CASE("mapped Upload inspection preserves byte regions and frame number", "[render][device]") {
    auto gpu = createReadbackGpu();
    const std::array<uint8_t, 12> initial{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    auto buffer = lmx::engine::GpuBuffer::create(
        gpu->device(),
        {.size = initial.size(), .memory = rojoRHI::Memory::Upload, .label = "readback.upload"},
        std::as_bytes(std::span{initial}));
    REQUIRE(buffer.has_value());
    const auto frameBefore = gpu->frameNumber();
    auto immediate = lmx::render::readbackNow(
        *gpu, *buffer, {rojoRHI::Stage::None, rojoRHI::Access::None, rojoRHI::Layout::Undefined},
        {1, 5});
    REQUIRE(immediate.has_value());
    CHECK(*immediate == std::vector<uint8_t>{1, 2, 3, 4, 5});
    CHECK(gpu->frameNumber() == frameBefore);

    auto frame = gpu->beginFrame();
    const auto ticket = lmx::render::recordReadback(
        *gpu, frame.commands, *buffer,
        {rojoRHI::Stage::None, rojoRHI::Access::None, rojoRHI::Layout::Undefined}, {3, 7});
    gpu->submitAndEndFrame(frame);
    std::array<uint8_t, 4> leading{};
    REQUIRE(lmx::render::resolve(*gpu, ticket, leading).has_value());
    CHECK(leading == std::array<uint8_t, 4>{3, 4, 5, 6});
    CHECK(gpu->frameNumber() == frame.number);
}

//======================================================================================================================
TEST_CASE("a recorded empty readback remains distinct from an unexecuted pass",
          "[render][device]") {
    auto gpu = createReadbackGpu();
    auto buffer =
        lmx::engine::GpuBuffer::create(gpu->device(), {.size = 16, .label = "readback.empty"});
    REQUIRE(buffer.has_value());
    const std::optional<lmx::render::ReadbackTicket> neverExecuted;
    const lmx::render::ReadbackTicket invalid;
    auto frame = gpu->beginFrame();
    const auto ticket = lmx::render::recordReadback(
        *gpu, frame.commands, *buffer,
        {rojoRHI::Stage::None, rojoRHI::Access::None, rojoRHI::Layout::Undefined}, {0, 0});
    gpu->submitAndEndFrame(frame);

    CHECK_FALSE(neverExecuted.has_value());
    CHECK(invalid.buffer.bits == 0);
    CHECK(invalid.texture.bits == 0);
    CHECK(ticket.buffer == buffer->rhi());
    CHECK(ticket.frame == frame.number);
    REQUIRE(lmx::render::resolve(*gpu, ticket, std::span<uint8_t>{}).has_value());
    const auto frameBefore = gpu->frameNumber();
    auto immediate = lmx::render::readbackNow(
        *gpu, *buffer, {rojoRHI::Stage::None, rojoRHI::Access::None, rojoRHI::Layout::Undefined},
        {0, 0});
    REQUIRE(immediate.has_value());
    CHECK(immediate->empty());
    CHECK(gpu->frameNumber() == frameBefore);
}

//======================================================================================================================
TEST_CASE("a failed readback resolves its original error before waiting", "[render][device]") {
    auto gpu = createReadbackGpu();
    auto ticket =
        lmx::render::makeReadbackTicket(lmx::Error{lmx::ErrorCode::Gpu, "readback causal failure"});
    ticket.frame = ~uint64_t{0};
    std::array<uint8_t, 4> bytes{9, 8, 7, 6};

    auto result = lmx::render::resolve(*gpu, ticket, bytes);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == lmx::ErrorCode::Gpu);
    CHECK(result.error().message == "readback causal failure");
    CHECK(bytes == std::array<uint8_t, 4>{9, 8, 7, 6});
    CHECK(gpu->frameNumber() == 0);
}

//======================================================================================================================
TEST_CASE("renderer resize seeds replacement identities from their own creation layout",
          "[render][device]") {
    auto gpu = createReadbackGpu();
    auto created = lmx::render::Renderer::create(*gpu, 8, 8);
    INFO((created ? "" : created.error().message));
    REQUIRE(created.has_value());
    auto renderer = std::move(*created);
    auto& states = renderer->textureStates();
    const auto oldColor = renderer->colorTarget().rhi();
    const auto oldHdr = renderer->hdrColorTexture().rhi();
    const auto countBefore = states.size();
    const auto readers = rojoRHI::Stage::VertexShader | rojoRHI::Stage::FragmentShader |
                         rojoRHI::Stage::ComputeShader;
    const std::array<rojoRHI::TextureBarrier, 2> barriers{
        {{.texture = oldColor,
          .range = {0, 1, 0, 1},
          .dstStage = readers,
          .dstAccess = rojoRHI::Access::ShaderRead,
          .oldLayout = rojoRHI::Layout::Undefined,
          .newLayout = rojoRHI::Layout::ShaderRead},
         {.texture = oldHdr,
          .range = {0, 1, 0, 1},
          .dstStage = readers,
          .dstAccess = rojoRHI::Access::ShaderRead,
          .oldLayout = rojoRHI::Layout::Undefined,
          .newLayout = rojoRHI::Layout::ShaderRead}}};
    auto frame = gpu->beginFrame();
    frame.commands.barrier({}, {}, barriers);
    gpu->submitAndEndFrame(frame);
    gpu->waitIdle();
    states.set(oldColor, {0, 1, 0, 1},
               {readers, rojoRHI::Access::ShaderRead, rojoRHI::Layout::ShaderRead});
    states.set(oldHdr, {0, 1, 0, 1},
               {readers, rojoRHI::Access::ShaderRead, rojoRHI::Layout::ShaderRead});
    states.setLogicalUse(oldColor, lmx::render::TextureUse::ShaderRead);
    states.setLogicalUse(oldHdr, lmx::render::TextureUse::ShaderRead);
    uint32_t width = 12;
    uint32_t height = 6;
    SECTION("different extent replaces the identities") {}
    SECTION("same extent still follows the renderer's replacement policy") {
        width = 8;
        height = 8;
    }

    REQUIRE(renderer->resize(width, height).has_value());

    const auto newColor = renderer->colorTarget().rhi();
    const auto newHdr = renderer->hdrColorTexture().rhi();
    CHECK(newColor != oldColor);
    CHECK(newHdr != oldHdr);
    for (auto texture : {newColor, newHdr}) {
        CHECK(states.logicalUse(texture) == lmx::render::TextureUse::ShaderRead);
        const auto physical = states.find(texture, {0, 1, 0, 1});
        REQUIRE(physical.has_value());
        CHECK(physical->stage == rojoRHI::Stage::None);
        CHECK(physical->access == rojoRHI::Access::None);
        CHECK(physical->layout == rojoRHI::Layout::Undefined);
    }
    CHECK_FALSE(states.logicalUse(oldColor).has_value());
    CHECK_FALSE(states.logicalUse(oldHdr).has_value());
    CHECK(states.physicalRanges(oldColor, {0, 1, 0, 1}).empty());
    CHECK(states.physicalRanges(oldHdr, {0, 1, 0, 1}).empty());
    CHECK(states.size() == countBefore);
}
