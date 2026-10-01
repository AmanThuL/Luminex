//----------------------------------------------------------------------------------------------------------------------
/// @file EditorShortcuts.cpp
/// @brief Applies global editor shortcut focus and capability gates.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Capture/EditorShortcuts.h"

namespace lmx::app {

//======================================================================================================================
bool shortcutAllowed(EditorShortcut shortcut, const ShortcutContext& context) {
    if (shortcut == EditorShortcut::Quit)
        return true;
    if (context.textInput || context.cameraLook || context.popupOpen || context.otherSurfaceFocused)
        return false;
    switch (shortcut) {
    case EditorShortcut::FrameSelected:
        return context.hasSelection;
    case EditorShortcut::ResetCamera:
    case EditorShortcut::Capture:
    case EditorShortcut::Document:
    case EditorShortcut::Quit:
        return true;
    }
    return false;
}

} // namespace lmx::app
