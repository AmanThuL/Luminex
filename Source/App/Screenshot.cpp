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
#include <fstream>
#include <iostream>
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

} // namespace

//======================================================================================================================
int runScreenshot(const std::filesystem::path& outPath, engine::SceneId sceneId, uint32_t frames,
                  bool temporal, render::TemporalDebugView temporalView) {
    auto device = rhi::createDevice();
    if (!device) {
        LMX_LOG_ERROR("createDevice failed: {}", device.error().message);
        return 1;
    }
    LMX_LOG_INFO("Metal 4 device: {}", (*device)->deviceName());

    engine::SceneLibrary library(**device);
    const engine::SceneEntry& entry = library.entry(sceneId);
    if (!entry.available) {
        std::cerr << "Error: " << entry.stableId << " assets missing; run xmake setup\n";
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
    const bool hasAnyTrack = !activeScene->animation.tracks.empty() || hasCameraTrack;

    for (uint32_t frame = 0; frame < frames; ++frame) {
        // The first frame renders at the scene's authored t = 0; later frames advance by the same
        // fixed step the editor's frame loop uses, so a warmup run matches what playback produces.
        if (frame > 0 && hasAnyTrack) {
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
        view.temporal.enabled = temporal;
        view.temporal.debugView = temporalView;

        rhi::CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, camera, view, /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);

        // readback() has no synchronization; wait until the GPU releases the shared target, and
        // commitFrame() must not promote this frame's motion to "previous" before the GPU has
        // actually consumed it.
        (*device)->waitIdle();
        activeScene->commitFrame();
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

} // namespace lmx::app
