//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentWorkflow.cpp
/// @brief Sequences confirmed document work and defers quit until pending responses arrive.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/DocumentWorkflow.h"

namespace lmx::app {

//======================================================================================================================
void DocumentWorkflow::setContext(bool dirty, bool stopped, bool measuring, bool proposalPending) {
    m_dirty = dirty;
    m_stopped = stopped;
    m_measuring = measuring;
    m_proposalPending = proposalPending;
}

//======================================================================================================================
std::optional<std::string> DocumentWorkflow::unavailableReason(DocumentAction action, bool stopped,
                                                               bool measuring,
                                                               bool proposalPending) {
    if (proposalPending && (action == DocumentAction::Save || action == DocumentAction::SaveAs))
        return "Review the pending proposal first";
    if (action != DocumentAction::Save && action != DocumentAction::SaveAs &&
        action != DocumentAction::Revert)
        return {};
    if (measuring)
        return "Stop measurement before saving or reverting the scene.";
    if (!stopped)
        return "Stop playback before saving or reverting the scene.";
    return {};
}

//======================================================================================================================
bool DocumentWorkflow::offersSave(DocumentAction action) {
    return action != DocumentAction::Revert;
}

//======================================================================================================================
bool DocumentWorkflow::request(DocumentAction action, std::optional<scenes::SceneId> target) {
    if (unavailableReason(action, m_stopped, m_measuring, m_proposalPending))
        return false;
    if (m_step != WorkflowStep::Idle) {
        if (action != DocumentAction::Quit)
            return false;
        if (m_pending->action == DocumentAction::Quit)
            return true;
        if (m_step == WorkflowStep::ChoosePath || m_step == WorkflowStep::Ready) {
            m_quitQueued = true;
            return true;
        }
    }
    m_pending = PendingDocumentWork{.action = action, .target = std::move(target)};
    m_issued = false;
    const bool destructive = action != DocumentAction::Save && action != DocumentAction::SaveAs;
    if (m_dirty && destructive)
        m_step = WorkflowStep::Confirm;
    else
        advance();
    return true;
}

//======================================================================================================================
bool DocumentWorkflow::savingBeforeOpen() const {
    return m_pending && m_pending->action == DocumentAction::Open && m_pending->saveFirst;
}

//======================================================================================================================
void DocumentWorkflow::advance() {
    m_step =
        m_pending->action == DocumentAction::Open || m_pending->action == DocumentAction::SaveAs
            ? WorkflowStep::ChoosePath
            : WorkflowStep::Ready;
}

//======================================================================================================================
void DocumentWorkflow::clear() {
    m_pending.reset();
    m_step = WorkflowStep::Idle;
    m_issued = false;
}

//======================================================================================================================
void DocumentWorkflow::confirm(ConfirmChoice choice) {
    if (m_step != WorkflowStep::Confirm)
        return;
    if (choice == ConfirmChoice::Cancel) {
        clear();
    } else if (choice == ConfirmChoice::Discard) {
        advance();
    } else if (offersSave(m_pending->action) &&
               !unavailableReason(DocumentAction::Save, m_stopped, m_measuring,
                                  m_proposalPending)) {
        m_pending->saveFirst = true;
        if (savingBeforeOpen())
            m_step = WorkflowStep::Ready;
        else
            advance();
    }
}

//======================================================================================================================
void DocumentWorkflow::resumeQuit() {
    m_quitQueued = false;
    clear();
    request(DocumentAction::Quit);
}

//======================================================================================================================
void DocumentWorkflow::pathChosen(std::optional<std::filesystem::path> path) {
    if (m_step != WorkflowStep::ChoosePath)
        return;
    if (!path || path->empty()) {
        if (m_quitQueued)
            resumeQuit();
        else
            clear();
        return;
    }
    m_pending->path = std::move(path);
    m_step = WorkflowStep::Ready;
}

//======================================================================================================================
std::optional<PendingDocumentWork> DocumentWorkflow::takeWork() {
    if (m_step != WorkflowStep::Ready || m_issued)
        return {};
    m_issued = true;
    if (savingBeforeOpen())
        return PendingDocumentWork{.action = DocumentAction::Save};
    return m_pending;
}

//======================================================================================================================
void DocumentWorkflow::complete(bool success) {
    if (!m_issued)
        return;
    if (success && savingBeforeOpen()) {
        m_dirty = false;
        m_pending->saveFirst = false;
        m_issued = false;
        advance();
        return;
    }
    if (success && (m_pending->saveFirst || m_pending->action == DocumentAction::Save ||
                    m_pending->action == DocumentAction::SaveAs))
        m_dirty = false;
    if (m_quitQueued)
        resumeQuit();
    else
        clear();
}

//======================================================================================================================
std::optional<DocumentAction> DocumentWorkflow::action() const {
    return m_pending ? std::optional(m_pending->action) : std::nullopt;
}
} // namespace lmx::app
