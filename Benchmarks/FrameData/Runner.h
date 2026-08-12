//----------------------------------------------------------------------------------------------------------------------
/// @file Runner.h
/// @brief Declares the timed-region runner for one FrameDataBench workload.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Workload.h"

#include "RHI/Metal4/Metal4FrameData.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lmx::bench {

/// Run parameters a caller controls from the command line; defaults match spec section 11's frozen
/// protocol (16 warm-up + 256 measured frames).
struct RunConfig {
    uint32_t warmupFrames = 16;    ///< Frames run and discarded before timing starts.
    uint32_t measuredFrames = 256; ///< Frames whose timed region is recorded.
    bool verify = false;           ///< Reads back the final measured frame and digests it.
};

/// One workload run's outcome: either a populated result or `error` naming what failed.
struct RunResult {
    bool ok = false;   ///< False when setup or a GPU call failed; `error` explains why.
    std::string error; ///< Failure detail; empty when `ok`.
    /// One entry per measured frame, in frame order: CPU wall time of that frame's timed region
    /// (device->beginFrame() through device->endFrame()), in nanoseconds.
    std::vector<uint64_t> perFrameTimedRegionNs;
    /// Median of perFrameTimedRegionNs; 0 when perFrameTimedRegionNs is empty.
    uint64_t medianNs = 0;
    /// FNV-1a digest of the final measured frame's readback; set only when RunConfig::verify and
    /// `ok`.
    std::optional<uint64_t> digest;
    /// Caller-side overflow buffers created across the whole run (DeliverPerDrawData.h) when a
    /// dynamic block did not fit the simulated incumbent uniform ring. Zero for both static
    /// workloads and for any dynamic workload whose per-frame bytes never exceed the ring.
    uint64_t overflowBufferCreations = 0;
    /// overflowBufferCreations divided by the run's total frame count (warmup + measured). Exact,
    /// not an average: which draws overflow is a fixed function of draw index and block size, so
    /// every frame of one workload creates the same number of overflow buffers. Recorded so an
    /// analysis can decompose per-buffer cost scaling without re-deriving this from the raw count.
    uint64_t overflowBufferCreationsPerFrame = 0;

    /// Candidate-only counter evidence (`RHI/Include/RHI/Metal4/Metal4FrameData.h`): the
    /// device's frame-data counters snapshotted immediately after warm-up completes, before the
    /// first measured frame's timed region begins. Unset when `config.warmupFrames == 0` (no
    /// "after warm-up, before measurement" boundary exists) or on an error path before the loop
    /// starts; always set for the frozen 16-warmup protocol. The baseline binary this struct also
    /// serves is built from the frozen `m5.2-baseline` tag tree, whose copy of this file predates
    /// these fields entirely, so "unset" is not how baseline/candidate differ here.
    std::optional<rhi::metal4::FrameDataCounters> frameDataCountersAfterWarmup;
    /// The same device's frame-data counters snapshotted immediately after the last measured frame
    /// retires. Doubles as the run's final/total counters -- `calls`, `bytes`, `addressBinds`,
    /// `pageCreations`, and per-slot occupancy accumulated across the whole run (warm-up and
    /// measured frames both), since the arena never resets outside a frame boundary.
    std::optional<rhi::metal4::FrameDataCounters> frameDataCountersAfterMeasurement;
};

/// Runs `spec` for `config.warmupFrames + config.measuredFrames` frames on a freshly created RHI
/// device, timing each measured frame's `device->beginFrame()`..`device->endFrame()` span. Dynamic
/// workloads deliver a changing per-draw block through `deliverPerDrawData` every draw of every
/// frame; static workloads bind buffers created once before the loop and write no per-frame data.
/// With `config.verify` set, reads back the final measured frame's render target and digests it
/// after the loop completes -- readback is never part of a timed frame.
RunResult runWorkload(const WorkloadSpec& spec, const RunConfig& config);

/// Runs a small set of self-checks against this binary's own pure host-side logic -- ring
/// alignment arithmetic, the median statistic, the frozen workload table, and disjoint grid
/// placement for a non-square draw count -- and returns true iff every check passed. Touches no
/// GPU device; `--selftest` on the command line is the only caller. Every failing check's
/// description is appended to `failures`.
bool runSelfTests(std::vector<std::string>& failures);

} // namespace lmx::bench
