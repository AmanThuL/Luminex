#pragma once

#include "Engine/SceneLibrary.h"

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace lmx::app {

enum class RunMode {
    Windowed,
    Screenshot,
};

struct AppOptions {
    RunMode mode = RunMode::Windowed;
    engine::SceneId initialScene = engine::defaultSceneId();
    std::filesystem::path screenshotPath;
};

struct AppOptionsError {
    std::string message;
};

using AppOptionsResult = std::expected<AppOptions, AppOptionsError>;

// Parses arguments after the executable name. Repeated options use the final value; a bare `--`
// is ignored. An empty screenshot path preserves windowed mode. Returned options own argv data.
AppOptionsResult parseAppOptions(std::span<const std::string_view> arguments);

} // namespace lmx::app
