//----------------------------------------------------------------------------------------------------------------------
/// @file Metrics.h
/// @brief Declares the adapter-neutral shapes M5.1 Stage 4's measurement instrumentation reports
///        through (plan Stage 4 item 4, spec sections 8-9): per-frame binding traffic and
///        allocation snapshots, read from each adapter after the fact rather than computed inside a
///        timed region.
///
///        The two adapters count genuinely different things -- the prototype's address-first
///        traffic (setAddress/pushRoot/bindless-table writes) has no incumbent equivalent, and the
///        incumbent's object-shaped traffic (bind*/setUniforms calls, per-frame buffer creation)
///        has no prototype equivalent -- so `FrameBindingCounters` below keeps every field its own
///        adapter actually produces named separately rather than collapsing dissimilar concepts
///        into one shared "bind count": a field this adapter's model has no concept of stays zero,
///        which is itself part of the recorded evidence (spec section 9: "primary metrics are ...
///        counted
///        ... under the same counting rules"), not a gap.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lmx::noapi::bench {

/// Binding traffic one adapter produced encoding one frame (spec section 9's binding-traffic
/// dimension and section 8's barrier count). Fields with no meaning on one side stay zero there --
/// see this file's header comment for why they are not collapsed into a shared count.
struct FrameBindingCounters {
    // Prototype-side (address-first): zero on the incumbent adapter.
    uint64_t setAddressCalls = 0; ///< Every `MTL4::ArgumentTable::setAddress` (bindAddress calls).
    uint64_t pushRootCalls = 0;   ///< Every `pushRoot` call.
    uint64_t pushRootBytes = 0;   ///< Bytes `pushRoot` copied into the frame ring.
    uint64_t tableWriteCalls = 0; ///< Bindless table slot writes this frame.
    uint64_t tableWriteBytes = 0; ///< Bytes written to the bindless table this frame.

    // Incumbent-side (object-shaped RHI): zero on the prototype adapter.
    uint64_t bindCalls = 0;         ///< Every bindTexture/bindBuffer/bindSampler/bindStorage* call.
    uint64_t setUniformsCalls = 0;  ///< Every `setUniforms` call.
    uint64_t setUniformsBytes = 0;  ///< Bytes passed to `setUniforms`.
    uint64_t bufferCreateCalls = 0; ///< Per-frame buffer creations counted as binding delivery.
    uint64_t bufferCreateBytes = 0; ///< Bytes of those per-frame buffer creations.

    // Shared: both adapters populate this from their own barrier vocabulary.
    uint64_t barrierCalls = 0; ///< Barrier/transition primitives emitted this frame.
};

/// Creation call counts and resident bytes for one adapter, sampled at a point in time (spec
/// section 9's allocation dimension: "allocation call counts and resident bytes ... at end of setup
/// and end of run").
///
/// A scored comparison needs one common basis, and the public RHI cannot produce the prototype's
/// true Metal-reported allocated size (its `Buffer`/`Texture` interfaces report only the caller's
/// requested logical size, never Metal's padded/aligned allocation) -- so `requestedBytes` is the
/// scored figure on BOTH sides: the incumbent's own best-available number (computed from the same
/// descriptors it created every resource with) and the prototype's equivalent computed the same way
/// (summed requested allocation sizes, not Metal's rounded-up ones). `metalReportedBytes` carries
/// the prototype's true figure as additional, clearly separate evidence -- `std::nullopt` on the
/// incumbent, which cannot produce it -- so a comparison can never silently mix the two bases
/// (spec's fairness rule: "any asymmetry you cannot avoid must be reported, not silently
/// accepted").
struct AllocationSnapshot {
    uint32_t textureCreateCalls =
        0;                          ///< Textures created (live count; see each adapter's own note).
    uint32_t bufferCreateCalls = 0; ///< Buffers/allocations created (live or cumulative; see note).
    uint32_t samplerCreateCalls = 0;            ///< Samplers created.
    uint32_t pipelineCreateCalls = 0;           ///< Graphics + compute pipelines created.
    uint64_t requestedBytes = 0;                ///< Scored figure; both sides, same computation.
    std::optional<uint64_t> metalReportedBytes; ///< Prototype only; nullopt on the incumbent.
};

/// One `--measure` invocation's collected samples (plan Stage 4 item 4, spec section 8's protocol):
/// per-frame timed-region durations over the measured (post-warmup) frames, the binding/barrier
/// counters observed, whether those counters were identical on every measured frame, and allocation
/// snapshots at end of setup and end of run. This is the in-process shape `--measure` fills in;
/// NoApiBenchMain.cpp serializes it to the JSON blob collect.py consumes.
struct MeasuredRun {
    bool ok = false;
    std::string error;
    std::vector<uint64_t> perFrameTimedRegionNs; ///< Measured frames only; warm-up excluded.
    FrameBindingCounters counters{};             ///< From the last measured frame.
    bool countersStableAcrossFrames = true; ///< False if any measured frame's counters differed.
    AllocationSnapshot endOfSetup{};
    AllocationSnapshot endOfRun{};
};

} // namespace lmx::noapi::bench
