//----------------------------------------------------------------------------------------------------------------------
/// @file Screenshot.cpp
/// @brief Implements deterministic offscreen scene capture.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Screenshot.h"

#include "App/EditorShell.h"
#include "Core/Log.h"
#include "Engine/Scene.h"
#include "Engine/SceneAnimation.h"
#include "Engine/SceneLibrary.h"
#include "RHI/RHI.h"
#include "Render/Renderer.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace lmx::app {

namespace {

// Fixed dimensions keep headless output reproducible across machines.
constexpr uint32_t kScreenshotWidth = 1280;
constexpr uint32_t kScreenshotHeight = 720;

//======================================================================================================================
void appendLittleEndian(std::vector<uint8_t>& out, uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
        out.push_back(static_cast<uint8_t>((value >> (8 * byte)) & 0xFFu));
    }
}

//======================================================================================================================
void appendLittleEndian(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

//======================================================================================================================
// A negative-height, 32-bit BI_RGB bitmap accepts top-down BGRA8 readback verbatim. Four-byte
// pixels also satisfy BMP row alignment without padding.
bool writeBmp(const std::filesystem::path& path, const std::vector<uint8_t>& bgra, uint32_t width,
              uint32_t height) {
    constexpr uint32_t kFileHeaderSize = 14;
    constexpr uint32_t kInfoHeaderSize = 40;
    constexpr uint32_t kPixelOffset = kFileHeaderSize + kInfoHeaderSize;
    // Nonzero density prevents image readers from guessing display scale.
    constexpr int32_t kPixelsPerMeter = 2835;

    const uint32_t imageSize = static_cast<uint32_t>(bgra.size());

    std::vector<uint8_t> header;
    header.reserve(kPixelOffset);
    header.push_back('B');
    header.push_back('M');
    appendLittleEndian(header, kPixelOffset + imageSize);
    appendLittleEndian(header, uint16_t{0}); // reserved1
    appendLittleEndian(header, uint16_t{0}); // reserved2
    appendLittleEndian(header, kPixelOffset);

    appendLittleEndian(header, kInfoHeaderSize);
    appendLittleEndian(header, width);
    // BMP encodes top-down rows with a negative signed height.
    appendLittleEndian(header, static_cast<uint32_t>(-static_cast<int32_t>(height)));
    appendLittleEndian(header, uint16_t{1});  // planes
    appendLittleEndian(header, uint16_t{32}); // bits per pixel
    appendLittleEndian(header, uint32_t{0});  // BI_RGB, no compression
    appendLittleEndian(header, imageSize);
    appendLittleEndian(header, static_cast<uint32_t>(kPixelsPerMeter));
    appendLittleEndian(header, static_cast<uint32_t>(kPixelsPerMeter));
    appendLittleEndian(header, uint32_t{0}); // palette colors used
    appendLittleEndian(header, uint32_t{0}); // palette colors required

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        LMX_LOG_ERROR("screenshot: cannot open '{}' for writing", path.string());
        return false;
    }
    file.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
    file.write(reinterpret_cast<const char*>(bgra.data()),
               static_cast<std::streamsize>(bgra.size()));
    file.close();
    if (!file) {
        LMX_LOG_ERROR("screenshot: failed while writing '{}'", path.string());
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
std::string jsonString(std::string_view value) {
    std::string result = "\"";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 32) {
            result += std::format("\\u{:04x}", c);
        } else {
            result += static_cast<char>(c);
        }
    }
    return result + "\"";
}

//======================================================================================================================
std::string_view captureModeName(TemporalMode mode) {
    switch (mode) {
    case TemporalMode::Off:
        return "off";
    case TemporalMode::Raw:
        return "raw";
    case TemporalMode::Taa:
        return "taa";
    case TemporalMode::Vendor:
        return "metalfx";
    }
    return "unknown";
}

//======================================================================================================================
bool writeManifest(const AppOptions& options, std::string_view device, bool cameraTrack,
                   const std::vector<std::string>& records, bool complete,
                   std::string_view failure = {}) {
    std::ofstream file(options.captureSequencePath / "manifest.json", std::ios::trunc);
    if (!file)
        return false;
    file << std::setprecision(17)
         << "{\n\"schemaVersion\":1,\"complete\":" << (complete ? "true" : "false")
         << ",\"scene\":" << jsonString(engine::sceneIdString(options.initialScene))
         << ",\"failure\":" << jsonString(failure) << ",\"device\":" << jsonString(device)
         << ",\"requestedMode\":" << jsonString(captureModeName(options.temporal))
         << ",\"width\":" << kScreenshotWidth << ",\"height\":" << kScreenshotHeight
         << ",\"fps\":60,\"warmup\":" << options.warmup << ",\"frameCount\":" << options.frames
         << ",\"renderScale\":" << options.renderScale
         << ",\"debugView\":" << static_cast<int>(options.temporalView)
         << ",\"cameraTrack\":" << (cameraTrack ? "true" : "false")
         << ",\"colorSpace\":\"sRGB LDR\",\"dynamicResolution\":false,\"frames\":[\n";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i != 0)
            file << ",\n";
        file << records[i];
    }
    file << "\n]}\n";
    file.close();
    return static_cast<bool>(file);
}

//======================================================================================================================
std::string captureRecord(uint32_t ordinal, uint32_t frame, const render::Camera& camera,
                          const render::SceneView& view, const render::TemporalStatus& status,
                          std::string_view filename, TemporalMode requested) {
    std::ostringstream out;
    out << std::setprecision(17) << "{\"ordinal\":" << ordinal << ",\"simulationFrame\":" << frame
        << ",\"timeSeconds\":" << static_cast<double>(frame) / engine::kAnimationBakeRate
        << ",\"file\":" << jsonString(filename) << ",\"camera\":{\"position\":["
        << camera.position.x << ',' << camera.position.y << ',' << camera.position.z
        << "],\"yaw\":" << camera.yaw << ",\"pitch\":" << camera.pitch
        << ",\"fovY\":" << camera.fovY << ",\"nearZ\":" << camera.nearZ
        << ",\"farZ\":" << camera.farZ << "}"
        << ",\"effectiveMode\":"
        << jsonString(requested == TemporalMode::Off ? "off"
                      : status.reconstruction == render::ReconstructionMode::VendorTemporal
                          ? "metalfx"
                      : status.reconstruction == render::ReconstructionMode::NativeTaa ? "taa"
                                                                                       : "raw")
        << ",\"fallback\":" << static_cast<int>(status.vendorFallback)
        << ",\"vendorName\":" << jsonString(status.vendorName)
        << ",\"renderWidth\":" << status.extents.renderWidth
        << ",\"renderHeight\":" << status.extents.renderHeight
        << ",\"effectiveScale\":" << status.renderScale << ",\"jitterIndex\":" << status.jitterIndex
        << ",\"jitterEnabled\":" << (view.temporal.jitterEnabled ? "true" : "false")
        << ",\"historyAge\":" << status.historyAge
        << ",\"lastResetReason\":" << jsonString(render::historyResetReasonName(status.lastReset))
        << ",\"lastResetFrame\":" << status.lastResetFrame
        << ",\"vendorReset\":" << (status.vendorReset ? "true" : "false")
        << ",\"exposureEv\":" << view.exposureEv
        << ",\"autoExposure\":" << (view.autoExposureEnabled ? "true" : "false")
        << ",\"bloom\":" << (view.bloomEnabled ? "true" : "false")
        << ",\"bloomThreshold\":" << view.bloomThreshold
        << ",\"bloomIntensity\":" << view.bloomIntensity << ",\"shadowFilter\":\"pcf\"}";
    return out.str();
}

//======================================================================================================================
int runOffscreen(const std::filesystem::path& outPath, engine::SceneId sceneId, uint32_t frames,
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

    engine::SceneLibrary library(**device);
    const engine::SceneEntry& entry = library.entry(sceneId);
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

    render::Camera camera = cameraFromScene(activeScene->initialCamera);
    const bool hasCameraTrack = !activeScene->animation.cameraTrack.empty();
    const bool hasAnyTrack = engine::hasAnimationTracks(activeScene->animation);

    if (sequence &&
        !writeManifest(*sequence, (*device)->deviceName(), hasCameraTrack, records, false)) {
        return 1;
    }
    const uint32_t totalFrames = sequence ? sequence->warmup + frames : frames;
    for (uint32_t frame = 0; frame < totalFrames; ++frame) {
        // The first frame renders at the scene's authored t = 0; later frames advance by the same
        // fixed step the editor's frame loop uses, so a warmup run matches what playback produces.
        if (sequence) {
            activeScene->animationTime = static_cast<double>(frame) / engine::kAnimationBakeRate;
            activeScene->animate(activeScene->animationTime);
        } else if (frame > 0 && hasAnyTrack) {
            activeScene->advanceAnimation(1.0 / engine::kAnimationBakeRate);
            activeScene->animate(activeScene->animationTime);
        }
        if (hasCameraTrack) {
            // On frame 0 this re-derives the pose `initialCamera` already holds (the track's first
            // key), which is redundant and deliberately harmless -- one unconditional sample is
            // clearer than a special case that must stay in step with the authored first key.
            const engine::CameraKey pose = engine::sampleCameraTrack(
                activeScene->animation.cameraTrack, activeScene->animationTime);
            camera.position = pose.position;
            camera.yaw = pose.yaw;
            camera.pitch = pose.pitch;
        }

        std::vector<render::DrawItem> items;
        render::SceneView view =
            activeScene->view(items, render::ShadowFilter::PCF, /*wireframe=*/false);
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
        (*renderer)->render(commands, camera, view, /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);

        // readback() has no synchronization; wait until the GPU releases the shared target, and
        // commitFrame() must not promote this frame's motion to "previous" before the GPU has
        // actually consumed it.
        (*device)->waitIdle();
        activeScene->commitFrame();
        if (sequence) {
            const auto status = (*renderer)->temporalStatus();
            if (temporal == TemporalMode::Vendor &&
                status.vendorFallback != render::VendorFallback::None) {
                const std::string failure =
                    std::format("frame {}: requested metalfx, effective taa, fallback {}", frame,
                                status.vendorFallback == render::VendorFallback::Unsupported
                                    ? "unsupported"
                                    : "creation-failed");
                if (!writeManifest(*sequence, (*device)->deviceName(), hasCameraTrack, records,
                                   false, failure)) {
                    LMX_LOG_ERROR("capture could not write fallback metadata");
                }
                LMX_LOG_ERROR("capture sequence refused vendor fallback: {}", failure);
                return 1;
            }
            if (frame >= sequence->warmup) {
                const uint32_t ordinal = frame - sequence->warmup;
                const std::string filename = std::format("frame-{:06}.bmp", ordinal);
                std::vector<uint8_t> pixels(size_t{kScreenshotWidth} * kScreenshotHeight * 4);
                (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
                if (!writeBmp(sequence->captureSequencePath / filename, pixels, kScreenshotWidth,
                              kScreenshotHeight)) {
                    return 1;
                }
                records.push_back(
                    captureRecord(ordinal, frame, camera, view, status, filename, temporal));
                if (!writeManifest(*sequence, (*device)->deviceName(), hasCameraTrack, records,
                                   false)) {
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
        return writeManifest(*sequence, (*device)->deviceName(), hasCameraTrack, records, true) ? 0
                                                                                                : 1;
    }

    std::vector<uint8_t> pixels(size_t{kScreenshotWidth} * kScreenshotHeight * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    // Preserve flat output as debugging evidence before reporting liveness failure.
    if (!writeBmp(outPath, pixels, kScreenshotWidth, kScreenshotHeight)) {
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
int runScreenshot(const std::filesystem::path& outPath, engine::SceneId sceneId, uint32_t frames,
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
