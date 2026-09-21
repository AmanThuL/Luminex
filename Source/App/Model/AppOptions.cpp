//----------------------------------------------------------------------------------------------------------------------
/// @file AppOptions.cpp
/// @brief Implements command-line option parsing for application run modes.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/AppOptions.h"

#include "Core/Util/Parse.h"
#include "Render/Temporal.h"

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
std::string_view captureFormatName(CaptureFormat format) {
    switch (format) {
    case CaptureFormat::Png:
        return "png";
    case CaptureFormat::Bmp:
        return "bmp";
    }
    return "unknown";
}

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
    std::string_view measurementPath;
    bool unscored = false;
    bool measurementTrack = true;
    bool measurementCameraSpecified = false;
    bool visibilityEnabled = true;
    render::SubmissionMode submission = render::SubmissionMode::Indirect;
    render::ClassifyMode classifyMode = render::ClassifyMode::Cpu;
    bool classifyCheck = false;
    bool occlusionEnabled = false;
    bool occlusionCheck = false;
    int32_t hzbDebugLevel = -1;
    uint32_t labOccluders = 0;
    bool labOccludersSpecified = false;
    uint32_t labInstances = 4096;
    bool labInstancesSpecified = false;
    engine::LocalLightMode localLightMode = engine::LocalLightMode::Clustered;
    bool lightCheck = false;
    engine::LightDebugView lightDebugView = engine::LightDebugView::Off;
    bool localLightRig = false;
    bool localLightRigSpecified = false;
    uint32_t labLights = 256;
    bool labLightsSpecified = false;
    uint32_t labLightPile = 0;
    bool labLightPileSpecified = false;
    std::string_view screenshotPath;
    std::string_view captureSequencePath;
    uint32_t warmup = 0;
    bool warmupSpecified = false;
    CaptureFormat captureFormat = CaptureFormat::Png;
    bool captureFormatSpecified = false;
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
        if (argument == "--measure") {
            if (++i >= arguments.size() || arguments[i].empty() || !looksLikeValue(arguments[i])) {
                return fail("--measure needs a non-empty output JSON path");
            }
            measurementPath = arguments[i];
        } else if (argument == "--unscored") {
            unscored = true;
        } else if (argument == "--measure-camera") {
            if (++i >= arguments.size() || (arguments[i] != "track" && arguments[i] != "initial")) {
                return fail("--measure-camera needs track|initial");
            }
            measurementTrack = arguments[i] == "track";
            measurementCameraSpecified = true;
        } else if (argument == "--visibility") {
            if (++i >= arguments.size() || (arguments[i] != "cull" && arguments[i] != "off")) {
                return fail("--visibility needs cull|off");
            }
            visibilityEnabled = arguments[i] == "cull";
        } else if (argument == "--classify") {
            if (++i >= arguments.size() || (arguments[i] != "cpu" && arguments[i] != "gpu"))
                return fail("--classify needs cpu|gpu");
            classifyMode =
                arguments[i] == "gpu" ? render::ClassifyMode::Gpu : render::ClassifyMode::Cpu;
        } else if (argument == "--classify-check") {
            classifyCheck = true;
        } else if (argument == "--occlusion") {
            if (++i >= arguments.size() || (arguments[i] != "on" && arguments[i] != "off"))
                return fail("--occlusion needs on|off");
            occlusionEnabled = arguments[i] == "on";
        } else if (argument == "--occlusion-check") {
            occlusionCheck = true;
        } else if (argument == "--hzb-level") {
            if (++i >= arguments.size() || !parseNumber(arguments[i], hzbDebugLevel) ||
                hzbDebugLevel < 0 || hzbDebugLevel > 30)
                return fail("--hzb-level needs an integer in [0, 30]");
        } else if (argument == "--lab-occluders") {
            if (++i >= arguments.size() || !parseNumber(arguments[i], labOccluders) ||
                labOccluders > 1024)
                return fail("--lab-occluders needs an integer in [0, 1024]");
            labOccludersSpecified = true;
        } else if (argument == "--submission") {
            if (++i >= arguments.size())
                return fail("--submission needs direct|indirect|batched");
            if (arguments[i] == "direct")
                submission = render::SubmissionMode::Direct;
            else if (arguments[i] == "indirect")
                submission = render::SubmissionMode::Indirect;
            else if (arguments[i] == "batched")
                submission = render::SubmissionMode::Batched;
            else
                return fail("--submission needs direct|indirect|batched");
        } else if (argument == "--lab-instances") {
            if (++i >= arguments.size() || !parseNumber(arguments[i], labInstances) ||
                labInstances < 1 || labInstances > 1048576) {
                return fail("--lab-instances needs an integer in [1, 1048576]");
            }
            labInstancesSpecified = true;
        } else if (argument == "--local-lights") {
            if (++i >= arguments.size()) {
                return fail("--local-lights needs off, direct, or clustered");
            }
            if (arguments[i] == "off") {
                localLightMode = engine::LocalLightMode::Off;
            } else if (arguments[i] == "direct") {
                localLightMode = engine::LocalLightMode::Direct;
            } else if (arguments[i] == "clustered") {
                localLightMode = engine::LocalLightMode::Clustered;
            } else {
                return fail("--local-lights needs off, direct, or clustered");
            }
        } else if (argument == "--light-check") {
            lightCheck = true;
        } else if (argument == "--light-view") {
            if (++i >= arguments.size())
                return fail("--light-view needs off|count|overflow|missed");
            if (arguments[i] == "off")
                lightDebugView = engine::LightDebugView::Off;
            else if (arguments[i] == "count")
                lightDebugView = engine::LightDebugView::Count;
            else if (arguments[i] == "overflow")
                lightDebugView = engine::LightDebugView::Overflow;
            else if (arguments[i] == "missed")
                lightDebugView = engine::LightDebugView::Missed;
            else
                return fail("--light-view needs off|count|overflow|missed");
        } else if (argument == "--local-light-rig") {
            if (++i >= arguments.size() || (arguments[i] != "on" && arguments[i] != "off")) {
                return fail("--local-light-rig needs on or off");
            }
            localLightRig = arguments[i] == "on";
            localLightRigSpecified = true;
        } else if (argument == "--lab-lights") {
            if (++i >= arguments.size() || !parseNumber(arguments[i], labLights) || labLights < 1 ||
                labLights > engine::kMaxLocalLights) {
                return fail("--lab-lights needs an integer in [1, " +
                            std::to_string(engine::kMaxLocalLights) + "]");
            }
            labLightsSpecified = true;
        } else if (argument == "--lab-light-pile") {
            if (++i >= arguments.size() || !parseNumber(arguments[i], labLightPile) ||
                labLightPile > engine::kMaxLocalLights) {
                return fail("--lab-light-pile needs an integer in [0, " +
                            std::to_string(engine::kMaxLocalLights) + "]");
            }
            labLightPileSpecified = true;
        } else if (argument == "--screenshot") {
            if (++i >= arguments.size()) {
                return fail(
                    "--screenshot needs an output path: App --screenshot <out.png|out.bmp>");
            }
            screenshotPath = arguments[i];
        } else if (argument == "--capture-sequence") {
            if (++i >= arguments.size() || arguments[i].empty() || !looksLikeValue(arguments[i])) {
                return fail("--capture-sequence needs a non-empty output directory");
            }
            captureSequencePath = arguments[i];
        } else if (argument == "--capture-format") {
            if (++i >= arguments.size()) {
                return fail("--capture-format needs one of png|bmp");
            }
            if (arguments[i] == "png") {
                captureFormat = CaptureFormat::Png;
            } else if (arguments[i] == "bmp") {
                captureFormat = CaptureFormat::Bmp;
            } else {
                return fail("--capture-format needs one of png|bmp, got '" +
                            std::string(arguments[i]) + "'");
            }
            captureFormatSpecified = true;
        } else if (argument == "--warmup") {
            if (++i >= arguments.size()) {
                return fail("--warmup needs a non-negative frame count");
            }
            const auto raw = arguments[i];
            if (!parseNumber(raw, warmup)) {
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
            if (!parseNumber(raw, parsedFrames) || parsedFrames < 1) {
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
            if (!parseNumber(raw, parsedScale) || !std::isfinite(parsedScale) ||
                parsedScale < render::kMinRenderScale || parsedScale > render::kMaxRenderScale) {
                return fail("--render-scale needs a value in [0.5, 1.0], got '" + std::string(raw) +
                            "'");
            }
            renderScale = parsedScale;
        } else {
            return fail(
                "unknown argument '" + std::string(argument) +
                "'; usage: App [--screenshot <out.png|out.bmp>] [--scene <" + sceneIdList("|") +
                ">] [--windowed] [--frames <N>] [--temporal <off|raw|taa|metalfx>] "
                "[--temporal-view <off|motion|reprojection|reprojected|rejection|weight|age>] "
                "[--render-scale <0.5..1.0>] [--capture-sequence <directory> --warmup <N> "
                "--capture-format <png|bmp>] [--measure <out.json> --unscored] "
                "[--visibility <cull|off>] [--classify <cpu|gpu>] [--classify-check] [--submission "
                "<direct|indirect|batched>] "
                "[--lab-instances <1..1048576>] [--lab-occluders <0..1024>] "
                "[--lab-lights <1.." +
                std::to_string(engine::kMaxLocalLights) + ">] [--lab-light-pile <0.." +
                std::to_string(engine::kMaxLocalLights) +
                ">] "
                "[--light-check] [--light-view <off|count|overflow|missed>] [--local-lights "
                "<off|direct|clustered>] [--local-light-rig <on|off>] "
                "[--occlusion <on|off>] [--occlusion-check] [--hzb-level <k>] "
                "[--measure-camera <track|initial>] "
                "(--screenshot saves the last of N frames; --capture-sequence saves N frames "
                "after W unsaved warmup frames)");
        }
    }

    if ((lightCheck || lightDebugView != engine::LightDebugView::Off) &&
        localLightMode != engine::LocalLightMode::Clustered)
        return fail("--light-check and --light-view require --local-lights clustered");
    if (lightDebugView != engine::LightDebugView::Off &&
        (temporalView != render::TemporalDebugView::Off || hzbDebugLevel >= 0))
        return fail("--light-view conflicts with --temporal-view and --hzb-level");
    if ((lightCheck || lightDebugView != engine::LightDebugView::Off) && !measurementPath.empty() &&
        !unscored)
        return fail("lighting diagnostics measurement requires --unscored");
    if (occlusionEnabled && (classifyMode != render::ClassifyMode::Gpu || !visibilityEnabled))
        return fail("--occlusion on requires --classify gpu and --visibility cull");
    if (occlusionCheck && !occlusionEnabled)
        return fail("--occlusion-check requires --occlusion on");
    if (occlusionCheck && !measurementPath.empty() && !unscored)
        return fail("--occlusion-check measurement requires --unscored");
    if (hzbDebugLevel >= 0 && !occlusionEnabled)
        return fail("--hzb-level requires --occlusion on");
    if (hzbDebugLevel >= 0 && temporalView != render::TemporalDebugView::Off)
        return fail("--hzb-level conflicts with --temporal-view");
    if (hzbDebugLevel >= 0 && !measurementPath.empty() && !unscored)
        return fail("--hzb-level measurement requires --unscored");
    if (labOccludersSpecified && sceneName != "visibility-lab")
        return fail("--lab-occluders requires --scene visibility-lab");
    if (classifyMode == render::ClassifyMode::Gpu && submission == render::SubmissionMode::Direct)
        return fail("--classify gpu conflicts with --submission direct");
    if (classifyCheck && classifyMode != render::ClassifyMode::Gpu)
        return fail("--classify-check requires --classify gpu");
    if (classifyCheck && !measurementPath.empty() && !unscored)
        return fail("--classify-check measurement requires --unscored");
    if (!measurementPath.empty() && (!captureSequencePath.empty() || !screenshotPath.empty())) {
        return fail("--measure conflicts with --screenshot and --capture-sequence");
    }
    if ((unscored || measurementCameraSpecified) && measurementPath.empty()) {
        return fail("--unscored and --measure-camera require --measure");
    }
    if (labInstancesSpecified && sceneName != "visibility-lab") {
        return fail("--lab-instances requires --scene visibility-lab");
    }
    if (localLightRigSpecified && sceneName != "sponza") {
        return fail("--local-light-rig requires --scene sponza");
    }
    if (labLightsSpecified && sceneName != "light-lab") {
        return fail("--lab-lights requires --scene light-lab");
    }
    if (labLightPileSpecified && sceneName != "light-lab") {
        return fail("--lab-light-pile requires --scene light-lab");
    }
    if (uint64_t{labLights} + labLightPile > engine::kMaxLocalLights) {
        return fail("--lab-lights plus --lab-light-pile must not exceed " +
                    std::to_string(engine::kMaxLocalLights));
    }
    if (!captureSequencePath.empty() && !screenshotPath.empty()) {
        return fail("--capture-sequence conflicts with --screenshot");
    }
    if (!screenshotPath.empty()) {
        const auto extension = std::filesystem::path(screenshotPath).extension();
        if (extension != ".png" && extension != ".bmp") {
            return fail("--screenshot accepts .png or .bmp output paths, got '" +
                        std::string(screenshotPath) + "'");
        }
    }
    if (captureFormatSpecified && captureSequencePath.empty()) {
        return fail("--capture-format requires --capture-sequence");
    }
    if (warmupSpecified && captureSequencePath.empty() && measurementPath.empty()) {
        return fail("--warmup requires --capture-sequence or --measure");
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
    options.measurementPath = measurementPath;
    options.unscored = unscored;
    options.measurementTrack = measurementTrack;
    options.visibilityEnabled = visibilityEnabled;
    options.submission = submission;
    options.classifyMode = classifyMode;
    options.classifyCheck = classifyCheck;
    options.occlusionEnabled = occlusionEnabled;
    options.occlusionCheck = occlusionCheck;
    options.hzbDebugLevel = hzbDebugLevel;
    options.labOccluders = labOccluders;
    options.labInstances = labInstances;
    options.localLightMode = localLightMode;
    options.lightCheck = lightCheck;
    options.lightDebugView = lightDebugView;
    options.localLightRig = localLightRigSpecified ? localLightRig : sceneName == "sponza";
    options.labLights = labLights;
    options.labLightPile = labLightPile;
    options.initialScene = *sceneId;
    options.maximized = maximized;
    options.frames = frames;
    options.warmup = warmup;
    options.captureFormat = captureFormat;
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
    if (!measurementPath.empty())
        options.mode = RunMode::Measure;
    return options;
}

} // namespace lmx::app
