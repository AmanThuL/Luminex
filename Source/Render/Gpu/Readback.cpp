//----------------------------------------------------------------------------------------------------------------------
/// @file Readback.cpp
/// @brief Implements recorded and immediate readback over the RojoRHI v1 blocking readback.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Gpu/Readback.h"

#include "Core/Diagnostics/Assert.h"

namespace lmx::render {

namespace {

//======================================================================================================================
// Level 0 of `texture`, which is all v1 reads back. A format without a packed texel cannot be read
// back at all, so asking for one is a caller error rather than an empty read.
BufferRegion textureRegion(const engine::GpuTexture& texture) {
    const uint64_t size = packedTextureBytes(texture.desc());
    LMX_ASSERT(size != 0, "readback: the texture's format has no tightly packed texel size");
    return {0, size};
}

//======================================================================================================================
// v1's Buffer::readback copies the buffer's leading bytes, so only a region at offset zero maps
// onto it; the switch to v2 copies any region into a readback buffer and lifts this.
void readBufferRegion(rojoRHI::Buffer& buffer, BufferRegion region, uint8_t* out) {
    LMX_ASSERT(region.offset == 0, "readback: RojoRHI v1 reads a buffer from offset zero only");
    buffer.readback(out, region.size);
}

//======================================================================================================================
// A region that leaves the buffer would read past its allocation on either RHI.
void checkBufferRegion(const engine::GpuBuffer& buffer, BufferRegion region) {
    LMX_ASSERT(region.offset <= buffer.desc().size &&
                   region.size <= buffer.desc().size - region.offset,
               "readback: the region must lie inside the buffer");
}

} // namespace

//======================================================================================================================
// Omitting default lets -Wswitch catch a newly added format.
uint32_t packedTexelBytes(rojoRHI::Format format) {
    switch (format) {
    case rojoRHI::Format::BGRA8Unorm:
    case rojoRHI::Format::RGBA8Unorm:
    case rojoRHI::Format::RGBA8Unorm_sRGB:
    case rojoRHI::Format::RG16Float:
    case rojoRHI::Format::R32Float:
        return 4;
    case rojoRHI::Format::RGBA16Float:
        return 8;
    case rojoRHI::Format::R16Float:
        return 2;
    case rojoRHI::Format::R8Unorm:
        return 1;
    case rojoRHI::Format::BC1Unorm:
    case rojoRHI::Format::BC1Unorm_sRGB:
    case rojoRHI::Format::D32Float:
    case rojoRHI::Format::Unknown:
        return 0;
    }
    return 0;
}

//======================================================================================================================
uint64_t packedTextureBytes(const rojoRHI::TextureDesc& desc) {
    return uint64_t{desc.width} * desc.height * packedTexelBytes(desc.format);
}

//======================================================================================================================
BufferRegion wholeBuffer(const engine::GpuBuffer& buffer) {
    return {0, buffer.desc().size};
}

//======================================================================================================================
ReadbackTicket makeReadbackTicket(const engine::GpuBuffer& buffer, BufferRegion region,
                                  uint64_t frame) {
    checkBufferRegion(buffer, region);
    return {.buffer = &buffer.rhi(), .texture = nullptr, .region = region, .frame = frame};
}

//======================================================================================================================
ReadbackTicket makeReadbackTicket(const engine::GpuTexture& texture, uint64_t frame) {
    return {.buffer = nullptr,
            .texture = &texture.rhi(),
            .region = textureRegion(texture),
            .frame = frame};
}

//======================================================================================================================
ReadbackTicket recordReadback(engine::GpuContext& gpu,
                              [[maybe_unused]] rojoRHI::CommandList& commands,
                              const engine::GpuBuffer& buffer, [[maybe_unused]] BufferUse current,
                              BufferRegion region) {
    return makeReadbackTicket(buffer, region, gpu.frameNumber());
}

//======================================================================================================================
ReadbackTicket recordReadback(engine::GpuContext& gpu,
                              [[maybe_unused]] rojoRHI::CommandList& commands,
                              const engine::GpuTexture& texture,
                              [[maybe_unused]] TextureUse current) {
    return makeReadbackTicket(texture, gpu.frameNumber());
}

//======================================================================================================================
void resolve([[maybe_unused]] engine::GpuContext& gpu, const ReadbackTicket& ticket,
             std::span<uint8_t> destination) {
    LMX_ASSERT((ticket.buffer == nullptr) != (ticket.texture == nullptr),
               "resolve: a ticket names exactly one buffer or texture");
    if (ticket.buffer != nullptr) {
        LMX_ASSERT(destination.size() <= ticket.region.size,
                   "resolve: the destination must fit inside the ticket's region");
        readBufferRegion(*ticket.buffer, {ticket.region.offset, destination.size()},
                         destination.data());
        return;
    }
    LMX_ASSERT(destination.size() == ticket.region.size,
               "resolve: a texture destination must hold exactly the ticket's region");
    ticket.texture->readback(destination.data(), destination.size());
}

//======================================================================================================================
std::vector<uint8_t> readbackNow(engine::GpuContext& gpu, const engine::GpuTexture& texture,
                                 [[maybe_unused]] TextureUse current) {
    const BufferRegion region = textureRegion(texture);
    std::vector<uint8_t> bytes(region.size);
    gpu.waitIdle();
    texture.rhi().readback(bytes.data(), region.size);
    return bytes;
}

//======================================================================================================================
std::vector<uint8_t> readbackNow(engine::GpuContext& gpu, const engine::GpuBuffer& buffer,
                                 [[maybe_unused]] BufferUse current, BufferRegion region) {
    checkBufferRegion(buffer, region);
    std::vector<uint8_t> bytes(region.size);
    gpu.waitIdle();
    readBufferRegion(buffer.rhi(), region, bytes.data());
    return bytes;
}

} // namespace lmx::render
