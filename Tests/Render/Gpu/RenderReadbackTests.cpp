#include <catch2/catch_test_macros.hpp>

#include "Render/Gpu/Readback.h"
#include "Support/GraphTestSupport.h"

#include <rojoRHI/Buffer.h>
#include <rojoRHI/Format.h>
#include <rojoRHI/Texture.h>
#include <rojoRHI/TextureDesc.h>

#include <array>
#include <cstring>
#include <memory>
#include <utility>

namespace {

// Stands in for a device texture; tickets only take its address.
class StubTexture final : public rojoRHI::Texture {
public:
    //==================================================================================================================
    uint32_t width() const override { return 0; }

    //==================================================================================================================
    uint32_t height() const override { return 0; }

    //==================================================================================================================
    rojoRHI::Format format() const override { return rojoRHI::Format::RGBA16Float; }

    //==================================================================================================================
    uint32_t mipLevels() const override { return 1; }

    //==================================================================================================================
    uint32_t arrayLayers() const override { return 1; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}
};

// Stands in for a device buffer whose every byte reads back as `fill`.
class StubBuffer final : public rojoRHI::Buffer {
public:
    //==================================================================================================================
    explicit StubBuffer(uint8_t fill = 0) : m_fill(fill) {}

    //==================================================================================================================
    uint64_t size() const override { return 0; }

    //==================================================================================================================
    void readback(void* out, uint64_t outSize) override { std::memset(out, m_fill, outSize); }

    //==================================================================================================================
    void write(uint64_t, const void*, uint64_t) override {}

private:
    uint8_t m_fill;
};

//======================================================================================================================
// A level-0 descriptor of `width` by `height` texels in `format`; nothing else affects the size.
rojoRHI::TextureDesc textureDesc(uint32_t width, uint32_t height, rojoRHI::Format format) {
    rojoRHI::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.cpuReadback = true;
    desc.label = "readback.texture";
    return desc;
}

//======================================================================================================================
// A readback buffer of `size` bytes.
rojoRHI::BufferDesc bufferDesc(uint64_t size) {
    rojoRHI::BufferDesc desc;
    desc.size = size;
    desc.cpuReadback = true;
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
TEST_CASE("makeReadbackTicket stamps the frame and region", "[render]") {
    const auto buffer =
        lmx::engine::GpuBuffer::adopt(std::make_unique<StubBuffer>(), bufferDesc(256));
    const auto texture = lmx::engine::GpuTexture::adopt(
        std::make_unique<StubTexture>(), textureDesc(4, 2, rojoRHI::Format::RGBA16Float));

    const lmx::render::ReadbackTicket bufferTicket =
        lmx::render::makeReadbackTicket(buffer, {0, 64}, 7);
    const lmx::render::ReadbackTicket textureTicket = lmx::render::makeReadbackTicket(texture, 9);

    CHECK(bufferTicket.buffer == &buffer.rhi());
    CHECK(bufferTicket.texture == nullptr);
    CHECK(bufferTicket.region.offset == 0);
    CHECK(bufferTicket.region.size == 64);
    CHECK(bufferTicket.frame == 7);
    CHECK(textureTicket.buffer == nullptr);
    CHECK(textureTicket.texture == &texture.rhi());
    CHECK(textureTicket.region.offset == 0);
    CHECK(textureTicket.region.size == 4 * 2 * 8);
    CHECK(textureTicket.frame == 9);
}

//======================================================================================================================
TEST_CASE("wholeBuffer spans the descriptor size from offset zero", "[render]") {
    const auto buffer = lmx::engine::GpuBuffer::adopt(nullptr, bufferDesc(1024));

    const lmx::render::BufferRegion region = lmx::render::wholeBuffer(buffer);

    CHECK(region.offset == 0);
    CHECK(region.size == 1024);
}

//======================================================================================================================
TEST_CASE("a ticket resolves the object recorded even after its owner is replaced", "[render]") {
    auto owner = lmx::engine::GpuBuffer::adopt(std::make_unique<StubBuffer>(0x11), bufferDesc(64));
    const lmx::render::ReadbackTicket ticket = lmx::render::makeReadbackTicket(owner, {0, 8}, 3);
    // Growth keeps the written buffer alive elsewhere and hands the owner a new one.
    const lmx::engine::GpuBuffer retiring = std::move(owner);
    owner = lmx::engine::GpuBuffer::adopt(std::make_unique<StubBuffer>(0x22), bufferDesc(64));
    FakeDevice device;
    lmx::engine::GpuContext gpu{device};
    std::array<uint8_t, 8> bytes{};

    lmx::render::resolve(gpu, ticket, bytes);

    CHECK(ticket.buffer == &retiring.rhi());
    CHECK(bytes == std::array<uint8_t, 8>{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11});
}
