//----------------------------------------------------------------------------------------------------------------------
/// @file EditorActions.cpp
/// @brief Implements the App-owned action intents the main menu and shortcuts emit.
//----------------------------------------------------------------------------------------------------------------------

#include "App/EditorActions.h"

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

} // namespace lmx::app
