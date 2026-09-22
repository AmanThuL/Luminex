//----------------------------------------------------------------------------------------------------------------------
/// @file ConsoleLog.cpp
/// @brief Implements bounded log ingestion and coherent copies without a UI dependency.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Console/ConsoleLog.h"

#include <algorithm>
#include <format>
#include <utility>

namespace lmx::app {

//======================================================================================================================
void ConsoleLog::append(log::Level severity, int64_t timestampMilliseconds,
                        std::string_view message) {
    size_t count = std::min(message.size(), kMaxMessageBytes);
    const bool truncated = count < message.size();
    if (truncated) {
        while (count > 0 && (static_cast<unsigned char>(message[count]) & 0xC0) == 0x80) {
            --count;
        }
    }
    std::scoped_lock lock(m_mutex);
    ConsoleEntry entry{m_nextSequence++, timestampMilliseconds, severity,
                       std::string(message.substr(0, count)), truncated};
    if (m_entries.size() == kMaxEntries) {
        m_payloadBytes -= m_entries[0].message.size();
        m_entries.popOldest();
        ++m_evictedEntries;
    }
    m_entries.push(std::move(entry));
    m_payloadBytes += count;
    m_truncatedMessages += truncated ? 1 : 0;
    while (m_payloadBytes > kMaxPayloadBytes) {
        m_payloadBytes -= m_entries[0].message.size();
        m_entries.popOldest();
        ++m_evictedEntries;
    }
    ++m_revision;
}

//======================================================================================================================
ConsoleSnapshot ConsoleLog::snapshotLocked() const {
    return {m_revision,
            {m_entries.begin(), m_entries.end()},
            m_payloadBytes,
            m_evictedEntries,
            m_truncatedMessages};
}

//======================================================================================================================
ConsoleSnapshot ConsoleLog::snapshot() const {
    std::scoped_lock lock(m_mutex);
    return snapshotLocked();
}

//======================================================================================================================
std::optional<ConsoleSnapshot> ConsoleLog::snapshotAfter(uint64_t revision) const {
    std::scoped_lock lock(m_mutex);
    return revision == m_revision ? std::nullopt : std::optional(snapshotLocked());
}

//======================================================================================================================
ConsoleSnapshot ConsoleLog::clear() {
    std::scoped_lock lock(m_mutex);
    m_entries.clear();
    m_payloadBytes = 0;
    m_evictedEntries = 0;
    m_truncatedMessages = 0;
    ++m_revision;
    return snapshotLocked();
}

//======================================================================================================================
std::string_view consoleSeverityName(log::Level severity) {
    switch (severity) {
    case log::Level::Trace:
        return "Trace";
    case log::Level::Debug:
        return "Debug";
    case log::Level::Info:
        return "Info";
    case log::Level::Warning:
        return "Warning";
    case log::Level::Error:
        return "Error";
    case log::Level::Critical:
        return "Critical";
    }
    return "Info";
}

//======================================================================================================================
std::string consoleTimestamp(int64_t milliseconds) {
    constexpr int64_t kDay = 24 * 60 * 60 * 1000;
    const int64_t time = (milliseconds % kDay + kDay) % kDay;
    return std::format("{:02}:{:02}:{:02}.{:03}", time / 3600000, (time / 60000) % 60,
                       (time / 1000) % 60, time % 1000);
}

} // namespace lmx::app
