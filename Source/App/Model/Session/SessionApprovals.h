//----------------------------------------------------------------------------------------------------------------------
/// @file SessionApprovals.h
/// @brief Declares operator-reviewed session commands and ordered plans.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "App/Model/Session/SessionCommands.h"
#include "App/Model/Session/SessionProtocol.h"
#include "App/Model/Session/SessionTypes.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lmx::app {

/// One immutable command and its serialized arguments within an approved plan.
struct ApprovalStep {
    SessionCommand command; ///< Apply command identity, excluding PlanSubmit.
    std::string arguments;  ///< Owned argument text shown to the operator.
};

/// An owned approval request retained through its terminal result for response dispatch.
struct PendingApproval {
    uint64_t id;                                 ///< Queue-assigned approval identity.
    uint64_t request;                            ///< Original protocol request identity.
    std::string client;                          ///< Session client name.
    std::string summary;                         ///< Operator-facing plan summary.
    std::vector<ApprovalStep> steps;             ///< Copied steps; never extended after submission.
    size_t cursor = 0;                           ///< Next step to dispatch, or step that failed.
    SessionState state = SessionState::Awaiting; ///< Awaiting, Working, Applied or Error.
    std::optional<SessionError> terminalError;   ///< Distinguishes Denied, Failed and Cancelled.
};

/// Most requests that may be Awaiting or Working at once; a further submit is refused.
inline constexpr size_t kMaxUnresolvedApprovals = 8;
/// Seconds an approval card must have been shown before Approve or Deny acts on it.
inline constexpr double kApprovalReviewDelaySeconds = 0.5;

/// Main-thread approval queue. Requests are reviewed and run in submission order. Borrowed
/// pointers and spans are invalidated by submit, takeTerminalResults or destruction; completed
/// entries remain visible in pending() only until the shell takes their terminal results.
class SessionApprovals {
public:
    /// Copies one to 32 Apply steps into a new request; rejects unknown, non-Apply and nested plan
    /// commands without changing the queue. A single Apply command is a one-step plan. Refuses
    /// while kMaxUnresolvedApprovals requests are Awaiting or Working.
    std::expected<uint64_t, std::string> submit(uint64_t request, std::string client,
                                                std::string summary,
                                                std::vector<ApprovalStep> steps);
    /// Grants only the first unresolved Awaiting request. Unknown, queued or terminal ids do
    /// nothing.
    void approve(uint64_t id);
    /// Denies only the first unresolved Awaiting request with SessionError::Denied.
    void deny(uint64_t id);
    /// Returns the current approved step once. Returns null until approval, while a step is in
    /// flight, or after completion. The returned pointer is invalidated by submit or destruction.
    const ApprovalStep* next();
    /// Completes the in-flight step. Failure stops the plan with SessionError::Failed; success
    /// advances to the next step and marks the plan Applied after its last step.
    void finishStep(bool ok);
    /// Completes only the in-flight step of the named approval. A late completion after
    /// cancellation or replacement is ignored and returns false.
    bool finishStep(uint64_t id, bool ok);
    /// Cancels every Awaiting request with SessionError::Cancelled. An approved plan continues,
    /// including its in-flight step and remaining approved steps.
    void cancelPending();
    /// Cancels awaiting and approved work when Listen is turned off or the shell shuts down.
    void cancelAll();
    /// Removes every terminal request from the queue and returns it once, in submission order.
    /// The shell uses this single dispatch point for logging and responding, including cancelled
    /// connections.
    std::vector<PendingApproval> takeTerminalResults();
    /// Returns the oldest Awaiting or Working request, or null when all requests are terminal.
    const PendingApproval* active() const;
    /// Returns all retained requests in submission order, including terminal results not yet
    /// taken. The span is invalidated by submit, takeTerminalResults or destruction.
    std::span<const PendingApproval> pending() const;

private:
    PendingApproval* activeMutable();

    std::vector<PendingApproval> m_approvals;
    uint64_t m_nextId = 1;
    bool m_stepInFlight = false;
};

/// Holds Approve and Deny for kApprovalReviewDelaySeconds after a different request takes the
/// approval card's place, so a repeated click cannot resolve a request the operator has not seen.
class ApprovalClickGuard {
public:
    /// Records the request awaiting review this frame (zero for none) and returns whether its
    /// buttons may act. The delay restarts whenever the shown request changes.
    bool accepts(uint64_t awaitingId, double now);

private:
    uint64_t m_shownId = 0;
    double m_shownAt = 0.0;
};

} // namespace lmx::app
