//----------------------------------------------------------------------------------------------------------------------
/// @file PerformanceModel.h
/// @brief Declares the ImGui-free coherent performance snapshot behind the Performance panel.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/PassTimingHistory.h"
#include "RHI/RHI.h"

#include <cstdint>
#include <span>
#include <vector>

namespace lmx::app {

/// Everything known about one newly retired GPU frame, fed to `PerformanceModel::tick` together so
/// it always joins one coherent snapshot rather than drifting in independently.
///
/// `timings` is schedule-ordered, exactly as the RHI publishes it; the model composes
/// `PassTimingHistory` to turn it into rolling rows. The remaining fields are plain values the
/// shell already has to hand -- this model reads no `FrameRecordRing` or `CompiledFrameRecord`
/// itself, so it stays ImGui/SDL/Metal/RHI-backend free.
struct PerformanceFrameSample {
    uint64_t frameId = 0;                     ///< The device frame number this sample measured.
    std::span<const rhi::PassTiming> timings; ///< Per-pass GPU times, schedule order.
    uint32_t objectCount = 0;                 ///< Scene object count at this frame.
    uint32_t drawCount = 0;                   ///< Draw-call count at this frame.
    uint32_t viewportLogicalWidth = 0;        ///< Viewport panel size, in ImGui logical points.
    uint32_t viewportLogicalHeight = 0;       ///< Viewport panel size, in ImGui logical points.
    uint32_t sceneTargetPixelWidth = 0;       ///< Scene render target size, in device pixels.
    uint32_t sceneTargetPixelHeight = 0;      ///< Scene render target size, in device pixels.
    uint64_t transientRequestedBytes = 0;     ///< `render::TransientMemory::requested`.
    uint64_t transientHighWaterBytes = 0;     ///< `render::TransientMemory::highWater`.
    uint64_t transientAliasSavingsBytes = 0;  ///< `render::TransientMemory::aliasSavings`.
};

/// One coherent, display-ready reading of the editor's rolling performance state.
///
/// Every field here was published together: while running they all advance on the same republish
/// tick, and while paused they all stay exactly as they were at the moment pause was requested.
/// Nothing here mixes a frozen pass row with a live resolution or memory value.
struct PerformanceSnapshot {
    /// Rolling wall-clock frame-interval history, oldest first, in milliseconds. Capacity
    /// `PerformanceModel::kFrameIntervalCapacity`. Comes from the App frame loop's `deltaSeconds`
    /// -- never CPU render time.
    std::vector<float> frameIntervalsMs;
    float latestFrameIntervalMs = 0.0f; ///< Newest wall-clock frame interval, in milliseconds.
    /// `1000 / latestFrameIntervalMs`, or 0 while no interval has been published yet.
    float framesPerSecond = 0.0f;

    /// Rolling GPU pass rows -- Average/Latest/Min-Max/Samples -- in schedule order, over the
    /// trailing `PassTimingHistory::kSampleCapacity` retired frames.
    std::vector<PassTimingSummary> passRows;
    /// Sum of `passRows`' displayed averages. Explicitly NOT total GPU frame time: present, driver,
    /// and untimestamped work fall outside it.
    double timedPassSumMilliseconds = 0.0;

    uint64_t frameId = 0;     ///< The newest displayed rolling frame's ID; 0 before any sample.
    uint32_t objectCount = 0; ///< Scene object count from the frame the pass rows accompanied.
    uint32_t drawCount = 0;   ///< Draw-call count from the frame the pass rows accompanied.
    uint32_t viewportLogicalWidth = 0;       ///< Viewport panel size, in ImGui logical points.
    uint32_t viewportLogicalHeight = 0;      ///< Viewport panel size, in ImGui logical points.
    uint32_t sceneTargetPixelWidth = 0;      ///< Scene render target size, in device pixels.
    uint32_t sceneTargetPixelHeight = 0;     ///< Scene render target size, in device pixels.
    uint64_t transientRequestedBytes = 0;    ///< Bytes requested by the retained compiled frame.
    uint64_t transientHighWaterBytes = 0;    ///< Bytes its transient heap had to provide.
    uint64_t transientAliasSavingsBytes = 0; ///< Bytes aliasing saved against `requested`.

    /// True until the first retired GPU sample lands, and again after `clearHistory()` until the
    /// next one does. The frame-interval history keeps collecting from wall-clock deltas regardless
    /// -- this flag names only the GPU-row/frame-context side of the snapshot as not-yet-useful.
    bool waitingForSamples = true;
};

/// Composes `PassTimingHistory` into the coherent, pausable, clearable performance snapshot the
/// Performance panel presents. Contains no ImGui, SDL, Metal, or RHI-backend type.
///
/// Publication is throttled to four times a second so sub-millisecond digits stay readable, exactly
/// as the rolling GPU table already did; every field in the published `PerformanceSnapshot`
/// advances together on that same cadence, which is what keeps the snapshot internally consistent
/// without the panel having to reconcile independently-timed values itself.
class PerformanceModel {
public:
    /// Wall-clock frame intervals retained for the rolling plot before the oldest rolls out.
    static constexpr size_t kFrameIntervalCapacity = 120;
    /// Seconds between republished snapshots while running.
    static constexpr float kRepublishIntervalSeconds = 0.25f;

    /// Feeds one App frame. `deltaSeconds` is the frame loop's wall-clock delta and is always
    /// consumed; `sample` is non-null only on a frame that has a newly retired GPU frame to report,
    /// exactly as `FrameRecordRing::newestTimedFrame()` may be null. A sample whose `frameId` is
    /// zero, repeats, or regresses relative to the last accepted one is ignored in full -- neither
    /// its pass timings nor its counts, sizes, or transient bytes take effect -- matching
    /// `PassTimingHistory`'s own rule so the two never disagree about which frame is current.
    ///
    /// While paused this is a no-op: collection stops, so resuming has nothing stale to reconcile.
    void tick(float deltaSeconds, const PerformanceFrameSample* sample);

    /// The current display snapshot: the frozen one while paused, the latest published one while
    /// running.
    const PerformanceSnapshot& snapshot() const { return m_paused ? m_frozen : m_live; }

    /// Whether the model is currently paused.
    bool paused() const { return m_paused; }

    /// Pausing freezes the currently published snapshot; every subsequent `tick()` is ignored until
    /// resumed. Resuming immediately republishes one internally consistent current snapshot rather
    /// than waiting for the next 0.25 s tick, so the panel never has to show a stale frozen row
    /// beside a live resolution or memory value.
    void setPaused(bool paused);

    /// Empties the frame-interval and pass-timing history together and republishes immediately, so
    /// the snapshot reports a waiting state until the next accepted sample arrives. Takes effect on
    /// both the live and (if paused) the frozen snapshot, so Clear History is visible right away
    /// regardless of pause state.
    void clearHistory();

private:
    void rebuildLiveSnapshot();

    bool m_paused = false;
    float m_republishAccumulator = 0.0f;
    uint64_t m_lastAcceptedFrameId = 0;

    std::vector<float> m_frameIntervalMsHistory;
    PassTimingHistory m_passTimingHistory;

    // The most recently accepted sample's context fields, held so they keep publishing alongside
    // the pass rows they accompanied between one accepted sample and the next.
    uint32_t m_objectCount = 0;
    uint32_t m_drawCount = 0;
    uint32_t m_viewportLogicalWidth = 0;
    uint32_t m_viewportLogicalHeight = 0;
    uint32_t m_sceneTargetPixelWidth = 0;
    uint32_t m_sceneTargetPixelHeight = 0;
    uint64_t m_transientRequestedBytes = 0;
    uint64_t m_transientHighWaterBytes = 0;
    uint64_t m_transientAliasSavingsBytes = 0;

    PerformanceSnapshot m_live;
    PerformanceSnapshot m_frozen;
};

} // namespace lmx::app
