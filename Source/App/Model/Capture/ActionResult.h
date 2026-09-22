//----------------------------------------------------------------------------------------------------------------------
/// @file ActionResult.h
/// @brief Owns the last diagnostic operation result independently of its UI.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <string>

namespace lmx::app {

/// Lifecycle of one bounded diagnostic operation.
enum class ActionStatus {
    Unavailable, ///< Cannot run in the current process; message explains
                 ///< recovery.
    Ready,       ///< Available, with no outstanding request.
    Pending,     ///< Requested or running; repeated requests coalesce.
    Succeeded,   ///< Completed output is available at path.
    Failed       ///< Attempt failed; message retains the causal reason.
};

/// User-facing lifecycle label with static lifetime, shared by notifications and result controls.
constexpr const char* actionStatusName(ActionStatus status) {
    switch (status) {
    case ActionStatus::Unavailable:
        return "Unavailable";
    case ActionStatus::Ready:
        return "Ready";
    case ActionStatus::Pending:
        return "Pending";
    case ActionStatus::Succeeded:
        return "Succeeded";
    case ActionStatus::Failed:
        return "Failed";
    }
    return "Unavailable";
}

/// Owned result retained until the next operation replaces it.
struct ActionResult {
    ActionStatus status = ActionStatus::Ready; ///< Current operation state.
    std::string message;                       ///< Readable result or availability explanation.
    std::string path;              ///< Absolute output path when known, including failed attempts.
    std::string pathActionError{}; ///< Last clipboard/Finder failure; does not
                                   ///< change the operation result.
};

} // namespace lmx::app
