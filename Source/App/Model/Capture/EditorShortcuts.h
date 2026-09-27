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
    Capture,       ///< Request a GPU capture; an unavailable request reports its recovery.
};

/// Per-frame keyboard ownership and action prerequisites.
struct ShortcutContext {
    bool textInput = false;  ///< Text editing owns keyboard input.
    bool cameraLook = false; ///< Relative mouse look owns navigation input.
    bool popupOpen = false;  ///< An open popup owns keyboard input.
    /// Keyboard focus belongs to a window outside the main viewport, such as the detached Render
    /// Graph or Performance window.
    bool otherSurfaceFocused = false;
    bool hasSelection = false; ///< A selected object has reliable framing bounds.
};

/// Rejects all commands during editing, look, popups or focus on another surface, then checks
/// command prerequisites. Capture has none: an unavailable or pending request still reaches the
/// capture intent, which reports the recovery explanation or coalesces.
bool shortcutAllowed(EditorShortcut shortcut, const ShortcutContext& context);

} // namespace lmx::app
