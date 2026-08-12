//----------------------------------------------------------------------------------------------------------------------
/// @file Runner.h
/// @brief Declares Runner for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Declares the adapter-neutral M5.1 bench runner: the frame loop, per-frame readback
///        capture, deterministic hashing and raw dumping, and the interface both encoders'
///        NoApiBench adapters implement (spec section 6, plan Stage 3 item 1).

#pragma once
#include "Bench/Metrics.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace lmx::experimental::noapi::bench {

/// FNV-1a 64-bit over `bytes` -- the representative graph's deterministic per-frame output hash
/// (this bench's parity and determinism oracle: two runs of one adapter must produce identical
/// sequences, and Stage 3's exit gate compares the two adapters' sequences frame for frame). Not
/// cryptographic; chosen for being dependency-free and exactly reproducible byte for byte.
uint64_t fnv1a64(std::span<const uint8_t> bytes);

/// What one encoder must implement to run the representative graph end to end. The maintained-RHI
/// adapter (RhiAdapter) and the prototype adapter execute the same shared workload manifest
/// (Workload/RepresentativeGraph.h) through this one interface, so runBench below drives either
/// identically and neither adapter's own frame loop differs from the other's shape.
class Adapter {
public:
    virtual ~Adapter() = default;

    /// One-time setup: device creation, pipeline compilation, resource creation and upload,
    /// synthetic asset generation, and -- for an adapter that plans its schedule and barriers from
    /// a declarative graph -- the one-time compile that derives them (spec section 8: "the graph
    /// planner must NOT run per-frame or inside any timed region"). Runs entirely outside the timed
    /// region every runFrame() call documents. Returns false and logs a diagnostic on failure.
    virtual bool setup() = 0;

    /// Runs one frame of the representative graph -- frame `frameIndex` in [0,
    /// kCorrectnessFrameCount) for a correctness run -- and fills `outReadback` with R9's full
    /// readback bytes once the GPU work has completed and been waited on. `frameIndex` selects this
    /// frame's camera pose and R10 staging content per the frozen draw/update policy
    /// (Workload/DrawPolicy.h); the schedule, barriers, resources, and pipelines are identical
    /// every frame.
    virtual void runFrame(uint32_t frameIndex, std::vector<uint8_t>& outReadback) = 0;

    /// Releases GPU resources. Called once after every frame has run.
    virtual void teardown() = 0;

    /// Wall time of the most recent `runFrame` call's timed region (spec section 8), in
    /// nanoseconds, measured with `std::chrono::steady_clock` identically on both adapters.
    ///
    /// Valid after at least one `runFrame` call; each adapter's own runFrame() documents exactly
    /// which of its statements the region spans.
    virtual uint64_t lastFrameTimedRegionNs() const = 0;

    /// Binding traffic and barrier count the most recent `runFrame` call produced (spec section 9).
    virtual FrameBindingCounters lastFrameBindingCounters() const = 0;

    /// Creation call counts and resident bytes as of the moment this is called (spec section 9's
    /// allocation dimension). Callable after `setup()` and again after the run's last `runFrame` to
    /// get the "end of setup" and "end of run" snapshots the spec's allocation dimension names.
    virtual AllocationSnapshot allocationSnapshot() const = 0;
};

/// Parsed NoApiBench `--run-graph` options.
struct RunOptions {
    std::string graph;             ///< The value after `--run-graph=` ("rhi" or "proto").
    uint32_t frames = 0;           ///< Frame count (`--frames=N`).
    std::filesystem::path dumpDir; ///< Directory raw per-frame readback dumps are written to.
};

/// Drives `adapter` through `options.frames` frames of the representative graph: setup(), then for
/// each frame runFrame() followed by a hash and a raw dump of the readback bytes to
/// `<dumpDir>/frame_NNNN.bin`, then teardown(). Prints one line per frame ("frame N hash=<hex>") to
/// stdout and a final summary. Returns 0 when every frame produced a non-empty readback and its
/// dump was written successfully, 1 on any failure (setup failure, an empty readback, or a write
/// failure).
int runBench(Adapter& adapter, const RunOptions& options);

} // namespace lmx::experimental::noapi::bench
