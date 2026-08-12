//----------------------------------------------------------------------------------------------------------------------
/// @file PerformanceModel.cpp
/// @brief Implements the coherent performance snapshot behind the Performance panel.
//----------------------------------------------------------------------------------------------------------------------

#include "App/PerformanceModel.h"

#include <numeric>
#include <utility>

namespace lmx::app {

//======================================================================================================================
void PerformanceModel::tick(float deltaSeconds, const PerformanceFrameSample* sample) {
    // Coherent pause (spec section 10): collection stops entirely while paused, so resuming has
    // nothing collected-but-unpublished to reconcile against the frozen snapshot.
    if (m_paused) {
        return;
    }

    if (m_frameIntervalMsHistory.size() == kFrameIntervalCapacity) {
        m_frameIntervalMsHistory.erase(m_frameIntervalMsHistory.begin());
    }
    m_frameIntervalMsHistory.push_back(deltaSeconds * 1000.0f);

    bool scheduleChanged = false;
    bool sampleAccepted = false;
    // A zero, repeated, or regressing frame id is ignored in full: neither its pass timings nor
    // its counts, sizes, or transient bytes take effect, matching PassTimingHistory's own rule.
    if (sample != nullptr && sample->frameId != 0 && sample->frameId > m_lastAcceptedFrameId) {
        scheduleChanged = m_passTimingHistory.addFrame(sample->frameId, sample->timings);
        m_lastAcceptedFrameId = sample->frameId;
        m_objectCount = sample->objectCount;
        m_drawCount = sample->drawCount;
        m_viewportLogicalWidth = sample->viewportLogicalWidth;
        m_viewportLogicalHeight = sample->viewportLogicalHeight;
        m_sceneTargetPixelWidth = sample->sceneTargetPixelWidth;
        m_sceneTargetPixelHeight = sample->sceneTargetPixelHeight;
        m_transientRequestedBytes = sample->transientRequestedBytes;
        m_transientHighWaterBytes = sample->transientHighWaterBytes;
        m_transientAliasSavingsBytes = sample->transientAliasSavingsBytes;
        sampleAccepted = true;
    }

    // Every field of the published snapshot advances together on the same 0.25 s cadence -- the
    // rolling GPU table's existing refresh rate -- rather than each independently timed, which is
    // what keeps the snapshot coherent by construction. A schedule change or the first accepted
    // sample after a waiting state publishes immediately, exactly as the GPU table already did.
    m_republishAccumulator += deltaSeconds;
    const bool endsWaiting = sampleAccepted && m_live.waitingForSamples;
    if (scheduleChanged || endsWaiting || m_republishAccumulator >= kRepublishIntervalSeconds) {
        rebuildLiveSnapshot();
        m_republishAccumulator = 0.0f;
    }
}

//======================================================================================================================
void PerformanceModel::setPaused(bool paused) {
    if (paused == m_paused) {
        return;
    }
    m_paused = paused;
    if (paused) {
        m_frozen = m_live;
        return;
    }
    // Resume publishes one internally consistent current snapshot immediately rather than waiting
    // for the next 0.25 s tick, so the panel never shows a stale frozen row beside a live
    // resolution or memory value.
    rebuildLiveSnapshot();
    m_republishAccumulator = 0.0f;
}

//======================================================================================================================
void PerformanceModel::clearHistory() {
    m_frameIntervalMsHistory.clear();
    m_passTimingHistory = PassTimingHistory{};
    m_lastAcceptedFrameId = 0;
    m_objectCount = 0;
    m_drawCount = 0;
    m_viewportLogicalWidth = 0;
    m_viewportLogicalHeight = 0;
    m_sceneTargetPixelWidth = 0;
    m_sceneTargetPixelHeight = 0;
    m_transientRequestedBytes = 0;
    m_transientHighWaterBytes = 0;
    m_transientAliasSavingsBytes = 0;
    m_republishAccumulator = 0.0f;

    rebuildLiveSnapshot();
    // Clear History is visible immediately regardless of pause state: it never leaves a stale
    // frozen snapshot behind a display that claims to already be empty.
    if (m_paused) {
        m_frozen = m_live;
    }
}

//======================================================================================================================
void PerformanceModel::rebuildLiveSnapshot() {
    PerformanceSnapshot snapshot;
    snapshot.frameIntervalsMs = m_frameIntervalMsHistory;
    snapshot.latestFrameIntervalMs =
        m_frameIntervalMsHistory.empty() ? 0.0f : m_frameIntervalMsHistory.back();
    // Smoothed over the whole rolling history rather than the instantaneous latest interval, so a
    // single slow or fast tick does not swing the displayed FPS as far as it would swing
    // 1000 / latestFrameIntervalMs at the 4 Hz republish cadence.
    if (!m_frameIntervalMsHistory.empty()) {
        const float sumMs =
            std::accumulate(m_frameIntervalMsHistory.begin(), m_frameIntervalMsHistory.end(), 0.0f);
        const float meanIntervalMs = sumMs / static_cast<float>(m_frameIntervalMsHistory.size());
        snapshot.framesPerSecond = meanIntervalMs > 0.0f ? 1000.0f / meanIntervalMs : 0.0f;
    } else {
        snapshot.framesPerSecond = 0.0f;
    }

    snapshot.passRows = m_passTimingHistory.summaries();
    for (const PassTimingSummary& row : snapshot.passRows) {
        snapshot.timedPassSumMilliseconds += row.averageGpuMilliseconds;
    }

    snapshot.frameId = m_lastAcceptedFrameId;
    snapshot.objectCount = m_objectCount;
    snapshot.drawCount = m_drawCount;
    snapshot.viewportLogicalWidth = m_viewportLogicalWidth;
    snapshot.viewportLogicalHeight = m_viewportLogicalHeight;
    snapshot.sceneTargetPixelWidth = m_sceneTargetPixelWidth;
    snapshot.sceneTargetPixelHeight = m_sceneTargetPixelHeight;
    snapshot.transientRequestedBytes = m_transientRequestedBytes;
    snapshot.transientHighWaterBytes = m_transientHighWaterBytes;
    snapshot.transientAliasSavingsBytes = m_transientAliasSavingsBytes;
    snapshot.waitingForSamples = snapshot.passRows.empty();

    m_live = std::move(snapshot);
}

} // namespace lmx::app
