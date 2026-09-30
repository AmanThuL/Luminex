//----------------------------------------------------------------------------------------------------------------------
/// @file EditorFont.h
/// @brief Configures the editor typeface before the first ImGui frame.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

struct ImFont;

namespace lmx::app {

/// Borrowed atlas faces, valid until the owning ImGui context is destroyed.
struct EditorFonts {
    ImFont* sans = nullptr;       ///< Regular, or the embedded fallback.
    ImFont* sansMedium = nullptr; ///< Medium, falling back to sans.
    ImFont* mono = nullptr;       ///< Mono, falling back to sans.
    bool icons = false;           ///< Codicons loaded into the Sans faces.
};

/// Loads bundled Geist before the first frame. Sans digits use fixed 0.6 em advances.
/// Missing faces warn once per process; Medium/Mono use Sans and Regular uses the embedded face.
EditorFonts configureEditorFonts();

} // namespace lmx::app
