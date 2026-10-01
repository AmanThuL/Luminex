//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentWatch.h
/// @brief Declares stable document-pair polling and sidecar grace decisions.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lmx::app {

/// Filesystem observation of a loaded glTF, its buffer, and writer temporary directories.
struct FileStamp {
    uintmax_t gltfSize = 0;   ///< glTF byte count, zero when absent.
    uintmax_t bufferSize = 0; ///< Companion byte count, zero when absent.
    int64_t gltfTime = 0;     ///< Filesystem modification clock ticks.
    int64_t bufferTime = 0;   ///< Filesystem modification clock ticks.
    bool saving = false;      ///< A writer's .lmx-save-*.tmp directory exists.
    bool operator==(const FileStamp&) const = default; ///< Exact observation equality.
};

/// Next main-thread operation permitted by a stable poll.
enum class WatchDecision {
    Wait,    ///< Do nothing this poll.
    Hash,    ///< Hash the stable pair once.
    Sidecar, ///< Retry sidecar attribution within its four-second grace period.
};

/// Pure half-second polling model. Caller owns file I/O and uses one monotonic seconds clock.
class DocumentWatch {
public:
    /// Adopts the current pair observation after load or an editor save.
    void reset(const FileStamp& loaded);
    /// Whether a filesystem observation is due on this clock; does not update the poll cursor.
    bool due(double seconds) const;
    /// Returns Hash after two equal non-saving changed observations, never more often than 0.5 s.
    WatchDecision poll(const FileStamp& now, double seconds);
    /// Reports a pair hash and whether a sidecar names it; a mismatch waits up to four seconds.
    void hashed(std::string_view hash, bool sidecarMatches, double seconds);
    /// True once a matching sidecar arrives or the four-second grace expires.
    bool ready() const { return m_ready; }

private:
    FileStamp m_loaded;
    FileStamp m_observed;
    double m_lastPoll = -1.0;
    double m_graceStart = -1.0;
    std::string m_hash;
    bool m_observedOnce = false;
    bool m_hashed = false;
    bool m_ready = false;
};

} // namespace lmx::app
