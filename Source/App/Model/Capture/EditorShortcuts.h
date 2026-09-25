//----------------------------------------------------------------------------------------------------------------------
/// @file EditorShortcuts.h
/// @brief Declares focus and capability gates for global editor shortcuts.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

namespace lmx::app {

/// Global editor commands whose keyboard routes share one focus policy.
enum class EditorShortcut {
    FrameSelected, ///< Fit the selected object's reliable bounds.
    ResetCamera,   ///< Restore the scene camera.
    Capture,       ///< Request a GPU capture when available.
};

/// Per-frame keyboard ownership and action prerequisites.
struct ShortcutContext {
    bool textInput = false;        ///< Text editing owns keyboard input.
    bool cameraLook = false;       ///< Relative mouse look owns navigation input.
    bool popupOpen = false;        ///< An open popup owns keyboard input.
    bool captureAvailable = false; ///< Capture is available and has no pending request.
    bool hasSelection = false;     ///< A selected object has reliable framing bounds.
};

/// Rejects all commands during editing, look or popups, then checks command prerequisites.
bool shortcutAllowed(EditorShortcut shortcut, const ShortcutContext& context);

} // namespace lmx::app
