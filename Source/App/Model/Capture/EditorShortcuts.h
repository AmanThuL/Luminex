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
    Document,      ///< Request a document action; workflow owns dirty and transport gates.
    Gizmo,         ///< Choose transient transform tools or axes without editing the scene.
    Quit,          ///< Request quit from any focus; its workflow confirms unsaved changes.
};

/// Per-frame keyboard ownership and action prerequisites.
struct ShortcutContext {
    bool textInput = false;  ///< Text editing or another active widget owns keyboard input.
    bool cameraLook = false; ///< Relative mouse look owns navigation input.
    bool popupOpen = false;  ///< An open popup owns keyboard input.
    /// Keyboard focus belongs to a window outside the main viewport, such as the detached Render
    /// Graph or Performance window.
    bool otherSurfaceFocused = false;
    bool hasSelection = false; ///< A selected object has reliable framing bounds.
    /// An active ImGui InputText state was observed for the current active item.
    bool textFieldFocused = false;
};

/// Rejects all commands but Quit during editing, look, popups or focus on another surface, then
/// checks command prerequisites. Quit is unconditional. Capture has no prerequisite: an unavailable
/// or pending request still reaches the capture intent, which reports the recovery explanation or
/// coalesces.
bool shortcutAllowed(EditorShortcut shortcut, const ShortcutContext& context);

} // namespace lmx::app
