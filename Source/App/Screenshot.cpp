//----------------------------------------------------------------------------------------------------------------------
/// @file Screenshot.cpp
/// @brief Implements deterministic offscreen scene capture.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Screenshot.h"

#include "App/Model/CaptureMetadata.h"
#include "App/Model/FrameDeclaration.h"
#include "App/Model/SceneDefaults.h"
#include "App/Model/SceneSession.h"
#include "Asset/BmpImage.h"
#include "Asset/PngImage.h"
#include "Core/Log.h"
#include "RHI/RHI.h"
#include "Render/Renderer.h"
#include "Scene/Scene.h"
#include "Scene/SceneLibrary.h"

#include <algorithm>
#include <array>
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
int runOffscreen(const std::filesystem::path& outPath, scene::SceneId sceneId, uint32_t frames,
                 TemporalMode temporal, render::TemporalDebugView temporalView, float renderScale,
                 const AppOptions* sequence) {
    std::vector<std::string> records;
    if (sequence) {
        std::error_code error;
        std::filesystem::create_directories(sequence->captureSequencePath, error);
        if (error || !std::filesystem::is_empty(sequence->captureSequencePath, error) || error) {
            LMX_LOG_ERROR("capture directory must be new or empty: {}",
                          sequence->captureSequencePath.string());
            return 1;
        }
    }
    auto device = rhi::createDevice();
    if (!device) {
        LMX_LOG_ERROR("createDevice failed: {}", device.error().message);
        return 1;
    }
    LMX_LOG_INFO("Metal 4 device: {}", (*device)->deviceName());

    scene::SceneLibrary library(**device);
    const scene::SceneEntry& entry = library.entry(sceneId);
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
    scene::Scene* activeScene = *scene;
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
    const render::Camera& camera = session.camera();
    const bool hasCameraTrack = !activeScene->animation.cameraTrack.empty();
    FrameRecordRing frameRecords;

    if (sequence && !writeManifest(*sequence, (*device)->deviceName(), (*renderer)->displayDomain(),
                                   hasCameraTrack, records, false)) {
        return 1;
    }
    const uint32_t totalFrames = sequence ? sequence->warmup + frames : frames;
    for (uint32_t frame = 0; frame < totalFrames; ++frame) {
        if (sequence) {
            session.prepareSequenceFrame(frame);
        } else {
            session.prepareScreenshotFrame(frame);
        }

        std::vector<render::DrawItem> items;
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
        view.temporal.enabled = temporal != TemporalMode::Off;
        view.temporal.jitterEnabled = temporal != TemporalMode::Off;
        view.temporal.reconstruction = temporalReconstructionMode(temporal);
        view.temporal.debugView = temporalView;
        view.temporal.renderScale = renderScale;

        rhi::CommandList& commands = (*device)->beginFrame();
        FrameDeclaration declared(transientPool, **renderer, commands, camera, view,
                                  /*poolingEnabled=*/true);
        declared.graph().exportTexture(declared.displayColor());
        declared.execute(frameRecords);
        (*device)->endFrame(nullptr);

        // readback() has no synchronization; wait until the GPU releases the shared target, and
        // commitFrame() must not promote this frame's motion to "previous" before the GPU has
        // actually consumed it.
        (*device)->waitIdle();
        session.commitFrame();
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
                                       captureFrameMetadataJson(sceneId, frames, frame, temporal,
                                                                temporalView, renderScale, status,
                                                                (*device)->deviceName()))) {
                    return 1;
                }
                records.push_back(
                    captureRecordJson(ordinal, frame, camera, view, status, filename, temporal));
                if (!writeManifest(*sequence, (*device)->deviceName(), (*renderer)->displayDomain(),
                                   hasCameraTrack, records, false)) {
                    return 1;
                }
                if (isFlatImage(pixels)) {
                    LMX_LOG_ERROR("capture frame {} is flat; manifest remains incomplete", ordinal);
                    return 1;
                }
            }
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
                               sceneId, frames, frames - 1, temporal, temporalView, renderScale,
                               (*renderer)->temporalStatus(), (*device)->deviceName()))) {
        return 1;
    }
    LMX_LOG_INFO("screenshot written: {} ({}x{}, {} bytes of pixels)", outPath.string(),
                 kScreenshotWidth, kScreenshotHeight, pixels.size());

    if (isFlatImage(pixels)) {
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
int runScreenshot(const std::filesystem::path& outPath, scene::SceneId sceneId, uint32_t frames,
                  TemporalMode temporal, render::TemporalDebugView temporalView,
                  float renderScale) {
    return runOffscreen(outPath, sceneId, frames, temporal, temporalView, renderScale, nullptr);
}

//======================================================================================================================
int runCaptureSequence(const AppOptions& options) {
    return runOffscreen({}, options.initialScene, options.frames, options.temporal,
                        options.temporalView, options.renderScale, &options);
}

} // namespace lmx::app
