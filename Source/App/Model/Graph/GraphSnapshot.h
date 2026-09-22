//----------------------------------------------------------------------------------------------------------------------
/// @file GraphSnapshot.h
/// @brief Owns the Render Graph panel's published and frozen frame snapshot.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/DiagnosticRefresh.h"
#include "App/Model/Graph/FrameRecordRing.h"

#include <optional>

namespace lmx::app {

/// Publishes complete value-owned frames at four updates per second, independently of scene
/// playback and Performance sampling. Timings remain exact-frame latest values, never averages.
class GraphSnapshot {
public:
    /// Observes the newest retained frame. First data and the first data after resume publish
    /// immediately; every later publication waits for the shared interval, including topology
    /// changes. `nowSeconds` is a nonnegative monotonic clock. The frame pointer is borrowed only
    /// during this call; record, metadata and joined timings are copied together when published.
    /// Null leaves the current publication intact, or keeps an empty model waiting. Frozen updates
    /// are ignored. Unjoined timings are discarded rather than presented as matched data.
    void update(double nowSeconds, const RetainedFrame* newest);
    /// Freezes the currently displayed publication; does nothing while no frame is displayed.
    void freeze();
    /// Returns to Live and waits for update() to publish the newest available frame immediately.
    /// Clears the frozen publication so unavailable live data is shown as waiting.
    void resume();
    /// Whether the displayed publication is frozen.
    bool frozen() const { return m_frozen; }
    /// The owned displayed frame, or null while waiting; valid until
    /// publication/resume/destruction.
    const RetainedFrame* displayed() const;

private:
    std::optional<RetainedFrame> m_frame;
    double m_nextPublishSeconds = 0.0;
    bool m_frozen = false;
};

} // namespace lmx::app
