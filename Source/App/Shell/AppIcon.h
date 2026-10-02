//----------------------------------------------------------------------------------------------------------------------
/// @file AppIcon.h
/// @brief Applies the native application name and icon for the windowed editor.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <filesystem>

namespace lmx::app {

/// Names the unbundled process "Luminex" for the macOS menu bar; call before SDL video
/// initialization creates the application. The executable file keeps its own name.
void applyApplicationName();

/// Sets the application icon from a PNG on the main thread after SDL video initialization.
/// Logs one warning and keeps the system icon when the image cannot be loaded.
void applyApplicationIcon(const std::filesystem::path& png);

} // namespace lmx::app
