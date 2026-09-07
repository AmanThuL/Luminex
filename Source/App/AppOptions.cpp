//----------------------------------------------------------------------------------------------------------------------
/// @file AppOptions.cpp
/// @brief Implements command-line option parsing for application run modes.
//----------------------------------------------------------------------------------------------------------------------

#include "App/AppOptions.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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
    uint32_t frames = 1;
    bool temporal = false;
    render::TemporalDebugView temporalView = render::TemporalDebugView::Off;

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
        } else if (argument == "--frames") {
            if (++i >= arguments.size()) {
                return fail("--frames needs a count: App --frames <N> (N >= 1)");
            }
            const std::string_view raw = arguments[i];
            uint32_t parsedFrames = 0;
            const auto [end, ec] =
                std::from_chars(raw.data(), raw.data() + raw.size(), parsedFrames);
            if (ec != std::errc{} || end != raw.data() + raw.size() || parsedFrames < 1) {
                return fail("--frames needs a positive integer: App --frames <N> (N >= 1)");
            }
            frames = parsedFrames;
        } else if (argument == "--temporal") {
            temporal = true;
        } else if (argument == "--temporal-view") {
            if (++i >= arguments.size()) {
                return fail(
                    "--temporal-view needs a value: App --temporal-view <off|motion|reprojection>");
            }
            const std::string_view value = arguments[i];
            if (value == "off") {
                temporalView = render::TemporalDebugView::Off;
            } else if (value == "motion") {
                temporalView = render::TemporalDebugView::MotionVectors;
            } else if (value == "reprojection") {
                temporalView = render::TemporalDebugView::ReprojectionError;
            } else {
                return fail("--temporal-view needs one of off|motion|reprojection, got '" +
                            std::string(value) + "'");
            }
            // Naming a view is itself an opt-in, even "off" -- it says the caller cares about the
            // temporal path's behavior, not merely its display.
            temporal = true;
        } else {
            return fail("unknown argument '" + std::string(argument) +
                        "'; usage: App [--screenshot <out.bmp>] [--scene <" + sceneIdList("|") +
                        ">] [--windowed] [--frames <N>] [--temporal] "
                        "[--temporal-view <off|motion|reprojection>]");
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
    options.frames = frames;
    options.temporal = temporal;
    options.temporalView = temporalView;
    if (!screenshotPath.empty()) {
        options.mode = RunMode::Screenshot;
        options.screenshotPath = screenshotPath;
    }
    return options;
}

} // namespace lmx::app
