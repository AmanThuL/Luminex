//----------------------------------------------------------------------------------------------------------------------
/// @file SessionTypes.h
/// @brief Declares shared session permission tiers and lifecycle states.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string_view>

namespace lmx::app {

/// Maximum permission granted by the operator to a session connection.
enum class SessionTier : uint8_t {
    ReadOnly, ///< Inspect state without requesting a change.
    Propose,  ///< Submit changes for operator review.
    Apply     ///< Request an operator-approved command.
};

/// Lifecycle state shared by proposals, approvals and active work.
enum class SessionState {
    Idle,     ///< No work is pending.
    Working,  ///< Work is currently executing.
    Awaiting, ///< Operator input is required.
    Proposed, ///< A change is ready for review.
    Applied,  ///< A reviewed change completed.
    Error,    ///< Work failed.
    Stale     ///< The underlying subject changed before review.
};

/// Returns the stable lowercase display name of a lifecycle state.
std::string_view sessionStateLabel(SessionState state);

} // namespace lmx::app
