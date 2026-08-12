//----------------------------------------------------------------------------------------------------------------------
/// @file DeliverPerDrawData.h
/// @brief Declares the single per-draw dynamic-data delivery seam FrameDataBench measures.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Core/Assert.h"
#include "RHI/RHI.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace lmx::bench {

/// Mirrors `RHI/Backends/Metal4/Source/Metal4Device.h`'s `kUniformRingBytes`. Backend-private and
/// not part of the public `RHI/Include` surface `FrameDataBench` links against, so this is a
/// deliberately duplicated, independent tracker -- not a shared symbol -- exactly as the incumbent
/// M5.1 adapters (`Experiments/NoApi/Bench/RhiAdapter.cpp`, read-only, never included or linked)
/// had to duplicate it to decide client-side whether a call would fit before making it, since
/// `setUniforms` overflow is a fatal `LMX_ASSERT` with no recoverable failure to branch on.
inline constexpr uint64_t kRingCapacityBytes = 256 * 1024;
/// Mirrors `Metal4Common.h`'s `kUniformOffsetAlignment`: every `setUniforms` write rounds its
/// occupied span up to this boundary regardless of its own size.
inline constexpr uint64_t kRingAlignmentBytes = 256;

/// Rounds `value` up to the next multiple of `alignment` (`alignment` a power of two).
constexpr uint64_t alignUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

/// Per-run state the delivery seam needs to reproduce the incumbent RHI's exact per-frame
/// uniform-ring behavior: "the fixed uniform ring while blocks fit, then caller-side per-draw
/// buffer creation beyond capacity" (ADR 0010 / M5.1 evidence section 7.1) -- not a fatal abort,
/// which is what a caller that ignored capacity would hit, and not what the measured "old
/// allocation path" is.
///
/// Stage 3 contract: only `deliverPerDrawData`'s *body* becomes a single `bindFrameData` call, and
/// this struct shrinks to nothing (a growable arena needs no client-side ring tracking or overflow
/// retention). The function *signature* below -- context, commands, slot, data, size -- and every
/// Runner.cpp call site stay exactly as they are; only the body swaps.
struct DeliveryContext {
    /// `device` must outlive every call this context is passed to.
    explicit DeliveryContext(rhi::Device& device) : device(&device) {}

    rhi::Device* device = nullptr;

    /// Mirrors the backend's per-frame ring bump cursor. Reset to zero once per frame by
    /// `beginFrame()`, matching the real ring's own per-frame reset.
    uint64_t ringCursor = 0;

    /// The previous frame's caller-side overflow buffers, retained until `beginFrame()` retires
    /// (destroys) them. Safe to destroy at that point because the harness waits the device fully
    /// idle after every frame before the next one ever records a command (Runner.cpp) -- the same
    /// safety argument `RhiAdapter.cpp`'s own per-draw buffer replacement relies on.
    std::vector<std::unique_ptr<rhi::Buffer>> overflowBuffers;

    /// Cumulative caller-side overflow buffers created across the whole run. `F-FIT-512` and both
    /// static workloads must finish a run with this at zero -- they never exceed the simulated
    /// ring.
    uint64_t overflowBufferCreations = 0;

    /// Resets the simulated ring cursor and retires the previous frame's overflow buffers. Called
    /// once per frame, inside the timed region, as the first step after `Device::beginFrame()`: the
    /// retirement destruction is real, counted CPU cost -- the allocation cliff this benchmark
    /// exists to measure -- not bookkeeping to hide outside the timer.
    void beginFrame() {
        ringCursor = 0;
        overflowBuffers.clear();
    }
};

/// The one production-owned seam every dynamic FrameDataBench workload delivers its per-draw block
/// through, and nothing else. Reproduces the incumbent RHI's exact behavior: if `size` (aligned to
/// `kRingAlignmentBytes`) still fits under `kRingCapacityBytes` from `context.ringCursor`, delivers
/// through `CommandList::setUniforms` exactly like the pre-migration production renderer and
/// advances the simulated cursor; otherwise creates a fresh `rhi::Buffer` with `data` as its
/// initial content (`rhi::Buffer` has no public write path after creation -- the only other legal
/// way to vary a bound slot's contents per draw) and binds it with `CommandList::bindBuffer`,
/// leaving the simulated cursor untouched since no ring write occurred. M5.2 Stage 3 changes only
/// this function's body to call `CommandList::bindFrameData` once the candidate path exists, so the
/// same benchmark binary keeps measuring both the baseline and (source-unmodified elsewhere) the
/// post-migration candidate; `DeliveryContext`'s ring-tracking and retention members will no longer
/// be needed at that point, but this signature will not need to change.
///
/// `slot` is the argument-table buffer slot the caller's pipeline reads the block from; `data`/
/// `size` name the bytes to copy, which the caller may reuse or free immediately after the call
/// returns. Valid only inside a render or compute pass, per `setUniforms`' own contract.
inline void deliverPerDrawData(DeliveryContext& context, rhi::CommandList& commands, uint32_t slot,
                               const void* data, uint64_t size) {
    const uint64_t alignedSize = alignUp(size, kRingAlignmentBytes);
    if (context.ringCursor + alignedSize <= kRingCapacityBytes) {
        commands.setUniforms(slot, data, size);
        context.ringCursor += alignedSize;
        return;
    }
    auto buffer =
        context.device->createBuffer({.size = size, .label = "framedatabench.overflowBlock"}, data);
    LMX_ASSERT(buffer.has_value(), "FrameDataBench: overflow buffer creation failed");
    commands.bindBuffer(slot, **buffer);
    context.overflowBuffers.push_back(std::move(*buffer));
    context.overflowBufferCreations += 1;
}

} // namespace lmx::bench
