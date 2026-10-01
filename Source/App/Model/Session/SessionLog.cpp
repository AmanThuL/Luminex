//----------------------------------------------------------------------------------------------------------------------
/// @file SessionLog.cpp
/// @brief Records attributed session actions and their evidence.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionLog.h"

#include "Core/Diagnostics/Assert.h"

#include <algorithm>
#include <format>
#include <utility>

namespace lmx::app {

//======================================================================================================================
std::string_view sessionStateLabel(SessionState state) {
    switch (state) {
    case SessionState::Idle:
        return "idle";
    case SessionState::Working:
        return "working";
    case SessionState::Awaiting:
        return "awaiting";
    case SessionState::Proposed:
        return "proposed";
    case SessionState::Applied:
        return "applied";
    case SessionState::Error:
        return "error";
    case SessionState::Stale:
        return "stale";
    }
    LMX_ASSERT(false, "Unknown session state");
    return "error";
}

//======================================================================================================================
SessionLog::SessionLog(std::shared_ptr<ConsoleLog> console) : m_console(std::move(console)) {
    LMX_ASSERT(m_console != nullptr, "SessionLog requires a Console store");
}

//======================================================================================================================
uint64_t SessionLog::record(SessionAction action) {
    action.sequence = m_nextSequence++;
    const uint64_t sequence = action.sequence;
    m_console->append(log::Level::Info, action.timestampMilliseconds,
                      std::format("{} {} -> {}", action.command, action.arguments, action.outcome),
                      action.actor);
    m_actions.push_back(std::move(action));
    return sequence;
}

//======================================================================================================================
void SessionLog::attach(uint64_t sequence, SessionEvidence evidence) {
    const auto it =
        std::find_if(m_actions.begin(), m_actions.end(), [sequence](const SessionAction& action) {
            return action.sequence == sequence;
        });
    LMX_ASSERT(it != m_actions.end(), "Unknown session action sequence");
    it->evidence.push_back(std::move(evidence));
}

//======================================================================================================================
std::span<const SessionAction> SessionLog::actions() const {
    return m_actions;
}

} // namespace lmx::app
