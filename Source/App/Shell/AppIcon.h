//----------------------------------------------------------------------------------------------------------------------
/// @file AppIcon.h
/// @brief Applies the native application icon for the windowed editor.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <filesystem>

namespace lmx::app {

/// Sets the application icon from a PNG on the main thread after SDL video initialization.
/// Logs one warning and keeps the system icon when the image cannot be loaded.
void applyApplicationIcon(const std::filesystem::path& png);

} // namespace lmx::app
