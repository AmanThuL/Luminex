//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentWorkPump.cpp
/// @brief Drains completed native responses and ready document work without a render frame.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Scene/DocumentWorkPump.h"

namespace lmx::app {

//======================================================================================================================
bool pumpDocumentWork(DocumentWorkflow& workflow, DocumentDialogMailbox& mailbox,
                      const std::function<bool(const PendingDocumentWork&)>& execute,
                      const std::function<void(const std::string&)>& reportError) {
    if (auto result = mailbox.take()) {
        if (!result->error.empty() && reportError)
            reportError(result->error);
        workflow.pathChosen(result->error.empty() ? std::move(result->path) : std::nullopt);
    }
    while (const auto work = workflow.takeWork()) {
        const bool succeeded = execute(*work);
        if (work->action == DocumentAction::Quit && succeeded)
            return true;
        workflow.complete(succeeded);
    }
    return false;
}
} // namespace lmx::app
