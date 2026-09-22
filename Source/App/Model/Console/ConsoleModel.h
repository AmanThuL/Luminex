//----------------------------------------------------------------------------------------------------------------------
/// @file ConsoleModel.h
/// @brief Declares filtered and frozen presentation of the editor log store.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Capture/ActionResult.h"
#include "App/Model/Console/ConsoleLog.h"

#include <memory>

namespace lmx::app {

/// Display-only log filters; changing these never discards retained messages.
struct ConsoleFilter {
    log::Level minimumSeverity = log::Level::Trace; ///< Inclusive minimum importance.
    std::string search; ///< Case-insensitive ASCII substring matched against message payloads.
};

/// True when an entry satisfies both severity and case-insensitive message search.
bool consoleEntryMatches(const ConsoleEntry& entry, const ConsoleFilter& filter);
/// Plain text for exactly the matching entries of this snapshot, with UTC timestamps, severity
/// and explicit truncation markers; preserves multiline payloads and chronological order.
std::string consoleVisibleText(const ConsoleSnapshot& snapshot, const ConsoleFilter& filter);

/// Main-thread display state; ingestion continues independently through the shared ConsoleLog.
/// Freeze retains the displayed snapshot and its counters. Clear empties both store and displayed
/// view while retaining frozen state and filters; Resume immediately sees current retained logs.
class ConsoleModel {
public:
    /// Retains shared ownership of a non-null thread-safe store.
    explicit ConsoleModel(std::shared_ptr<ConsoleLog> log);
    /// Refreshes only when live and the store revision changed; call before drawing controls.
    void refresh();
    /// Freezes the already displayed snapshot, or resumes from current retained storage.
    void setFrozen(bool frozen);
    /// Whether ingestion is hidden behind the displayed frozen snapshot.
    bool frozen() const { return m_frozen; }
    /// Empties retained and displayed messages/counters, preserving freeze and filter settings.
    void clear();
    /// The owned displayed snapshot, stable until the next refresh, Clear or Resume.
    const ConsoleSnapshot& snapshot() const { return m_snapshot; }

    ConsoleFilter filter; ///< Current display filters, owned by the UI thread.
    /// Last clipboard action result, independent of log ingestion and display freeze.
    ActionResult clipboardResult;
    bool autoScroll = true; ///< Follow arrivals only when the view was already at its end.

private:
    std::shared_ptr<ConsoleLog> m_log;
    ConsoleSnapshot m_snapshot;
    bool m_frozen = false;
};

} // namespace lmx::app
