//----------------------------------------------------------------------------------------------------------------------
/// @file Readback.cpp
/// @brief Records preserving staging copies and resolves frame-bound readback outcomes.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Gpu/Readback.h"

#include "Core/Diagnostics/Assert.h"

#include <cstring>
#include <limits>
#include <optional>
#include <utility>

namespace lmx::render {

struct ReadbackOperation {
    engine::GpuBuffer staging;
    const std::byte* mapped = nullptr;
    std::optional<Error> error;
    uint64_t packedRow = 0;
    uint32_t rowPitch = 0;
    uint32_t height = 0;
};

struct ReadbackAccess {
    //==================================================================================================================
    static std::shared_ptr<ReadbackOperation>& operation(ReadbackTicket& ticket) {
        return ticket.m_operation;
    }

    //==================================================================================================================
    static const std::shared_ptr<ReadbackOperation>& operation(const ReadbackTicket& ticket) {
        return ticket.m_operation;
    }
};

namespace {

//======================================================================================================================
BufferRegion textureRegion(const engine::GpuTexture& texture) {
    const uint64_t size = packedTextureBytes(texture.desc());
    LMX_ASSERT(size != 0, "readback: the texture's format has no tightly packed texel size");
    return {0, size};
}

//======================================================================================================================
void checkBufferRegion(const engine::GpuBuffer& buffer, BufferRegion region) {
    LMX_ASSERT(region.offset <= buffer.desc().size &&
                   region.size <= buffer.desc().size - region.offset,
               "readback: the region must lie inside the buffer");
}

//======================================================================================================================
void checkCopyRegion(BufferRegion region) {
    LMX_ASSERT(region.size == 0 || (region.offset % 4 == 0 && region.size % 4 == 0),
               "readback: a GPU buffer copy requires four-byte aligned offset and size");
}

//======================================================================================================================
TextureState checkedTextureState(std::span<const TextureRangeState> current) {
    LMX_ASSERT(current.size() == 1 && current[0].range.baseMip == 0 &&
                   current[0].range.mipCount == 1 && current[0].range.baseLayer == 0 &&
                   current[0].range.layerCount == 1,
               "readback: physical state must cover exactly mip zero and layer zero");
    LMX_ASSERT(current[0].state.layout != rojoRHI::Layout::Undefined,
               "readback: preserving a texture requires a known non-Undefined layout");
    return current[0].state;
}

//======================================================================================================================
ReadbackTicket prepareBuffer(engine::GpuContext& gpu, const engine::GpuBuffer& buffer,
                             BufferRegion region, uint64_t frame) {
    auto ticket = makeReadbackTicket(buffer, region, frame);
    if (region.size == 0 || buffer.desc().memory != rojoRHI::Memory::Gpu) {
        return ticket;
    }
    checkCopyRegion(region);
    auto staging = engine::GpuBuffer::create(
        gpu.device(),
        {.size = region.size, .memory = rojoRHI::Memory::Readback, .label = "lmx.readback.buffer"});
    if (!staging) {
        auto failed = makeReadbackTicket(staging.error());
        failed.buffer = ticket.buffer;
        failed.region = ticket.region;
        failed.frame = frame;
        return failed;
    }
    auto operation = std::make_shared<ReadbackOperation>();
    operation->staging = std::move(*staging);
    ReadbackAccess::operation(ticket) = std::move(operation);
    return ticket;
}

//======================================================================================================================
ReadbackTicket prepareTexture(engine::GpuContext& gpu, const engine::GpuTexture& texture,
                              uint64_t frame) {
    auto ticket = makeReadbackTicket(texture, frame);
    const uint64_t packedRow =
        uint64_t{texture.desc().extent.width} * packedTexelBytes(texture.desc().format);
    const uint64_t rowPitch = (packedRow + 255) & ~uint64_t{255};
    LMX_ASSERT(rowPitch <= std::numeric_limits<uint32_t>::max(),
               "readback: texture row pitch exceeds the copy layout's range");
    auto staging =
        engine::GpuBuffer::create(gpu.device(), {.size = rowPitch * texture.desc().extent.height,
                                                 .memory = rojoRHI::Memory::Readback,
                                                 .label = "lmx.readback.texture"});
    if (!staging) {
        auto failed = makeReadbackTicket(staging.error());
        failed.texture = ticket.texture;
        failed.region = ticket.region;
        failed.frame = frame;
        return failed;
    }
    auto operation = std::make_shared<ReadbackOperation>();
    operation->staging = std::move(*staging);
    operation->packedRow = packedRow;
    operation->rowPitch = static_cast<uint32_t>(rowPitch);
    operation->height = texture.desc().extent.height;
    ReadbackAccess::operation(ticket) = std::move(operation);
    return ticket;
}

//======================================================================================================================
void copyBuffer(rojoRHI::CommandList& commands, const ReadbackTicket& ticket, SyncPoint current) {
    const auto& operation = *ReadbackAccess::operation(ticket);
    if (operation.error || !operation.staging) {
        return;
    }
    const rojoRHI::BufferBarrier toCopy{.buffer = ticket.buffer,
                                        .offset = ticket.region.offset,
                                        .size = ticket.region.size,
                                        .srcStage = current.stage,
                                        .srcAccess = current.access,
                                        .dstStage = rojoRHI::Stage::Copy,
                                        .dstAccess = rojoRHI::Access::CopyRead};
    commands.barrier({}, std::span{&toCopy, 1});
    commands.copyBuffer(ticket.buffer, ticket.region.offset, operation.staging.rhi(), 0,
                        ticket.region.size);
    const rojoRHI::BufferBarrier restore{.buffer = ticket.buffer,
                                         .offset = ticket.region.offset,
                                         .size = ticket.region.size,
                                         .srcStage = rojoRHI::Stage::Copy,
                                         .srcAccess = rojoRHI::Access::CopyRead,
                                         .dstStage = current.stage,
                                         .dstAccess = current.access};
    commands.barrier({}, std::span{&restore, 1});
}

//======================================================================================================================
void copyTexture(rojoRHI::CommandList& commands, const engine::GpuTexture& texture,
                 const ReadbackTicket& ticket, TextureState current) {
    const auto& operation = *ReadbackAccess::operation(ticket);
    if (operation.error) {
        return;
    }
    const rojoRHI::TextureBarrier toCopy{.texture = ticket.texture,
                                         .range = {0, 1, 0, 1},
                                         .srcStage = current.stage,
                                         .srcAccess = current.access,
                                         .dstStage = rojoRHI::Stage::Copy,
                                         .dstAccess = rojoRHI::Access::CopyRead,
                                         .oldLayout = current.layout,
                                         .newLayout = rojoRHI::Layout::CopySrc};
    commands.barrier({}, {}, std::span{&toCopy, 1});
    commands.copyTextureToBuffer(
        ticket.texture, {.extent = {texture.desc().extent.width, texture.desc().extent.height, 1}},
        operation.staging.rhi(), {.offset = 0, .rowPitch = operation.rowPitch});
    const rojoRHI::TextureBarrier restore{.texture = ticket.texture,
                                          .range = {0, 1, 0, 1},
                                          .srcStage = rojoRHI::Stage::Copy,
                                          .srcAccess = rojoRHI::Access::CopyRead,
                                          .dstStage = current.stage,
                                          .dstAccess = current.access,
                                          .oldLayout = rojoRHI::Layout::CopySrc,
                                          .newLayout = current.layout};
    commands.barrier({}, {}, std::span{&restore, 1});
}

//======================================================================================================================
Result<std::vector<uint8_t>> resolveVector(engine::GpuContext& gpu, const ReadbackTicket& ticket) {
    const auto& operation = ReadbackAccess::operation(ticket);
    if (operation && operation->error) {
        return std::unexpected(*operation->error);
    }
    std::vector<uint8_t> bytes(ticket.region.size);
    auto result = resolve(gpu, ticket, bytes);
    if (!result) {
        return std::unexpected(result.error());
    }
    return bytes;
}

} // namespace

//======================================================================================================================
uint32_t packedTexelBytes(rojoRHI::Format format) {
    switch (format) {
    case rojoRHI::Format::BGRA8Unorm:
    case rojoRHI::Format::RGBA8Unorm:
    case rojoRHI::Format::RGBA8Unorm_sRGB:
    case rojoRHI::Format::RG16Float:
    case rojoRHI::Format::R32Float:
    case rojoRHI::Format::RGB10A2Unorm:
    case rojoRHI::Format::D32Float:
        return 4;
    case rojoRHI::Format::RGBA16Float:
        return 8;
    case rojoRHI::Format::R16Float:
        return 2;
    case rojoRHI::Format::R8Unorm:
        return 1;
    case rojoRHI::Format::BC1Unorm:
    case rojoRHI::Format::BC1Unorm_sRGB:
    case rojoRHI::Format::D32FloatS8:
    case rojoRHI::Format::Undefined:
        return 0;
    }
    return 0;
}

//======================================================================================================================
uint64_t packedTextureBytes(const rojoRHI::TextureDesc& desc) {
    return uint64_t{desc.extent.width} * desc.extent.height * packedTexelBytes(desc.format);
}

//======================================================================================================================
BufferRegion wholeBuffer(const engine::GpuBuffer& buffer) {
    return {0, buffer.desc().size};
}

//======================================================================================================================
ReadbackTicket makeReadbackTicket(const engine::GpuBuffer& buffer, BufferRegion region,
                                  uint64_t frame) {
    checkBufferRegion(buffer, region);
    ReadbackTicket ticket;
    ticket.buffer = buffer.rhi();
    ticket.region = region;
    ticket.frame = frame;
    if (region.size == 0 || buffer.desc().memory != rojoRHI::Memory::Gpu) {
        auto operation = std::make_shared<ReadbackOperation>();
        if (region.size != 0) {
            operation->mapped = buffer.cpuPointer() + region.offset;
        }
        ReadbackAccess::operation(ticket) = std::move(operation);
    }
    return ticket;
}

//======================================================================================================================
ReadbackTicket makeReadbackTicket(const engine::GpuTexture& texture, uint64_t frame) {
    ReadbackTicket ticket;
    ticket.texture = texture.rhi();
    ticket.region = textureRegion(texture);
    ticket.frame = frame;
    return ticket;
}

//======================================================================================================================
ReadbackTicket makeReadbackTicket(Error error) {
    ReadbackTicket ticket;
    auto operation = std::make_shared<ReadbackOperation>();
    operation->error = std::move(error);
    ReadbackAccess::operation(ticket) = std::move(operation);
    return ticket;
}

//======================================================================================================================
ReadbackTicket recordReadback(engine::GpuContext& gpu, rojoRHI::CommandList& commands,
                              const engine::GpuBuffer& buffer, SyncPoint current,
                              BufferRegion region) {
    LMX_ASSERT(gpu.inFrame(), "recordReadback: a frame must be open");
    auto ticket = prepareBuffer(gpu, buffer, region, gpu.frameNumber());
    copyBuffer(commands, ticket, current);
    return ticket;
}

//======================================================================================================================
ReadbackTicket recordReadback(engine::GpuContext& gpu, rojoRHI::CommandList& commands,
                              const engine::GpuTexture& texture, const TextureStateTable& current) {
    const auto states = current.physicalRanges(texture.rhi(), {0, 1, 0, 1});
    return recordReadback(gpu, commands, texture, states);
}

//======================================================================================================================
ReadbackTicket recordReadback(engine::GpuContext& gpu, rojoRHI::CommandList& commands,
                              const engine::GpuTexture& texture,
                              std::span<const TextureRangeState> current) {
    LMX_ASSERT(gpu.inFrame(), "recordReadback: a frame must be open");
    const TextureState state = checkedTextureState(current);
    auto ticket = prepareTexture(gpu, texture, gpu.frameNumber());
    copyTexture(commands, texture, ticket, state);
    return ticket;
}

//======================================================================================================================
Result<void> resolve(engine::GpuContext& gpu, const ReadbackTicket& ticket,
                     std::span<uint8_t> destination) {
    const auto& operation = ReadbackAccess::operation(ticket);
    LMX_ASSERT(operation != nullptr, "resolve: a ticket must contain a recorded operation");
    if (operation->error) {
        return std::unexpected(*operation->error);
    }
    LMX_ASSERT((ticket.buffer.bits != 0) != (ticket.texture.bits != 0),
               "resolve: a ticket names exactly one buffer or texture");
    if (ticket.buffer.bits != 0) {
        LMX_ASSERT(destination.size() <= ticket.region.size,
                   "resolve: the destination must fit inside the ticket's region");
    } else {
        LMX_ASSERT(destination.size() == ticket.region.size,
                   "resolve: a texture destination must hold exactly the ticket's region");
    }
    gpu.waitForFrame(ticket.frame);
    if (destination.empty()) {
        return {};
    }
    const std::byte* source =
        operation->staging ? operation->staging.cpuPointer() : operation->mapped;
    LMX_ASSERT(source != nullptr, "resolve: the recorded operation must provide mapped bytes");
    if (ticket.buffer.bits != 0) {
        std::memcpy(destination.data(), source, destination.size());
    } else {
        for (uint32_t row = 0; row < operation->height; ++row) {
            std::memcpy(destination.data() + row * operation->packedRow,
                        source + row * operation->rowPitch, operation->packedRow);
        }
    }
    return {};
}

//======================================================================================================================
Result<std::vector<uint8_t>> readbackNow(engine::GpuContext& gpu, const engine::GpuTexture& texture,
                                         const TextureStateTable& current) {
    const auto states = current.physicalRanges(texture.rhi(), {0, 1, 0, 1});
    return readbackNow(gpu, texture, states);
}

//======================================================================================================================
Result<std::vector<uint8_t>> readbackNow(engine::GpuContext& gpu, const engine::GpuTexture& texture,
                                         std::span<const TextureRangeState> current) {
    LMX_ASSERT(!gpu.inFrame(), "readbackNow: no frame may be open");
    const TextureState state = checkedTextureState(current);
    auto ticket = prepareTexture(gpu, texture, 0);
    if (ReadbackAccess::operation(ticket)->error) {
        return std::unexpected(*ReadbackAccess::operation(ticket)->error);
    }
    auto frame = gpu.beginFrame();
    ticket.frame = frame.number;
    copyTexture(frame.commands, texture, ticket, state);
    gpu.submitAndEndFrame(frame);
    return resolveVector(gpu, ticket);
}

//======================================================================================================================
Result<std::vector<uint8_t>> readbackNow(engine::GpuContext& gpu, const engine::GpuBuffer& buffer,
                                         SyncPoint current, BufferRegion region) {
    LMX_ASSERT(!gpu.inFrame(), "readbackNow: no frame may be open");
    auto ticket = prepareBuffer(gpu, buffer, region, 0);
    if (ReadbackAccess::operation(ticket)->error) {
        return std::unexpected(*ReadbackAccess::operation(ticket)->error);
    }
    if (region.size == 0) {
        return std::vector<uint8_t>{};
    }
    if (buffer.desc().memory != rojoRHI::Memory::Gpu) {
        gpu.waitIdle();
        return resolveVector(gpu, ticket);
    }
    auto frame = gpu.beginFrame();
    ticket.frame = frame.number;
    copyBuffer(frame.commands, ticket, current);
    gpu.submitAndEndFrame(frame);
    return resolveVector(gpu, ticket);
}

} // namespace lmx::render
