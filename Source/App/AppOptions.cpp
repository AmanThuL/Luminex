//----------------------------------------------------------------------------------------------------------------------
/// @file AppOptions.cpp
/// @brief Implements command-line option parsing for application run modes.
//----------------------------------------------------------------------------------------------------------------------

#include "App/AppOptions.h"

#include <cstddef>
#include <optional>
#include <utility>

namespace lmx::app {

namespace {

//======================================================================================================================
AppOptionsResult fail(std::string message) {
    return std::unexpected(AppOptionsError{.message = std::move(message)});
}

//======================================================================================================================
// Built from the scene catalog rather than a second hardcoded list, so usage/error text can never
// drift from the IDs `--scene` actually accepts.
std::string sceneIdList(std::string_view separator) {
    std::string result;
    for (std::string_view id : engine::sceneStableIds()) {
        if (!result.empty()) {
            result += separator;
        }
        result += id;
    }
    return result;
}

} // namespace

//======================================================================================================================
AppOptionsResult parseAppOptions(std::span<const std::string_view> arguments) {
    std::string_view screenshotPath;
    std::string_view sceneName = engine::sceneIdString(engine::defaultSceneId());
    bool maximized = true;

    for (size_t i = 0; i < arguments.size(); ++i) {
        const std::string_view argument = arguments[i];
        if (argument == "--") {
            continue;
        }
        if (argument == "--screenshot") {
            if (++i >= arguments.size()) {
                return fail("--screenshot needs an output path: App --screenshot <out.bmp>");
            }
            screenshotPath = arguments[i];
        } else if (argument == "--scene") {
            if (++i >= arguments.size()) {
                return fail("--scene needs an ID: App --scene <" + sceneIdList("|") + ">");
            }
            sceneName = arguments[i];
        } else if (argument == "--windowed") {
            maximized = false;
        } else {
            return fail("unknown argument '" + std::string(argument) +
                        "'; usage: App [--screenshot <out.bmp>] [--scene <" + sceneIdList("|") +
                        ">] [--windowed]");
        }
    }

    const std::optional<engine::SceneId> sceneId = engine::parseSceneId(sceneName);
    if (!sceneId) {
        return fail("unknown scene ID '" + std::string(sceneName) +
                    "'; valid IDs: " + sceneIdList(", "));
    }

    AppOptions options;
    options.initialScene = *sceneId;
    options.maximized = maximized;
    if (!screenshotPath.empty()) {
        options.mode = RunMode::Screenshot;
        options.screenshotPath = screenshotPath;
    }
    return options;
}

} // namespace lmx::app
