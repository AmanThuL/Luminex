//----------------------------------------------------------------------------------------------------------------------
/// @file Runner.h
/// @brief Declares the timed-region runner for one FrameDataBench workload.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Workload.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lmx::bench {

/// Run parameters a caller controls from the command line; defaults match spec section 11's frozen
/// protocol (16 warm-up + 256 measured frames).
struct RunConfig {
    uint32_t warmupFrames = 16;   ///< Frames run and discarded before timing starts.
    uint32_t measuredFrames = 256; ///< Frames whose timed region is recorded.
    bool verify = false;          ///< Reads back the final measured frame and digests it.
};

/// One workload run's outcome: either a populated result or `error` naming what failed.
struct RunResult {
    bool ok = false;    ///< False when setup or a GPU call failed; `error` explains why.
    std::string error;  ///< Failure detail; empty when `ok`.
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
};

/// Runs `spec` for `config.warmupFrames + config.measuredFrames` frames on a freshly created RHI
/// device, timing each measured frame's `device->beginFrame()`..`device->endFrame()` span. Dynamic
/// workloads deliver a changing per-draw block through `deliverPerDrawData` every draw of every
/// frame; static workloads bind buffers created once before the loop and write no per-frame data.
/// With `config.verify` set, reads back the final measured frame's render target and digests it
/// after the loop completes -- readback is never part of a timed frame.
RunResult runWorkload(const WorkloadSpec& spec, const RunConfig& config);

} // namespace lmx::bench
