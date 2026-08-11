//----------------------------------------------------------------------------------------------------------------------
/// @file CommandBuffer.h
/// @brief Declares flat command recording over addresses, stages, and pass boundaries.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/LinearAllocator.h"
#include "NoApi/RenderPass.h"
#include "NoApi/Texture.h"
#include "NoApi/Types.h"

#include "Core/Assert.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace lmx::noapi {

/// Binding traffic one command buffer's recording produced (M5.1 spec section 9's binding-traffic
/// and barrier dimensions). Every field is a plain counter bumped at the one choke point that
/// performs the traffic it counts, so reading it after `endCommands` adds no measurable work inside
/// a timed region -- this is a snapshot of counters already updated in place, not a computation.
struct CommandBufferStats {
    uint64_t setAddressCalls =
        0;                     ///< Every `setAddress` the recording made (`bindAddress`'s count).
    uint64_t rootCalls = 0;    ///< Every `pushRoot` call.
    uint64_t rootBytes = 0;    ///< Bytes `pushRoot` copied into the frame allocator.
    uint64_t barrierCalls = 0; ///< Barrier primitives emitted onto an encoder.
};

/// Returns the binding traffic `commands` has produced since `beginCommands` reset its counters.
///
/// Valid at any point in the recording, including after `endCommands`; the counters are not cleared
/// again until the context is reused by a later `beginCommands`.
CommandBufferStats commandBufferStats(const CommandBuffer* commands);

/// Begins recording a transient command buffer.
///
/// The returned buffer is valid until `submit` consumes it; there is no reset and no reuse.
/// `rootAllocator` supplies the storage `pushRoot` suballocates and must belong to a frame slot the
/// caller has proven retired — `FrameRing` owns that proof. Recording is single-threaded per
/// command buffer.
CommandBuffer* beginCommands(Queue* queue, LinearAllocator* rootAllocator, std::string_view label);

/// Ends recording and makes the command buffer eligible for submission.
///
/// Asserts when a render pass is still open.
void endCommands(CommandBuffer* commands);

/// Submits recorded command buffers in order and optionally signals a semaphore afterwards.
///
/// Every submitted buffer must have been ended, and each is consumed: using one after submission is
/// a caller contract violation and asserts. When `signal` is non-null, the semaphore reaches
/// `signalValue` once every submitted buffer has completed.
void submit(Queue* queue, std::span<CommandBuffer* const> commands, Semaphore* signal = nullptr,
            uint64_t signalValue = 0);

/// Opens a capture and profiling group that groups every following command until the matching pop.
void pushDebugGroup(CommandBuffer* commands, std::string_view label);

/// Closes the innermost group opened by `pushDebugGroup`.
///
/// Asserts when no group is open.
void popDebugGroup(CommandBuffer* commands);

/// Copies `size` bytes of root data into the frame's allocator and returns its GPU address.
///
/// This is the whole per-draw binding mechanism: the returned address is passed to a draw or
/// dispatch, and the shader receives it as a pointer to its own root struct. Nothing is bound, and
/// no descriptor object is created. `alignment` must be a power of two and at least
/// `Capabilities::minRootDataAlignment`.
///
/// Asserts when the frame's allocator cannot satisfy the request, which diagnoses an undersized
/// ring at the offending draw rather than at frame end.
GpuAddress pushRoot(CommandBuffer* commands, const void* data, uint64_t size,
                    uint64_t alignment = kDefaultAlignment);

/// Copies one trivially copyable root block into the frame's allocator and returns its GPU address.
///
/// The block's type is the shared CPU and GPU contract: the same declaration compiles on both
/// sides, so there is no layout description anywhere in this interface.
template <typename T>
GpuAddress pushRoot(CommandBuffer* commands, const T& block) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "GPU-visible root data must be trivially copyable");
    return pushRoot(commands, &block, sizeof(T), alignof(T));
}

/// Makes `table` the bindless table every following shader indexes.
///
/// A device owns one table, so this is normally called once per command buffer. Asserts when
/// called inside a render pass.
void setBindlessTable(CommandBuffer* commands, const BindlessTable* table);

/// Copies `size` bytes from one GPU address to another.
///
/// The ranges must not overlap, must lie inside live allocations, and must satisfy
/// `Capabilities::minCopyAlignment`; all three are caller contracts and assert. Asserts when
/// called inside a render pass.
void copyMemory(CommandBuffer* commands, GpuAddress destination, GpuAddress source, uint64_t size);

/// Fills `size` bytes at `destination` with the byte value `value`.
///
/// @copydetails copyMemory
void fillMemory(CommandBuffer* commands, GpuAddress destination, uint64_t size, uint8_t value);

/// Copies linear memory into one texture subresource region.
///
/// The texture must declare `TextureUsage::CopyDestination` and `region` must lie inside the
/// addressed subresource; both are caller contracts and assert. Asserts when called inside a
/// render pass.
void copyToTexture(CommandBuffer* commands, Texture* destination, const TextureRegion& region,
                   GpuAddress source, const MemoryImageLayout& sourceLayout);

/// Copies one texture subresource region into linear memory.
///
/// The texture must declare `TextureUsage::CopySource`; this is a caller contract and asserts.
/// Asserts when called inside a render pass.
void copyFromTexture(CommandBuffer* commands, GpuAddress destination,
                     const MemoryImageLayout& destinationLayout, const Texture* source,
                     const TextureRegion& region);

/// Orders `producer` stages before `consumer` stages and performs the declared cache maintenance.
///
/// No resources are named. The stage pair is the dependency, and `hazards` names only the caches
/// hardware does not flush automatically. Render pass boundaries emit no barrier of their own, so
/// every cross-pass dependency is stated here.
void barrier(CommandBuffer* commands, Stage producer, Stage consumer,
             Hazard hazards = Hazard::None);

/// Signals a counter in GPU memory once `producer` stages have completed.
///
/// `counter` addresses eight bytes of GPU memory the caller owns; no synchronization object is
/// created. Pairing this with `waitBefore` and placing independent work between them is the split
/// form of `barrier`, and with `SignalOp::AtomicMax` it gives GPU-side timeline semantics.
void signalAfter(CommandBuffer* commands, Stage producer, GpuAddress counter, uint64_t value,
                 SignalOp op);

/// Blocks `consumer` stages until the counter at `counter` satisfies the comparison.
///
/// `mask` restricts the comparison to selected bits, which lets one wait cover several producers
/// that signalled with `SignalOp::AtomicOr`. `hazards` performs the same cache maintenance as the
/// equivalent `barrier` call.
void waitBefore(CommandBuffer* commands, Stage consumer, GpuAddress counter, uint64_t value,
                CompareOp op, Hazard hazards = Hazard::None, uint64_t mask = ~uint64_t{0});

/// Makes `pipeline` the pipeline every following draw or dispatch uses.
///
/// Asserts when a compute pipeline is set inside a render pass or a graphics pipeline outside one.
void setPipeline(CommandBuffer* commands, const Pipeline* pipeline);

/// Makes `state` the depth and stencil configuration of the open render pass.
///
/// Asserts when called outside a render pass.
void setDepthStencilState(CommandBuffer* commands, const DepthStencilState* state);

/// Sets the rasterizer's output rectangle and depth range for the open render pass.
void setViewport(CommandBuffer* commands, const Viewport& viewport);

/// Sets the rasterizer's scissor rectangle for the open render pass.
void setScissor(CommandBuffer* commands, const ScissorRect& scissor);

/// Sets which triangle facing the rasterizer discards for the open render pass.
void setCullMode(CommandBuffer* commands, CullMode cull);

/// Sets which winding order counts as front-facing for the open render pass.
void setFrontFace(CommandBuffer* commands, Winding winding);

/// Sets the depth bias applied to rasterized depth for the open render pass.
void setDepthBias(CommandBuffer* commands, float constantBias, float slopeScale, float clamp);

/// Sets the stencil reference value compared against the stencil buffer.
void setStencilReference(CommandBuffer* commands, uint32_t reference);

/// Begins a render pass with the attachment set `desc` declares.
///
/// Asserts when a render pass is already open, when an attachment lacks the matching usage, or
/// when the attachment formats do not agree with the pipeline set inside the pass.
void beginRenderPass(CommandBuffer* commands, const RenderPassDesc& desc);

/// Ends the open render pass.
///
/// Asserts when no render pass is open.
void endRenderPass(CommandBuffer* commands);

/// Draws non-indexed primitives with per-stage root data supplied by address.
///
/// `vertexRoot` and `pixelRoot` may be the same address when both stages read the same block, or
/// `kNullAddress` when a stage reads none. Asserts when called outside a render pass or with no
/// graphics pipeline set.
void draw(CommandBuffer* commands, GpuAddress vertexRoot, GpuAddress pixelRoot,
          uint32_t vertexCount, uint32_t instanceCount = 1);

/// Draws indexed primitives, reading indices directly from a GPU address.
///
/// `indices` must be aligned to the index width and must address at least `indexCount` elements;
/// both are caller contracts and assert.
void drawIndexed(CommandBuffer* commands, GpuAddress vertexRoot, GpuAddress pixelRoot,
                 GpuAddress indices, IndexKind indexKind, uint32_t indexCount,
                 uint32_t instanceCount = 1);

/// Draws non-indexed primitives whose arguments the GPU supplies at `arguments`.
///
/// When a preceding pass wrote `arguments`, the caller owes a barrier carrying
/// `Hazard::DrawArguments`; this interface never inserts one.
void drawIndirect(CommandBuffer* commands, GpuAddress vertexRoot, GpuAddress pixelRoot,
                  GpuAddress arguments);

/// Draws indexed primitives whose arguments the GPU supplies at `arguments`.
///
/// @copydetails drawIndirect
void drawIndexedIndirect(CommandBuffer* commands, GpuAddress vertexRoot, GpuAddress pixelRoot,
                         GpuAddress indices, IndexKind indexKind, GpuAddress arguments);

/// Dispatches a compute grid with root data supplied by address.
///
/// Asserts when called inside a render pass or with no compute pipeline set.
void dispatch(CommandBuffer* commands, GpuAddress root, uint32_t groupsX, uint32_t groupsY,
              uint32_t groupsZ);

/// Dispatches a compute grid whose dimensions the GPU supplies at `arguments`.
///
/// @copydetails drawIndexedIndirect
void dispatchIndirect(CommandBuffer* commands, GpuAddress root, GpuAddress arguments);

} // namespace lmx::noapi
