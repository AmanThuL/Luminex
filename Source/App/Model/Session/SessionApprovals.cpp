//----------------------------------------------------------------------------------------------------------------------
/// @file SessionApprovals.cpp
/// @brief Validates session plans and advances operator-approved steps.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Session/SessionApprovals.h"

#include "Core/Diagnostics/Assert.h"

#include <algorithm>
#include <utility>

namespace lmx::app {
namespace {

constexpr size_t kMaxApprovalSteps = 32;

//======================================================================================================================
bool isUnresolved(const PendingApproval& approval) {
    return approval.state == SessionState::Awaiting || approval.state == SessionState::Working;
}

//======================================================================================================================
bool isExecutable(SessionCommand command) {
    const auto specs = sessionCommands();
    const auto found = std::find_if(specs.begin(), specs.end(), [command](const CommandSpec& spec) {
        return spec.command == command;
    });
    return found != specs.end() && found->tier == SessionTier::Apply &&
           command != SessionCommand::PlanSubmit;
}

} // namespace

//======================================================================================================================
std::expected<uint64_t, std::string> SessionApprovals::submit(uint64_t request, std::string client,
                                                              std::string summary,
                                                              std::vector<ApprovalStep> steps) {
    if (steps.empty())
        return std::unexpected("Approval requires at least one step");
    if (steps.size() > kMaxApprovalSteps)
        return std::unexpected("Approval cannot exceed 32 steps");
    if (std::any_of(steps.begin(), steps.end(),
                    [](const ApprovalStep& step) { return !isExecutable(step.command); }))
        return std::unexpected(
            "Approval steps must be known Apply commands other than plan.submit");
    LMX_ASSERT(m_nextId != 0, "Approval identity exhausted");
    const uint64_t id = m_nextId++;
    m_approvals.push_back({id, request, std::move(client), std::move(summary), std::move(steps)});
    return id;
}

//======================================================================================================================
void SessionApprovals::approve(uint64_t id) {
    auto* approval = activeMutable();
    if (approval && approval->id == id && approval->state == SessionState::Awaiting)
        approval->state = SessionState::Working;
}

//======================================================================================================================
void SessionApprovals::deny(uint64_t id) {
    auto* approval = activeMutable();
    if (approval && approval->id == id && approval->state == SessionState::Awaiting) {
        approval->state = SessionState::Error;
        approval->terminalError = SessionError::Denied;
    }
}

//======================================================================================================================
const ApprovalStep* SessionApprovals::next() {
    auto* approval = activeMutable();
    if (!approval || approval->state != SessionState::Working || m_stepInFlight)
        return nullptr;
    LMX_ASSERT(approval->cursor < approval->steps.size(), "Approved plan has no remaining step");
    m_stepInFlight = true;
    return &approval->steps[approval->cursor];
}

//======================================================================================================================
void SessionApprovals::finishStep(bool ok) {
    auto* approval = activeMutable();
    LMX_ASSERT(approval && approval->state == SessionState::Working && m_stepInFlight,
               "No approved step is in flight");
    m_stepInFlight = false;
    if (!ok) {
        approval->state = SessionState::Error;
        approval->terminalError = SessionError::Failed;
        return;
    }
    ++approval->cursor;
    if (approval->cursor == approval->steps.size())
        approval->state = SessionState::Applied;
}

//======================================================================================================================
bool SessionApprovals::finishStep(uint64_t id, bool ok) {
    const auto* approval = active();
    if (!approval || approval->id != id || approval->state != SessionState::Working ||
        !m_stepInFlight)
        return false;
    finishStep(ok);
    return true;
}

//======================================================================================================================
void SessionApprovals::cancelPending() {
    for (auto& approval : m_approvals) {
        if (approval.state == SessionState::Awaiting) {
            approval.state = SessionState::Error;
            approval.terminalError = SessionError::Cancelled;
        }
    }
}

//======================================================================================================================
void SessionApprovals::cancelAll() {
    for (auto& approval : m_approvals) {
        if (isUnresolved(approval)) {
            approval.state = SessionState::Error;
            approval.terminalError = SessionError::Cancelled;
        }
    }
    m_stepInFlight = false;
}

//======================================================================================================================
const PendingApproval* SessionApprovals::active() const {
    const auto found = std::find_if(m_approvals.begin(), m_approvals.end(), isUnresolved);
    return found == m_approvals.end() ? nullptr : &*found;
}

//======================================================================================================================
std::span<const PendingApproval> SessionApprovals::pending() const {
    return m_approvals;
}

//======================================================================================================================
PendingApproval* SessionApprovals::activeMutable() {
    return const_cast<PendingApproval*>(std::as_const(*this).active());
}

} // namespace lmx::app
