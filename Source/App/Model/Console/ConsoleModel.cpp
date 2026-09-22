//----------------------------------------------------------------------------------------------------------------------
/// @file ConsoleModel.cpp
/// @brief Implements log filtering, clipboard text, freeze and clear semantics.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Console/ConsoleModel.h"

#include "Core/Diagnostics/Assert.h"

#include <algorithm>
#include <format>
#include <utility>

namespace lmx::app {
namespace {

//======================================================================================================================
unsigned char foldAscii(unsigned char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}

} // namespace

//======================================================================================================================
bool consoleEntryMatches(const ConsoleEntry& entry, const ConsoleFilter& filter) {
    if (entry.severity < filter.minimumSeverity)
        return false;
    return std::search(entry.message.begin(), entry.message.end(), filter.search.begin(),
                       filter.search.end(),
                       [](unsigned char a, unsigned char b) {
                           return foldAscii(a) == foldAscii(b);
                       }) != entry.message.end() ||
           filter.search.empty();
}

//======================================================================================================================
std::string consoleVisibleText(const ConsoleSnapshot& snapshot, const ConsoleFilter& filter) {
    std::string result;
    for (const auto& entry : snapshot.entries) {
        if (consoleEntryMatches(entry, filter)) {
            result +=
                std::format("[{} UTC] [{}] {}{}\n", consoleTimestamp(entry.timestampMilliseconds),
                            consoleSeverityName(entry.severity), entry.message,
                            entry.truncated ? " [truncated]" : "");
        }
    }
    return result;
}

//======================================================================================================================
ConsoleModel::ConsoleModel(std::shared_ptr<ConsoleLog> log) : m_log(std::move(log)) {
    LMX_ASSERT(m_log != nullptr, "ConsoleModel requires a log store");
    refresh();
}

//======================================================================================================================
void ConsoleModel::refresh() {
    if (!m_frozen) {
        if (auto snapshot = m_log->snapshotAfter(m_snapshot.revision)) {
            m_snapshot = std::move(*snapshot);
        }
    }
}

//======================================================================================================================
void ConsoleModel::setFrozen(bool frozen) {
    m_frozen = frozen;
    refresh();
}

//======================================================================================================================
void ConsoleModel::clear() {
    m_snapshot = m_log->clear();
}

} // namespace lmx::app
