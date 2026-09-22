//----------------------------------------------------------------------------------------------------------------------
/// @file Screenshot.cpp
/// @brief Implements deterministic offscreen scene capture.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Headless/Screenshot.h"

#include "App/Headless/OcclusionValidation.h"
#include "App/Model/CaptureMetadata.h"
#include "App/Model/DynamicResolution.h"
#include "App/Model/Graph/FrameRecordRing.h"
#include "App/Model/LightCheckCapture.h"
#include "App/Model/LightingDiagnostics.h"
#include "App/Model/Scene/SceneDefaults.h"
#include "App/Model/Scene/SceneSession.h"
#include "App/Model/VisibilityDiagnostics.h"
#include "Core/Diagnostics/Log.h"
#include "Core/Util/Parse.h"
#include "Engine/Asset/Image/BmpImage.h"
#include "Engine/Asset/Image/PngImage.h"
#include "Engine/Scene/Scene.h"
#include "Render/Graph/FrameDeclaration.h"
#include "Render/Renderer/Renderer.h"
#include "Scenes/SceneLibrary.h"
#include <rojoRHI/RHI.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace lmx::app {

namespace {

// Fixed dimensions keep headless output reproducible across machines.
constexpr uint32_t kScreenshotWidth = 1280;
constexpr uint32_t kScreenshotHeight = 720;

//======================================================================================================================
bool writeCaptureImage(const std::filesystem::path& path, const std::vector<uint8_t>& bgra,
                       const render::DisplayDomain& display, std::string frameMetadata) {
    if (path.extension() == ".bmp") {
        return asset::writeBmp(path, bgra, kScreenshotWidth, kScreenshotHeight);
    }
    if (path.extension() != ".png") {
        LMX_LOG_ERROR("capture accepts .png or .bmp output paths: {}", path.string());
        return false;
    }
    std::vector<uint8_t> rgba = bgra;
    for (size_t pixel = 0; pixel < rgba.size(); pixel += 4) {
        std::swap(rgba[pixel], rgba[pixel + 2]);
    }
    const std::array<asset::PngTextChunk, 2> text = {
        asset::PngTextChunk{"lmx:display", render::toJson(display)},
        asset::PngTextChunk{"lmx:frame", std::move(frameMetadata)}};
    const auto written = asset::writePng(path, rgba, kScreenshotWidth, kScreenshotHeight, text);
    if (!written) {
        LMX_LOG_ERROR("capture: {}", written.error().message);
        return false;
    }
    return true;
}

//======================================================================================================================
// A flat readback is a scene-independent liveness failure without imposing a golden image.
bool isFlatImage(const std::vector<uint8_t>& bgra) {
    if (bgra.size() < 4) {
        return true;
    }
    for (size_t i = 4; i + 3 < bgra.size(); i += 4) {
        if (bgra[i] != bgra[0] || bgra[i + 1] != bgra[1] || bgra[i + 2] != bgra[2] ||
            bgra[i + 3] != bgra[3]) {
            return false;
        }
    }
    return true;
}

//======================================================================================================================
bool writeManifest(const AppOptions& options, std::string_view device,
                   const render::DisplayDomain& display, bool cameraTrack,
                   const std::vector<std::string>& records, bool complete,
                   std::string_view failure = {}) {
    std::ofstream file(options.captureSequencePath / "manifest.json", std::ios::trunc);
    if (!file)
        return false;
    file << captureManifestJson(options, device, display, kScreenshotWidth, kScreenshotHeight,
                                cameraTrack, records, complete, failure);
    file.close();
    return static_cast<bool>(file);
}

//======================================================================================================================
int runOffscreen(AppOptions options) {
    const auto& outPath = options.screenshotPath;
    const auto sceneId = options.initialScene;
    const auto frames = options.frames;
    const auto temporal = options.temporal;
    const auto temporalView = options.temporalView;
    const auto renderScale = options.renderScale;
    const auto visibilityEnabled = options.visibilityEnabled;
    const auto submission = options.submission;
    const auto labInstances = options.labInstances;
    const auto classifyMode = options.classifyMode;
    const auto classifyCheck = options.classifyCheck;
    const auto occlusionEnabled = options.occlusionEnabled;
    const auto occlusionCheck = options.occlusionCheck;
    const auto hzbDebugLevel = options.hzbDebugLevel;
    const auto labOccluders = options.labOccluders;
    const auto labLights = options.labLights;
    const auto labLightPile = options.labLightPile;
    const auto localLightMode = options.localLightMode;
    const auto localLightRig = options.localLightRig;
    EditorRenderSettings resolutionSettings;
    resolutionSettings.temporalEnabled = temporal != TemporalMode::Off;
    resolutionSettings.renderScale = renderScale;
    if (const char* raw = std::getenv("LMX_DYNAMIC_RESOLUTION_BUDGET_MS")) {
        float budget = 0;
        if (!parseNumber(std::string_view(raw), budget) || !std::isfinite(budget) || budget <= 0 ||
            !resolutionSettings.temporalEnabled) {
            LMX_LOG_ERROR(
                "capture dynamic resolution requires temporal and a positive finite budget");
            return 1;
        }
        resolutionSettings.dynamicResolutionEnabled = true;
        resolutionSettings.gpuBudgetMilliseconds = budget;
        options.dynamicResolution = true;
    }
    const AppOptions* sequence = options.mode == RunMode::CaptureSequence ? &options : nullptr;
    std::ofstream lightDump;
    if (const char* raw = std::getenv("LMX_LIGHT_CHECK_DUMP")) {
        if (!options.lightCheck || !*raw || std::filesystem::exists(raw)) {
            LMX_LOG_ERROR("LMX_LIGHT_CHECK_DUMP requires --light-check and a new nonempty path");
            return 1;
        }
        lightDump.open(raw, std::ios::binary);
        if (!lightDump || !writeLightCheckHeader(lightDump)) {
            LMX_LOG_ERROR("cannot create raw light-check evidence");
            return 1;
        }
    }
    render::LightingStatus captureLighting;
    std::vector<std::string> records;
    render::VisibilityStatus captureVisibility;
    if (sequence) {
        std::error_code error;
        std::filesystem::create_directories(sequence->captureSequencePath, error);
        if (error || !std::filesystem::is_empty(sequence->captureSequencePath, error) || error) {
            LMX_LOG_ERROR("capture directory must be new or empty: {}",
                          sequence->captureSequencePath.string());
            return 1;
        }
    }
    auto device = rojoRHI::createDevice();
    if (!device) {
        LMX_LOG_ERROR("createDevice failed: {}", device.error().message);
        return 1;
    }
    LMX_LOG_INFO("Metal 4 device: {}", (*device)->deviceName());

    scenes::SceneLibrary library(**device, labInstances, labOccluders, labLights, labLightPile);
    const scenes::SceneEntry& entry = library.entry(sceneId);
    if (!entry.available) {
        std::cerr << "Error: " << entry.stableId << " assets missing; " << entry.hint << '\n';
        return 1;
    }
    auto scene = library.get(sceneId);
    if (!scene) {
        // Availability cannot detect corrupt or partial assets; preserve the loader's diagnosis.
        LMX_LOG_ERROR("scene '{}' failed to load: {}", entry.stableId, scene.error().message);
        return 1;
    }
    engine::Scene* activeScene = *scene;
    LMX_LOG_INFO("scene: {} ({} objects)", activeScene->name, activeScene->objects.size());

    // The renderer dies before its frame transients and their pool, while the device is alive.
    render::TransientPool transientPool(**device);

    // Shared storage permits direct CPU readback after GPU completion.
    auto renderer = render::Renderer::create(**device, kScreenshotWidth, kScreenshotHeight,
                                             /*cpuReadback=*/true);
    if (!renderer) {
        LMX_LOG_ERROR("Renderer::create failed: {}", renderer.error().message);
        return 1;
    }
    (*renderer)->clearColor[0] = kSceneClearGray;
    (*renderer)->clearColor[1] = kSceneClearGray;
    (*renderer)->clearColor[2] = kSceneClearGray;
    (*renderer)->clearColor[3] = 1.0f;

    SceneSession session;
    session.activate(*activeScene, SceneActivationMotion::PreserveLoadedMotion);
    if (session.localLightRigAvailable()) {
        if (auto rig = session.setLocalLightRig(localLightRig); !rig) {
            LMX_LOG_ERROR("local-light rig failed: {}", rig.error().message);
            return 1;
        }
    }
    const engine::Camera& camera = session.camera();
    const bool hasCameraTrack = !activeScene->animation.cameraTrack.empty();
    FrameRecordRing frameRecords;
    DynamicResolutionState resolutionState;
    render::ResolutionController resolutionController;

    if (sequence && !writeManifest(*sequence, (*device)->deviceName(), (*renderer)->displayDomain(),
                                   hasCameraTrack, records, false)) {
        return 1;
    }
    const auto scaleStep = readOcclusionScaleStep(true, temporal != TemporalMode::Off);
    if (!scaleStep) {
        LMX_LOG_ERROR("{}", scaleStep.error());
        return 1;
    }
    if (*scaleStep && options.dynamicResolution) {
        LMX_LOG_ERROR("capture cannot combine an extent step with dynamic resolution");
        return 1;
    }
    const uint32_t totalFrames = sequence ? sequence->warmup + frames : frames;
    for (uint32_t frame = 0; frame < totalFrames; ++frame) {
        if (sequence) {
            session.prepareSequenceFrame(frame);
        } else {
            session.prepareScreenshotFrame(frame);
        }

        rojoRHI::CommandList& commands = (*device)->beginFrame();
        frameRecords.joinTimings((*device)->passTimingsFrame(), (*device)->passTimings());
        applyDynamicResolution(resolutionState, resolutionController, resolutionSettings,
                               frameRecords.newestTimedFrame());
        if (auto prepared = session.prepareFrame((*device)->frameNumber()); !prepared) {
            LMX_LOG_ERROR("scene table preparation failed: {}", prepared.error().message);
            (*device)->endFrame(nullptr);
            (*device)->waitIdle();
            return 1;
        }
        std::vector<engine::DrawItem> items;
        render::SceneView view =
            session.view(items, render::ShadowFilter::PCF, /*wireframe=*/false);
        // Bloom defaults on here exactly as in the editor (spec 10); auto-exposure defaults off
        // (spec 9). LMX_SCREENSHOT_NO_BLOOM exists solely for the M5 parity check against pre-bloom
        // output -- "with auto exposure off and bloom off, a frame is byte-identical to the
        // pre-change tip" -- and is not a documented user-facing option.
        if (std::getenv("LMX_SCREENSHOT_NO_BLOOM") != nullptr) {
            view.bloomEnabled = false;
        }
        // A one-shot process has no prior generation to differ from and never teleports its own
        // camera, so both stay at SceneView's defaults (0, false).
        view.localLightMode = localLightMode;
        view.lightCheck = options.lightCheck;
        view.lightDebugView = options.lightDebugView;
        view.visibilityEnabled = visibilityEnabled;
        view.submission = submission;
        view.classifyMode = classifyMode;
        view.classifyCheck = classifyCheck;
        view.occlusionEnabled = occlusionEnabled;
        view.occlusionCheck = occlusionCheck;
        view.hzbDebugLevel = hzbDebugLevel;
        view.temporal.enabled = temporal != TemporalMode::Off;
        view.temporal.jitterEnabled = temporal != TemporalMode::Off;
        view.temporal.reconstruction = temporalReconstructionMode(temporal);
        view.temporal.debugView = temporalView;
        view.temporal.renderScale = *scaleStep && frame >= (**scaleStep).frame
                                        ? (**scaleStep).scale
                                        : resolutionSettings.renderScale;

        render::FrameDeclaration declared(transientPool, **renderer, commands, camera, view,
                                          /*poolingEnabled=*/true);
        declared.graph().exportTexture(declared.displayColor());
        frameRecords.retain(declared.execute());
        if (options.dynamicResolution)
            resolutionController.declared((*device)->frameNumber());
        (*device)->endFrame(nullptr);

        // readback() has no synchronization; wait until the GPU releases the shared target, and
        // commitFrame() must not promote this frame's motion to "previous" before the GPU has
        // actually consumed it.
        (*device)->waitIdle();
        session.commitFrame();
        (*renderer)->drainVisibilityAfterIdle();
        (*renderer)->drainLightingAfterIdle();
        captureLighting = (*renderer)->lightingStatus();
        for (const auto& retired : (*renderer)->takeRetiredLighting()) {
            if (retired.frameNumber == captureLighting.frameNumber)
                captureLighting = retired;
            if (lightDump.is_open() && retired.checkFrame &&
                !writeLightCheckFrame(lightDump, *retired.checkFrame)) {
                LMX_LOG_ERROR("raw light-check evidence write failed");
                return 1;
            }
        }
        if (const auto failure = lightingFailure(captureLighting); !failure.empty()) {
            if (sequence)
                writeManifest(*sequence, (*device)->deviceName(), (*renderer)->displayDomain(),
                              hasCameraTrack, records, false, failure);
            LMX_LOG_ERROR("capture refused: {}", failure);
            return 1;
        }
        captureVisibility = (*renderer)->visibilityStatus();
        for (const auto& retired : (*renderer)->takeRetiredVisibility()) {
            if (retired.frameNumber == captureVisibility.frameNumber)
                captureVisibility = retired;
        }
        if (const auto failure = visibilityFailure(captureVisibility); !failure.empty()) {
            if (sequence)
                writeManifest(*sequence, (*device)->deviceName(), (*renderer)->displayDomain(),
                              hasCameraTrack, records, false, failure);
            LMX_LOG_ERROR("capture refused: {}", failure);
            return 1;
        }
        if (sequence) {
            const auto status = (*renderer)->temporalStatus();
            if (temporal == TemporalMode::Vendor &&
                status.vendorFallback != render::VendorFallback::None) {
                const std::string failure =
                    std::format("frame {}: requested metalfx, effective taa, fallback {}", frame,
                                status.vendorFallback == render::VendorFallback::Unsupported
                                    ? "unsupported"
                                    : "creation-failed");
                if (!writeManifest(*sequence, (*device)->deviceName(), (*renderer)->displayDomain(),
                                   hasCameraTrack, records, false, failure)) {
                    LMX_LOG_ERROR("capture could not write fallback metadata");
                }
                LMX_LOG_ERROR("capture sequence refused vendor fallback: {}", failure);
                return 1;
            }
            if (frame >= sequence->warmup) {
                const uint32_t ordinal = frame - sequence->warmup;
                const std::string filename = std::format(
                    "frame-{:06}.{}", ordinal, captureFormatName(sequence->captureFormat));
                std::vector<uint8_t> pixels(size_t{kScreenshotWidth} * kScreenshotHeight * 4);
                (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
                if (!writeCaptureImage(sequence->captureSequencePath / filename, pixels,
                                       (*renderer)->displayDomain(),
                                       captureFrameMetadataJson(
                                           sceneId, frames, frame, temporal, temporalView,
                                           view.temporal.renderScale, status,
                                           (*device)->deviceName(), visibilityEnabled, submission,
                                           labInstances, classifyMode, classifyCheck,
                                           &captureVisibility, &options, &captureLighting))) {
                    return 1;
                }
                records.push_back(captureRecordJson(ordinal, frame, camera, view, status, filename,
                                                    temporal, &captureVisibility,
                                                    &captureLighting));
                if (!writeManifest(*sequence, (*device)->deviceName(), (*renderer)->displayDomain(),
                                   hasCameraTrack, records, false)) {
                    return 1;
                }
                if (options.lightDebugView != engine::LightDebugView::Missed &&
                    isFlatImage(pixels)) {
                    LMX_LOG_ERROR("capture frame {} is flat; manifest remains incomplete", ordinal);
                    return 1;
                }
            }
        }
    }
    if (lightDump.is_open()) {
        lightDump.flush();
        if (!lightDump) {
            LMX_LOG_ERROR("raw light-check evidence flush failed");
            return 1;
        }
        lightDump.close();
        if (!lightDump) {
            LMX_LOG_ERROR("raw light-check evidence close failed");
            return 1;
        }
    }
    if (sequence) {
        return writeManifest(*sequence, (*device)->deviceName(), (*renderer)->displayDomain(),
                             hasCameraTrack, records, true)
                   ? 0
                   : 1;
    }

    std::vector<uint8_t> pixels(size_t{kScreenshotWidth} * kScreenshotHeight * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    // Preserve flat output as debugging evidence before reporting liveness failure.
    if (!writeCaptureImage(outPath, pixels, (*renderer)->displayDomain(),
                           captureFrameMetadataJson(
                               sceneId, frames, frames - 1, temporal, temporalView,
                               (*renderer)->temporalStatus().renderScale,
                               (*renderer)->temporalStatus(), (*device)->deviceName(),
                               visibilityEnabled, submission, labInstances, classifyMode,
                               classifyCheck, &captureVisibility, &options, &captureLighting))) {
        return 1;
    }
    LMX_LOG_INFO("screenshot written: {} ({}x{}, {} bytes of pixels)", outPath.string(),
                 kScreenshotWidth, kScreenshotHeight, pixels.size());

    if (options.lightDebugView != engine::LightDebugView::Missed && isFlatImage(pixels)) {
        LMX_LOG_ERROR(
            "screenshot: {} is a single flat colour across the whole image -- nothing appears "
            "to have rendered (the file was still written, open it)",
            outPath.string());
        return 1;
    }

    return 0;
}

} // namespace

//======================================================================================================================
int runScreenshot(const AppOptions& options) {
    return runOffscreen(options);
}

//======================================================================================================================
int runCaptureSequence(const AppOptions& options) {
    return runOffscreen(options);
}

} // namespace lmx::app
