#include <catch2/catch_test_macros.hpp>

#include "Render/Gpu/Readback.h"
#include "Support/TestGpu.h"

#include <rojoRHI/RHI.h>

#include <array>
#include <cstring>
#include <memory>
#include <utility>

namespace {

//======================================================================================================================
// A level-0 descriptor of `width` by `height` texels in `format`; nothing else affects the size.
rojoRHI::TextureDesc textureDesc(uint32_t width, uint32_t height, rojoRHI::Format format) {
    rojoRHI::TextureDesc desc;
    desc.extent = {width, height, 1};
    desc.format = format;
    desc.usage = rojoRHI::TextureUsage::Sampled;
    desc.label = "readback.texture";
    return desc;
}

//======================================================================================================================
// A readback buffer of `size` bytes.
rojoRHI::BufferDesc bufferDesc(uint64_t size) {
    rojoRHI::BufferDesc desc;
    desc.size = size;
    desc.memory = rojoRHI::Memory::Upload;
    desc.label = "readback.buffer";
    return desc;
}

} // namespace

//======================================================================================================================
TEST_CASE("packedTextureBytes multiplies extent by the format's texel size", "[render]") {
    using lmx::render::packedTextureBytes;
    using rojoRHI::Format;

    CHECK(packedTextureBytes(textureDesc(16, 8, Format::BGRA8Unorm)) == 16 * 8 * 4);
    CHECK(packedTextureBytes(textureDesc(16, 8, Format::RGBA16Float)) == 16 * 8 * 8);
    CHECK(packedTextureBytes(textureDesc(16, 8, Format::R32Float)) == 16 * 8 * 4);
    CHECK(packedTextureBytes(textureDesc(16, 8, Format::RG16Float)) == 16 * 8 * 4);
}

//======================================================================================================================
TEST_CASE("makeReadbackTicket stamps the frame and region", "[render][device]") {
    auto gpu = createTestGpu();
    auto bufferResult = lmx::engine::GpuBuffer::create(gpu->device(), bufferDesc(256));
    INFO(errorOf(bufferResult));
    REQUIRE(bufferResult.has_value());
    auto textureResult = lmx::engine::GpuTexture::create(
        gpu->device(), textureDesc(4, 2, rojoRHI::Format::RGBA16Float));
    INFO(errorOf(textureResult));
    REQUIRE(textureResult.has_value());
    const auto& buffer = *bufferResult;
    const auto& texture = *textureResult;

    const lmx::render::ReadbackTicket bufferTicket =
        lmx::render::makeReadbackTicket(buffer, {0, 64}, 7);
    const lmx::render::ReadbackTicket textureTicket = lmx::render::makeReadbackTicket(texture, 9);

    CHECK(bufferTicket.buffer == buffer.rhi());
    CHECK(bufferTicket.texture == rojoRHI::Texture{});
    CHECK(bufferTicket.region.offset == 0);
    CHECK(bufferTicket.region.size == 64);
    CHECK(bufferTicket.frame == 7);
    CHECK(textureTicket.buffer == rojoRHI::Buffer{});
    CHECK(textureTicket.texture == texture.rhi());
    CHECK(textureTicket.region.offset == 0);
    CHECK(textureTicket.region.size == 4 * 2 * 8);
    CHECK(textureTicket.frame == 9);
}

//======================================================================================================================
TEST_CASE("wholeBuffer spans the descriptor size from offset zero", "[render][device]") {
    auto gpu = createTestGpu();
    auto result = lmx::engine::GpuBuffer::create(gpu->device(), bufferDesc(1024));
    INFO(errorOf(result));
    REQUIRE(result.has_value());
    const auto& buffer = *result;

    const lmx::render::BufferRegion region = lmx::render::wholeBuffer(buffer);

    CHECK(region.offset == 0);
    CHECK(region.size == 1024);
}

//======================================================================================================================
TEST_CASE("a ticket resolves the object recorded even after its owner is replaced",
          "[render][device]") {
    auto gpu = createTestGpu();
    const auto create = [&](uint8_t fill) {
        auto result = lmx::engine::GpuBuffer::create(gpu->device(), bufferDesc(64));
        INFO(errorOf(result));
        REQUIRE(result.has_value());
        std::memset(result->cpuPointer(), fill, 64);
        return std::move(*result);
    };
    auto owner = create(0x11);
    const auto ticket = lmx::render::makeReadbackTicket(owner, {0, 8}, 3);
    const lmx::engine::GpuBuffer retiring = std::move(owner);
    owner = create(0x22);
    for (uint32_t index = 0; index < 3; ++index) {
        auto frame = gpu->beginFrame();
        gpu->submitAndEndFrame(frame);
    }
    std::array<uint8_t, 8> bytes{};
    const auto result = lmx::render::resolve(*gpu, ticket, bytes);
    INFO(errorOf(result));
    REQUIRE(result.has_value());
    CHECK(ticket.buffer == retiring.rhi());
    CHECK(owner.rhi() != retiring.rhi());
    CHECK(bytes == std::array<uint8_t, 8>{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11});
}
