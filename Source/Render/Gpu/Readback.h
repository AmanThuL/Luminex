//----------------------------------------------------------------------------------------------------------------------
/// @file Readback.h
/// @brief Declares recorded and immediate GPU readback that names each resource's current use.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Graph/ResourceUse.h"

#include <rojoRHI/Buffer.h>
#include <rojoRHI/CommandList.h>
#include <rojoRHI/Format.h>
#include <rojoRHI/Texture.h>
#include <rojoRHI/TextureDesc.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace lmx::render {

/// A byte range of a buffer.
struct BufferRegion {
    uint64_t offset = 0; ///< First byte of the range.
    uint64_t size = 0;   ///< Number of bytes in the range.
};

/// One readback recorded into a frame, resolved once that frame has retired.
///
/// Callers treat a ticket as opaque: they get it from `recordReadback`, keep or move it until the
/// frame retires and pass it to `resolve`, and never build one or read its fields. The switch to
/// v2 adds the staging copy as a handle into a pool the GPU context owns. Exactly one of `buffer`
/// and `texture` is set; a texture ticket covers level 0, tightly packed. The ticket names the
/// object the owner held when it was recorded, never the owner: an owner can be handed a new object
/// while a readback is still pending, as draw-list growth does, and the resolve must read the
/// object the frame wrote. That object must outlive the resolve.
struct ReadbackTicket {
    rojoRHI::Buffer* buffer = nullptr;   ///< Buffer read back, or null for a texture.
    rojoRHI::Texture* texture = nullptr; ///< Texture read back, or null for a buffer.
    BufferRegion region;                 ///< Bytes the resolve copies out.
    uint64_t frame = 0;                  ///< Frame the readback was recorded in.
};

/// Returns the bytes a texel of `format` occupies when read back tightly packed.
///
/// Named apart from RojoRHI v1's `bytesPerPixel`, so code that uses both namespaces stays
/// unambiguous.
///
/// Matches RojoRHI v1's own table for every format it reads back; a block-compressed, depth or
/// unknown format has no packed texel and returns 0.
uint32_t packedTexelBytes(rojoRHI::Format format);

/// Returns the tightly packed size of level 0 of a texture described by `desc`.
uint64_t packedTextureBytes(const rojoRHI::TextureDesc& desc);

/// Returns the region that spans all of `buffer`, from offset zero.
BufferRegion wholeBuffer(const engine::GpuBuffer& buffer);

/// Builds the ticket for reading `region` of `buffer` back after `frame` retires; the region must
/// lie inside the buffer.
///
/// Internal to the readback seam and its tests: callers obtain tickets from `recordReadback`.
ReadbackTicket makeReadbackTicket(const engine::GpuBuffer& buffer, BufferRegion region,
                                  uint64_t frame);
/// Builds the ticket for reading level 0 of `texture` back after `frame` retires; internal to the
/// readback seam and its tests, as the buffer form is.
ReadbackTicket makeReadbackTicket(const engine::GpuTexture& texture, uint64_t frame);

/// Records a readback of `region` of `buffer` into the open frame's `commands`.
///
/// `current` is the use the frame last recorded on the buffer before this point. On RojoRHI v1
/// nothing is recorded and `current` is unused: the ticket names the open frame, and `resolve`
/// reads the buffer itself. The switch to v2 records here the barrier out of `current`, the copy
/// into a readback buffer and the barrier back, without touching callers.
ReadbackTicket recordReadback(engine::GpuContext& gpu, rojoRHI::CommandList& commands,
                              const engine::GpuBuffer& buffer, BufferUse current,
                              BufferRegion region);
/// Records a readback of level 0 of `texture` into the open frame's `commands`.
///
/// `current` is the use the frame last recorded on the texture before this point; v1 ignores it
/// as the buffer form does.
ReadbackTicket recordReadback(engine::GpuContext& gpu, rojoRHI::CommandList& commands,
                              const engine::GpuTexture& texture, TextureUse current);

/// Copies the bytes `ticket` names into `destination`.
///
/// A texture destination holds exactly `ticket.region.size` bytes. A buffer destination holds at
/// most that many and receives the region's leading bytes: it serves a caller that learns from the
/// retired frame itself how much of the region is defined, as a count read back first.
///
/// The caller resolves only after the ticket's frame has retired. On RojoRHI v1 this is the
/// blocking readback; a buffer ticket must start at offset zero because v1 reads a buffer's leading
/// bytes only. Bytes are `uint8_t`, as `readbackNow` returns them.
void resolve(engine::GpuContext& gpu, const ReadbackTicket& ticket, std::span<uint8_t> destination);

/// Views `values` as the `uint8_t` bytes a resolve fills, as `std::as_writable_bytes` views them as
/// `std::byte`, so a typed destination is filled in place without a staging copy.
template <typename T, std::size_t Extent>
std::span<uint8_t> resolveBytes(std::span<T, Extent> values) {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_const_v<T>,
                  "resolveBytes: a resolve writes plain values");
    return {reinterpret_cast<uint8_t*>(values.data()), values.size_bytes()};
}

/// Waits for the GPU to go idle and returns level 0 of `texture`, tightly packed.
///
/// Valid only outside a frame: an open frame would never complete under the wait. RojoRHI v1
/// cannot check that, so the caller must. `current` is the texture's use at that point, unused on
/// v1, which reads the texture directly.
std::vector<uint8_t> readbackNow(engine::GpuContext& gpu, const engine::GpuTexture& texture,
                                 TextureUse current);
/// Waits for the GPU to go idle and returns `region` of `buffer`.
///
/// Valid only outside a frame, as the texture form is. `region` must lie inside the buffer; on
/// RojoRHI v1 it must also start at offset zero, and `current` is unused.
std::vector<uint8_t> readbackNow(engine::GpuContext& gpu, const engine::GpuBuffer& buffer,
                                 BufferUse current, BufferRegion region);

} // namespace lmx::render
