//----------------------------------------------------------------------------------------------------------------------
/// @file SessionPanel.h
/// @brief Declares the docked Session review panel and its operator-only actions.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Session/SessionLog.h"
#include "App/Model/Session/SessionProposal.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace lmx::app {

inline constexpr const char* kSessionWindowName = "Session"; ///< Dockable Session window name.

/// Main-thread data needed to draw proposals and the action history.
struct SessionPanelContext {
    ProposalQueue& proposals;                ///< Retained proposals, read only during drawing.
    const SessionLog& log;                   ///< Retained action history.
    uint64_t& expandedId;                    ///< One expanded proposal, retained by the shell.
    std::filesystem::path evidenceDirectory; ///< Base for relative sidecar evidence paths.
    std::string& pathFeedback;               ///< Last Copy or Reveal result.
};

/// Operator action selected in this frame; future bridge controls use the reserved values.
enum class SessionPanelAction {
    None,         ///< No action was selected.
    Accept,       ///< Accept the identified proposal.
    Reject,       ///< Reject the identified proposal.
    Approve,      ///< Approve a pending command or plan.
    Deny,         ///< Deny a pending command or plan.
    ToggleListen, ///< Toggle the socket listener.
    SetTier,      ///< Change the current connection's ceiling.
    Export        ///< Export the session record.
};

/// Click result only; drawing never resolves proposals or raises permission tiers.
struct SessionPanelResult {
    SessionPanelAction action = SessionPanelAction::None; ///< Selected operator action.
    uint64_t id = 0;                                      ///< Proposal or approval identity.
    SessionTier tier = SessionTier::ReadOnly;             ///< Selected ceiling for SetTier.
};

/// Draws the docked panel and reports clicks without mutating editor or session state.
SessionPanelResult drawSessionPanel(bool& open, SessionPanelContext& context);

} // namespace lmx::app
