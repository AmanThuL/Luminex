//----------------------------------------------------------------------------------------------------------------------
/// @file Runner.h
/// @brief Declares the timed-region runner for one FrameDataBench workload.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Workload.h"

#include <rojoRHI/RHI.h>

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
    /// (`Device::beginFrame()` through `Device::endFrame()`), in nanoseconds.
    std::vector<uint64_t> perFrameTimedRegionNs;
    /// Median of perFrameTimedRegionNs; 0 when perFrameTimedRegionNs is empty.
    uint64_t medianNs = 0;
    /// FNV-1a digest of the final measured frame's readback; set only when RunConfig::verify and
    /// `ok`.
    std::optional<uint64_t> digest;
    /// The device's counters snapshotted immediately after warm-up retires, before the first
    /// measured frame's timed region begins. Unset when `config.warmupFrames == 0` (no "after
    /// warm-up, before measurement" boundary exists) or on an error path before the loop starts.
    std::optional<rojoRHI::Counters> countersAfterWarmup;
    /// The same device's counters snapshotted immediately after the last measured frame retires,
    /// before the verification readback: the run's totals over warm-up and measured frames both.
    std::optional<rojoRHI::Counters> countersAfterMeasurement;
};

/// Runs `spec` for `config.warmupFrames + config.measuredFrames` frames on a freshly created RHI
/// device, timing each measured frame's `Device::beginFrame()`..`Device::endFrame()` span. Dynamic
/// workloads deliver a changing per-draw block through `deliverPerDrawData` every draw of every
/// frame; static workloads address buffers created once before the loop and upload only their
/// roots. With `config.verify` set, copies the final measured frame's render target to a readback
/// buffer in one more frame after the loop and digests it -- readback is never part of a timed
/// frame.
RunResult runWorkload(const WorkloadSpec& spec, const RunConfig& config);

/// Runs a small set of self-checks against this binary's own pure host-side logic -- the median
/// statistic, the frozen workload table, and disjoint grid placement for a non-square draw count --
/// and returns true iff every check passed. Touches no GPU device; `--selftest` on the command line
/// is the only caller. Every failing check's description is appended to `failures`.
bool runSelfTests(std::vector<std::string>& failures);

} // namespace lmx::bench
