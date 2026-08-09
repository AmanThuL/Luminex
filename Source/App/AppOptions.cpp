#include "App/AppOptions.h"

#include <cstddef>
#include <optional>
#include <utility>

namespace lmx::app {

namespace {

constexpr std::string_view kSceneIds = "sponza|damaged-helmet";

//======================================================================================================================
AppOptionsResult fail(std::string message) {
    return std::unexpected(AppOptionsError{.message = std::move(message)});
}

} // namespace

//======================================================================================================================
AppOptionsResult parseAppOptions(std::span<const std::string_view> arguments) {
    std::string_view screenshotPath;
    std::string_view sceneName = engine::sceneIdString(engine::defaultSceneId());

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
                return fail("--scene needs an ID: App --scene <" + std::string(kSceneIds) + ">");
            }
            sceneName = arguments[i];
        } else {
            return fail("unknown argument '" + std::string(argument) +
                        "'; usage: App [--screenshot <out.bmp>] [--scene <" +
                        std::string(kSceneIds) + ">]");
        }
    }

    const std::optional<engine::SceneId> sceneId = engine::parseSceneId(sceneName);
    if (!sceneId) {
        return fail("unknown scene ID '" + std::string(sceneName) +
                    "'; valid IDs: sponza, "
                    "damaged-helmet");
    }

    AppOptions options;
    options.initialScene = *sceneId;
    if (!screenshotPath.empty()) {
        options.mode = RunMode::Screenshot;
        options.screenshotPath = screenshotPath;
    }
    return options;
}

} // namespace lmx::app
