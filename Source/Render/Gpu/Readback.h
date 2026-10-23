//----------------------------------------------------------------------------------------------------------------------
/// @file Readback.h
/// @brief Declares deferred readback outcomes and preserving copies with exact physical state.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Graph/RenderGraphLowering.h"
#include "Render/Graph/TextureStateTable.h"

#include <rojoRHI/RHI.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace lmx::render {

/// A byte range of a buffer.
struct BufferRegion {
    uint64_t offset = 0; ///< First byte of the range.
    uint64_t size = 0;   ///< Number of bytes in the range.
};

/// Opaque shared storage for a recorded copy, mapped source, empty operation or causal error.
struct ReadbackOperation;

/// One deferred readback outcome; a default ticket has no recorded operation and cannot resolve.
/// Copies share staging ownership; retain a copy through resolution.
/// The context/device must outlive every copy of the ticket.
/// A mapped source must remain alive and unmodified until resolution; a staged source must remain
/// alive until its recorded frame retires. Metadata names the identity recorded, not its owner.
struct ReadbackTicket {
    rojoRHI::Buffer buffer{};   ///< Recorded buffer identity, or null for a texture.
    rojoRHI::Texture texture{}; ///< Recorded texture identity, or null for a buffer.
    BufferRegion region;        ///< Original buffer region or packed mip-zero texture bytes.
    uint64_t frame = 0;         ///< Actual frame whose completion makes the bytes available.

private:
    friend struct ReadbackAccess;
    std::shared_ptr<ReadbackOperation> m_operation;
};

/// Returns packed texel bytes, including D32Float and RGB10A2; stencil/BC/Undefined return zero.
uint32_t packedTexelBytes(rojoRHI::Format format);
/// Returns tightly packed mip-zero bytes for one two-dimensional slice of desc.
uint64_t packedTextureBytes(const rojoRHI::TextureDesc& desc);
/// Returns the byte region covering the complete buffer.
BufferRegion wholeBuffer(const engine::GpuBuffer& buffer);

/// Creates internal source metadata; a nonempty Gpu source still needs a recorded staging copy.
/// Mapped source metadata borrows the actual pointer and requires source lifetime through resolve.
ReadbackTicket makeReadbackTicket(const engine::GpuBuffer& buffer, BufferRegion region,
                                  uint64_t frame);
/// Creates internal texture metadata; a staging copy must be attached before it can resolve.
ReadbackTicket makeReadbackTicket(const engine::GpuTexture& texture, uint64_t frame);
/// Creates a deferred causal failure, used when staging creation fails before any copy commands.
ReadbackTicket makeReadbackTicket(Error error);

/// Records a preserving buffer copy, or borrows mapped Upload/Readback bytes until frame
/// retirement. Gpu copies require four-byte aligned offset/size; empty regions record no allocation
/// or command. current names the actual prior stage/access frontier; its layout member is unused.
ReadbackTicket recordReadback(engine::GpuContext& gpu, rojoRHI::CommandList& commands,
                              const engine::GpuBuffer& buffer, SyncPoint current,
                              BufferRegion region);
/// Records a preserving mip-zero/layer-zero texture copy using exact known table state.
ReadbackTicket recordReadback(engine::GpuContext& gpu, rojoRHI::CommandList& commands,
                              const engine::GpuTexture& texture, const TextureStateTable& current);
/// Records the same copy with exact known mip-zero/layer-zero state, without holes or overlaps.
/// Undefined cannot be preserved. A restored None/None frontier needs a host frame wait before
/// later GPU use, or an explicit dependency establishing that later use. Other cells are untouched.
ReadbackTicket recordReadback(engine::GpuContext& gpu, rojoRHI::CommandList& commands,
                              const engine::GpuTexture& texture,
                              std::span<const TextureRangeState> current);

/// Waits for the recorded frame and copies into destination, or returns the unchanged causal error.
/// Texture destinations must hold all packed bytes; buffer destinations may take a leading subset.
/// Default or metadata-only Gpu tickets are misuse; a recorded empty ticket is a valid success.
Result<void> resolve(engine::GpuContext& gpu, const ReadbackTicket& ticket,
                     std::span<uint8_t> destination);

/// Views writable trivially-copyable values as the uint8 bytes resolve fills.
template <typename T, std::size_t Extent>
std::span<uint8_t> resolveBytes(std::span<T, Extent> values) {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_const_v<T>,
                  "resolveBytes: a resolve writes plain values");
    return {reinterpret_cast<uint8_t*>(values.data()), values.size_bytes()};
}

/// Reads one texture slice outside a frame, restoring its exact known table state.
Result<std::vector<uint8_t>> readbackNow(engine::GpuContext& gpu, const engine::GpuTexture& texture,
                                         const TextureStateTable& current);
/// Reads one texture slice outside a frame with exact explicit mip-zero/layer-zero state.
/// A private copy frame is submitted and waited; allocation failure consumes no frame.
Result<std::vector<uint8_t>> readbackNow(engine::GpuContext& gpu, const engine::GpuTexture& texture,
                                         std::span<const TextureRangeState> current);
/// Reads a buffer region outside a frame; mapped reads wait but consume no frame.
/// Gpu copies require four-byte aligned offset/size; unaligned mapped regions are supported.
Result<std::vector<uint8_t>> readbackNow(engine::GpuContext& gpu, const engine::GpuBuffer& buffer,
                                         SyncPoint current, BufferRegion region);

} // namespace lmx::render
