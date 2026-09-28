//----------------------------------------------------------------------------------------------------------------------
/// @file DocumentWorkPump.h
/// @brief Declares the document mailbox/work boundary independent of drawable acquisition.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "App/Model/Scene/DocumentDialogMailbox.h"
#include "App/Model/Scene/DocumentWorkflow.h"

#include <functional>

namespace lmx::app {
/// Consumes a dialog result and ready work, including a queued Quit after its prior operation.
/// execute performs saveFirst and the action, refreshing workflow context before returning success.
/// Returns true only for successfully executed Quit; leaves it issued until the owner exits.
/// Confirmation and native-dialog presentation remain UI responsibilities. Call before acquisition.
bool pumpDocumentWork(DocumentWorkflow& workflow, DocumentDialogMailbox& mailbox,
                      const std::function<bool(const PendingDocumentWork&)>& execute,
                      const std::function<void(const std::string&)>& reportError);
} // namespace lmx::app
