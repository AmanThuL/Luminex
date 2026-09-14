//----------------------------------------------------------------------------------------------------------------------
/// @file ConsoleLog.h
/// @brief Declares thread-safe bounded storage for the read-only editor log viewer.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::app {

/// Increasing log importance, independent of any logging library or UI.
enum class ConsoleSeverity : uint8_t {
    Trace,    ///< Detailed trace.
    Debug,    ///< Debugging detail.
    Info,     ///< Informational event.
    Warning,  ///< Recoverable warning.
    Error,    ///< Failed operation.
    Critical, ///< Critical failure.
};

/// One retained log message; text is owned and never exceeds the per-message payload limit.
struct ConsoleEntry {
    uint64_t sequence = 0;             ///< Monotonic ingestion identity, retained across Clear.
    int64_t timestampMilliseconds = 0; ///< UTC milliseconds since the Unix epoch.
    ConsoleSeverity severity = ConsoleSeverity::Info; ///< Message importance.
    std::string message;    ///< UTF-8 log payload, truncated at a code-point boundary when needed.
    bool truncated = false; ///< Original payload exceeded the per-message limit.
};

/// Coherent copy of retained messages and their loss counters at one store revision.
struct ConsoleSnapshot {
    uint64_t revision = 0; ///< Advances whenever a message arrives or storage is cleared.
    std::vector<ConsoleEntry>
        entries;                 ///< Oldest first; owned independently of later log ingestion.
    size_t payloadBytes = 0;     ///< Sum of retained message byte counts, excluding metadata.
    uint64_t evictedEntries = 0; ///< Entries lost to count or byte bounds since Clear.
    /// Incoming oversized messages since Clear, including evicted ones.
    uint64_t truncatedMessages = 0;
};

/// Bounded log storage with serialized ingestion/snapshot/clear operations. Producers may run on
/// worker threads; none of these operations invokes UI or logging APIs. Snapshot copies outlive
/// this store. Owners must stop producers before destroying their last shared store reference.
class ConsoleLog {
public:
    static constexpr size_t kMaxEntries = 2000;                 ///< Maximum retained entries.
    static constexpr size_t kMaxPayloadBytes = 2 * 1024 * 1024; ///< Maximum retained message bytes.
    /// Per-message byte cap before retention.
    static constexpr size_t kMaxMessageBytes = 16 * 1024;

    /// Copies one message, truncates it if needed, then evicts oldest entries until both limits
    /// hold.
    void append(ConsoleSeverity severity, int64_t timestampMilliseconds, std::string_view message);
    /// Returns an owned coherent snapshot; may be called from any thread.
    ConsoleSnapshot snapshot() const;
    /// Avoids copying payloads when the caller already has the current revision.
    std::optional<ConsoleSnapshot> snapshotAfter(uint64_t revision) const;
    /// Atomically empties entries and loss counters, returning the exact empty revision. Messages
    /// arriving afterwards belong to the new history; sequence identities are never reused.
    ConsoleSnapshot clear();

private:
    ConsoleSnapshot snapshotLocked() const;

    mutable std::mutex m_mutex;
    std::deque<ConsoleEntry> m_entries;
    uint64_t m_revision = 0;
    uint64_t m_nextSequence = 1;
    size_t m_payloadBytes = 0;
    uint64_t m_evictedEntries = 0;
    uint64_t m_truncatedMessages = 0;
};

/// Stable readable severity text for display and clipboard output.
std::string_view consoleSeverityName(ConsoleSeverity severity);
/// Formats UTC time of day with millisecond precision; timestamps are Unix epoch milliseconds.
std::string consoleTimestamp(int64_t milliseconds);

} // namespace lmx::app
