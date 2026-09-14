//----------------------------------------------------------------------------------------------------------------------
/// @file EditorFont.h
/// @brief Configures the editor typeface before the first ImGui frame.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

namespace lmx::app {

/// Loads the bundled Regular face with stable digit advances, or logs and uses the embedded
/// fallback.
void configureEditorFont();

} // namespace lmx::app
