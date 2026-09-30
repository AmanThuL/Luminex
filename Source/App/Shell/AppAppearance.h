//----------------------------------------------------------------------------------------------------------------------
/// @file AppAppearance.h
/// @brief Queries system appearance and applies native editor window themes.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "App/Model/Workspace/EditorTheme.h"

#include <optional>

namespace lmx::app {

/// Reads SDL's system theme on the main thread after SDL video initialization.
SystemTheme systemTheme();

/// Reads the current macOS Reduce Motion preference on the main thread.
bool reduceMotion();

/// Applies a forced appearance, or clears it to inherit the system, on each native viewport.
/// Call on the main thread after ImGui::UpdatePlatformWindows with a current ImGui context.
void applyViewportAppearance(std::optional<ThemeKind> appearance);

} // namespace lmx::app
