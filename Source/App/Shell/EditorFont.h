//----------------------------------------------------------------------------------------------------------------------
/// @file EditorFont.h
/// @brief Configures the editor typeface before the first ImGui frame.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

namespace lmx::app {

/// Loads the bundled Regular face with stable digit advances, or logs and uses the embedded
/// fallback. Merges Codicons when available and returns true only when icons loaded.
bool configureEditorFont();

} // namespace lmx::app
