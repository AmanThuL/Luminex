#include <catch2/catch_test_macros.hpp>

#include "Render/Gpu/Readback.h"

#include <rojoRHI/Buffer.h>
#include <rojoRHI/Format.h>
#include <rojoRHI/TextureDesc.h>

namespace {

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
    const auto buffer = lmx::engine::GpuBuffer::adopt(nullptr, bufferDesc(256));
    const auto texture =
        lmx::engine::GpuTexture::adopt(nullptr, textureDesc(4, 2, rojoRHI::Format::RGBA16Float));

    const lmx::render::ReadbackTicket bufferTicket =
        lmx::render::makeReadbackTicket(buffer, {0, 64}, 7);
    const lmx::render::ReadbackTicket textureTicket = lmx::render::makeReadbackTicket(texture, 9);

    CHECK(bufferTicket.buffer == &buffer);
    CHECK(bufferTicket.texture == nullptr);
    CHECK(bufferTicket.region.offset == 0);
    CHECK(bufferTicket.region.size == 64);
    CHECK(bufferTicket.frame == 7);
    CHECK(textureTicket.buffer == nullptr);
    CHECK(textureTicket.texture == &texture);
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
