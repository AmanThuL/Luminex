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

/// Mirrors the incumbent Metal 4 backend's fixed per-frame transient-uniform capacity, as recorded
/// by ADR 0010 and the M5.1 evidence this seam's baseline path reproduces (still present, under its
/// own name, in the backend source the `m5.2-baseline` tag checks out). Backend-private and not
/// part of the public `RHI/Include` surface `FrameDataBench` links against, so this is a
/// deliberately duplicated, independent tracker -- not a shared symbol -- exactly as the archived
/// M5.1 adapters at tag `m5.1-noapi-evidence` had to duplicate it to decide client-side whether a
/// call would fit before making it, since the incumbent path's overflow was a fatal assert with no
/// recoverable failure to branch on.
inline constexpr uint64_t kRingCapacityBytes = 256 * 1024;
/// Mirrors the incumbent backend's fixed per-frame transient-uniform offset alignment: every write
/// on that path rounds its occupied span up to this boundary regardless of its own size.
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
/// Only the baseline side needs that state: on this candidate path `deliverPerDrawData`'s body is a
/// single `bindFrameData` call. The struct still carries the full ring model rather than shrinking
/// to what this side uses, because the paired driver builds its baseline executable from the frozen
/// `m5.2-baseline` tag, whose copy of this file runs the ring/overflow logic below, and links both
/// executables against the same `DeliveryContext` shape through this header's frozen signature.
/// Shrinking it on the candidate side alone would buy nothing the signature freeze does not already
/// provide. The ring-model members below and `deliverPerDrawData`'s `context` parameter are
/// therefore unused on the candidate path.
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
/// through, and nothing else. On this candidate path it is one `CommandList::bindFrameData` call
/// against the growable per-slot arena -- one allocation, one copy, one address bind, no capacity
/// to track and no overflow buffer to create, unlike the ring-based incumbent this seam replaced
/// (still exercised by the baseline executable built from the `m5.2-baseline` tag's copy of this
/// file). `context` is threaded through unused; see the note on `DeliveryContext` above for why the
/// signature keeps it.
///
/// `slot` is the argument-table buffer slot the caller's pipeline reads the block from; `data`/
/// `size` name the bytes to copy, which the caller may reuse or free immediately after the call
/// returns. Valid only inside a render or compute pass, per `bindFrameData`'s own contract.
inline void deliverPerDrawData([[maybe_unused]] DeliveryContext& context,
                               rhi::CommandList& commands, uint32_t slot, const void* data,
                               uint64_t size) {
    commands.bindFrameData(slot, data, size);
}

} // namespace lmx::bench
