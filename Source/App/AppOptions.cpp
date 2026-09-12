//----------------------------------------------------------------------------------------------------------------------
/// @file AppOptions.cpp
/// @brief Implements command-line option parsing for application run modes.
//----------------------------------------------------------------------------------------------------------------------

#include "App/AppOptions.h"

#include "Render/Temporal.h"
#include "Render/VendorTemporalScaler.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

//======================================================================================================================
// A following token starting with "--" is the next option, not this option's value.
bool looksLikeValue(std::string_view token) {
    return !token.starts_with("--");
}

//======================================================================================================================
// The CLI token that names `view`, for the --temporal off / --temporal-view conflict message --
// the inverse of the value parsing in parseAppOptions.
std::string_view temporalViewName(render::TemporalDebugView view) {
    switch (view) {
    case render::TemporalDebugView::Off:
        return "off";
    case render::TemporalDebugView::MotionVectors:
        return "motion";
    case render::TemporalDebugView::ReprojectionError:
        return "reprojection";
    case render::TemporalDebugView::ReprojectedHistory:
        return "reprojected";
    case render::TemporalDebugView::RejectionMask:
        return "rejection";
    case render::TemporalDebugView::BlendWeight:
        return "weight";
    case render::TemporalDebugView::HistoryAge:
        return "age";
    }
}

} // namespace

//======================================================================================================================
render::ReconstructionMode temporalReconstructionMode(TemporalMode mode) {
    switch (mode) {
    case TemporalMode::Raw:
        return render::ReconstructionMode::Raw;
    case TemporalMode::Vendor:
        return render::ReconstructionMode::VendorTemporal;
    case TemporalMode::Off:
    case TemporalMode::Taa:
        return render::ReconstructionMode::NativeTaa;
    }
    return render::ReconstructionMode::NativeTaa;
}

//======================================================================================================================
AppOptionsResult parseAppOptions(std::span<const std::string_view> arguments) {
    std::string_view screenshotPath;
    std::string_view captureSequencePath;
    uint32_t warmup = 0;
    bool warmupSpecified = false;
    std::string_view sceneName = engine::sceneIdString(engine::defaultSceneId());
    bool maximized = true;
    uint32_t frames = 1;
    TemporalMode temporal = TemporalMode::Taa;
    render::TemporalDebugView temporalView = render::TemporalDebugView::Off;
    float renderScale = 1.0f;

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
        } else if (argument == "--capture-sequence") {
            if (++i >= arguments.size() || arguments[i].empty() || !looksLikeValue(arguments[i])) {
                return fail("--capture-sequence needs a non-empty output directory");
            }
            captureSequencePath = arguments[i];
        } else if (argument == "--warmup") {
            if (++i >= arguments.size()) {
                return fail("--warmup needs a non-negative frame count");
            }
            const auto raw = arguments[i];
            const auto [end, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), warmup);
            if (ec != std::errc{} || end != raw.data() + raw.size()) {
                return fail("--warmup needs a non-negative frame count");
            }
            warmupSpecified = true;
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
            // The value is optional: a following token that does not itself look like another
            // option is consumed as the mode; absent (or followed by another flag) means taa.
            if (i + 1 < arguments.size() && looksLikeValue(arguments[i + 1])) {
                ++i;
                const std::string_view value = arguments[i];
                if (value == "off") {
                    temporal = TemporalMode::Off;
                } else if (value == "raw") {
                    temporal = TemporalMode::Raw;
                } else if (value == "taa") {
                    temporal = TemporalMode::Taa;
                } else if (value == "metalfx") {
                    temporal = TemporalMode::Vendor;
                } else {
                    return fail("--temporal needs one of off|raw|taa|metalfx, got '" +
                                std::string(value) + "'");
                }
            } else {
                temporal = TemporalMode::Taa;
            }
        } else if (argument == "--temporal-view") {
            if (++i >= arguments.size()) {
                return fail("--temporal-view needs a value: App --temporal-view "
                            "<off|motion|reprojection|reprojected|rejection|weight|age>");
            }
            const std::string_view value = arguments[i];
            if (value == "off") {
                temporalView = render::TemporalDebugView::Off;
            } else if (value == "motion") {
                temporalView = render::TemporalDebugView::MotionVectors;
            } else if (value == "reprojection") {
                temporalView = render::TemporalDebugView::ReprojectionError;
            } else if (value == "reprojected") {
                temporalView = render::TemporalDebugView::ReprojectedHistory;
            } else if (value == "rejection") {
                temporalView = render::TemporalDebugView::RejectionMask;
            } else if (value == "weight") {
                temporalView = render::TemporalDebugView::BlendWeight;
            } else if (value == "age") {
                temporalView = render::TemporalDebugView::HistoryAge;
            } else {
                return fail("--temporal-view needs one of "
                            "off|motion|reprojection|reprojected|rejection|weight|age, got '" +
                            std::string(value) + "'");
            }
        } else if (argument == "--render-scale") {
            if (++i >= arguments.size()) {
                return fail("--render-scale needs a value: App --render-scale <0.5..1.0>");
            }
            const std::string_view raw = arguments[i];
            float parsedScale = 0.0f;
            const auto [end, ec] =
                std::from_chars(raw.data(), raw.data() + raw.size(), parsedScale);
            if (ec != std::errc{} || end != raw.data() + raw.size() ||
                !std::isfinite(parsedScale) || parsedScale < render::kMinRenderScale ||
                parsedScale > render::kMaxRenderScale) {
                return fail("--render-scale needs a value in [0.5, 1.0], got '" + std::string(raw) +
                            "'");
            }
            renderScale = parsedScale;
        } else {
            return fail(
                "unknown argument '" + std::string(argument) +
                "'; usage: App [--screenshot <out.bmp>] [--scene <" + sceneIdList("|") +
                ">] [--windowed] [--frames <N>] [--temporal <off|raw|taa|metalfx>] "
                "[--temporal-view <off|motion|reprojection|reprojected|rejection|weight|age>] "
                "[--render-scale <0.5..1.0>] [--capture-sequence <directory> --warmup <N>] "
                "(--screenshot saves the last of N frames; --capture-sequence saves N frames "
                "after W unsaved warmup frames)");
        }
    }

    if (!captureSequencePath.empty() && !screenshotPath.empty()) {
        return fail("--capture-sequence conflicts with --screenshot");
    }
    if (warmupSpecified && captureSequencePath.empty()) {
        return fail("--warmup requires --capture-sequence");
    }
    if (uint64_t{warmup} + frames > std::numeric_limits<uint32_t>::max()) {
        return fail("--warmup plus --frames exceeds the supported frame count");
    }

    if (temporal == TemporalMode::Vendor && render::nativeOnlyTemporalView(temporalView)) {
        return fail("--temporal metalfx conflicts with --temporal-view " +
                    std::string(temporalViewName(temporalView)) +
                    ": this diagnostic requires native reconstruction");
    }

    // --temporal off leaves nothing for the temporal path to draw a diagnostic over.
    if (temporal == TemporalMode::Off && temporalView != render::TemporalDebugView::Off) {
        return fail("--temporal off conflicts with --temporal-view " +
                    std::string(temporalViewName(temporalView)) +
                    ": the temporal path must run to draw a diagnostic view");
    }
    // --temporal off runs no reconstruction, so a render scale below the output extent has nothing
    // to upscale it back with.
    if (temporal == TemporalMode::Off && renderScale != 1.0f) {
        return fail("--temporal off conflicts with --render-scale: the temporal path must run to "
                    "reconstruct a render scale below 1.0");
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
    options.warmup = warmup;
    options.captureSequencePath = captureSequencePath;
    options.temporal = temporal;
    options.temporalView = temporalView;
    options.renderScale = renderScale;
    if (!screenshotPath.empty()) {
        options.mode = RunMode::Screenshot;
        options.screenshotPath = screenshotPath;
    }
    if (!captureSequencePath.empty()) {
        options.mode = RunMode::CaptureSequence;
    }
    return options;
}

} // namespace lmx::app
