//----------------------------------------------------------------------------------------------------------------------
/// @file AppOptions.h
/// @brief Declares application run modes, options, and parser errors.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/SceneLibrary.h"

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace lmx::app {

/// Selects the application's interactive or offscreen execution path.
enum class RunMode {
    Windowed,   ///< Interactive editor window.
    Screenshot, ///< Offscreen render written to disk.
};

/// Fully parsed, owned application startup configuration.
struct AppOptions {
    RunMode mode = RunMode::Windowed; ///< Execution path selected by command-line options.
    engine::SceneId initialScene = engine::defaultSceneId(); ///< Scene selected at startup.
    std::filesystem::path screenshotPath; ///< Destination used in screenshot mode.
};

/// Reports an invalid command-line option with user-facing context.
struct AppOptionsError {
    std::string message; ///< Description suitable for command-line diagnostics.
};

/// Result returned after parsing an application configuration.
using AppOptionsResult = std::expected<AppOptions, AppOptionsError>;

/// Parses arguments after the executable name. Repeated options use the final value; a bare `--`
/// is ignored. An empty screenshot path preserves windowed mode. Returned options own argv data.
AppOptionsResult parseAppOptions(std::span<const std::string_view> arguments);

} // namespace lmx::app
