//----------------------------------------------------------------------------------------------------------------------
/// @file Host.h
/// @brief Declares the isolated native Metal submission executor.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Model/Workload.h"

#include <filesystem>
#include <optional>

namespace lmx::experimental::submission {

/// Native replay configuration; paths are explicit and never resolved against a hidden CWD.
struct RunConfig {
    uint32_t warmup = 32;  ///< Unscored submissions, fully drained before samples.
    uint32_t frames = 256; ///< Positive measured/replayed frame count.
    bool verify = false;   ///< Untimed argument, canary and image checks on retirement.
    /// Directory holding pinned Slang Scene/Prepare artifacts.
    std::filesystem::path shaderDirectory;
    std::filesystem::path capturePath; ///< New absolute .gputrace; runNative captures one frame.
    /// Unscored native feedback observer; requires verify=false and an empty capturePath.
    /// Bounded by warmup/frames; logs to stderr and joins callbacks at normal slot retirement.
    /// Adds no readbacks, canary checks/writes, GPU submissions or per-frame idle waits.
    bool diagnostics = false;
};

/// One submitted frame, joined to exact slot retirement before its run returns.
struct FrameSample {
    uint32_t frame = 0;     ///< Zero-based logical frame in the measured replay.
    uint64_t cpuWorkNs = 0; ///< After slot wait through commit and retirement signal, nanoseconds.
    uint64_t waitNs = 0;    ///< Exact slot completion wait, nanoseconds, outside cpuWorkNs.
    uint64_t drawCalls = 0; ///< Native direct, indirect or instanced draw API calls.
    /// Per-frame payload writes, including CPU argument capacity clearing.
    uint64_t copiedBytes = 0;
    std::optional<double> gpuSpanMs;     ///< Null: workload timestamp boundaries are unverified.
    std::optional<double> preparationMs; ///< Null: stage timestamp boundaries are unverified.
    std::optional<double> rasterMs;      ///< Null: stage timestamp boundaries are unverified.
};

/// Fully drained run; byte counts are explained separately in capabilitiesJson's memory object.
/// With diagnostics=true all timings are unscored observer data and verified remains false.
struct RunResult {
    std::vector<FrameSample> samples; ///< Ordered, complete frame IDs; excludes warmup.
    double throughput = 0; ///< Completed frames/second, first commit to final retirement.
    double setupMs = 0;    ///< Native object allocation and shader/pipeline loading before warmup.
    double drainMs = 0;    ///< Final measured retirement wait; included in throughput's window.
    uint64_t requestedBytes = 0; ///< All slots plus shared and CPU temporary/requested storage.
    /// Queried GPU resources/allocator high water; opaque objects excluded.
    uint64_t allocatedBytes = 0;
    std::string device;        ///< Physical Metal device name reported by the native device.
    bool verified = false;     ///< Every requested frame passed retired ID, guard and image checks.
    std::string gpuSpanStatus; ///< Explicit unavailable reason, never a synthetic zero measurement.
};

/// Runs one isolated variant, reserves before warmup and destroys all native objects before return.
/// Headline only; counter lanes and ICB return an unavailable diagnostic. Scoring rejects
/// validation. Capture requires verify=true and writes one trace plus an experiment-specific
/// metadata sidecar.
/// Diagnostics permits validation environment flags and retains three-slot pipelining, with
/// immutable submission feedback and flushed stderr setup/start/submission/error logs. Callback
/// delivery joins the exact retirement event; all diagnostic timings are unscored. Diagnostics
/// combined with verify or a capture path fails before device creation.
Result<RunResult> runNative(const Case&, Suite, Variant, Lane, const RunConfig&);

/// Replays exact scored artifacts with debug=0 and returns retired pixels and observed visible IDs.
/// Does not start capture, even when capturePath is set; capture belongs to runNative exclusively.
/// Rejects diagnostics: this entry point always performs verification and readback.
Result<FrameImage> renderNativeFrame(const Case&, Suite, Variant, uint32_t logicalFrame,
                                     const RunConfig&);

/// Returns schema-versioned implementation capabilities and bounded, unverified ICB/timing status.
/// Does not create a device; modes list implementation coverage, not a runtime capability probe.
std::string capabilitiesJson();
} // namespace lmx::experimental::submission
