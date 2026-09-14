//----------------------------------------------------------------------------------------------------------------------
/// @file EditorActions.cpp
/// @brief Implements the App-owned action intents the main menu and shortcuts emit.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/EditorActions.h"

namespace lmx::app {

//======================================================================================================================
void EditorActions::requestQuit() {
    m_quitPending = true;
}

//======================================================================================================================
bool EditorActions::consumeQuit() {
    const bool wasPending = m_quitPending;
    m_quitPending = false;
    return wasPending;
}

//======================================================================================================================
void EditorActions::requestCapture() {
    m_captureFeedbackVisible = true;
    if (!m_captureAvailable || m_captureResult.status == ActionStatus::Pending) {
        return;
    }
    m_captureResult = {ActionStatus::Pending, "Waiting for the next drawable frame.", {}};
    m_capturePending = true;
}

//======================================================================================================================
bool EditorActions::consumeCapture() {
    const bool wasPending = m_capturePending;
    m_capturePending = false;
    return wasPending;
}

//======================================================================================================================
void EditorActions::requestResetLayout() {
    m_resetLayoutPending = true;
}

//======================================================================================================================
bool EditorActions::consumeResetLayout() {
    const bool wasPending = m_resetLayoutPending;
    m_resetLayoutPending = false;
    return wasPending;
}

//======================================================================================================================
void EditorActions::configureCapture(bool available) {
    m_captureAvailable = available;
    m_capturePending = false;
    m_captureResult =
        available ? ActionResult{ActionStatus::Ready, "Capture the next GPU frame (C).", {}}
                  : ActionResult{
                        ActionStatus::Unavailable,
                        "Relaunch with MTL_CAPTURE_ENABLED=1 xmake run App to enable GPU capture.",
                        {}};
}

} // namespace lmx::app
