#include "App/Screenshot.h"

#include "App/EditorShell.h"
#include "Core/Log.h"
#include "RHI/RHI.h"
#include "Render/Renderer.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace lmx::app {

namespace {

// The offscreen render target, in pixels. Fixed rather than derived from a window so the README
// image is the same on every machine.
constexpr uint32_t kScreenshotWidth = 1280;
constexpr uint32_t kScreenshotHeight = 720;

void appendLittleEndian(std::vector<uint8_t>& out, uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
        out.push_back(static_cast<uint8_t>((value >> (8 * byte)) & 0xFFu));
    }
}

void appendLittleEndian(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

// Uncompressed 32-bit BMP: a 14-byte BITMAPFILEHEADER, a 40-byte BITMAPINFOHEADER, then the
// readback bytes verbatim. Two facts make the pixel copy verbatim rather than a conversion:
// BI_RGB at 32bpp stores each pixel as B,G,R,A -- exactly our BGRA8Unorm readback -- and a
// *negative* biHeight declares top-down rows, which is the order readback() produces. At 4 bytes
// per pixel every row is already a multiple of 4 bytes, so there is no row padding to insert.
//
// Hand-rolled because the alternative is an image library dependency for one write of the
// simplest container in existence.
bool writeBmp(const std::filesystem::path& path, const std::vector<uint8_t>& bgra, uint32_t width,
              uint32_t height) {
    constexpr uint32_t kFileHeaderSize = 14;
    constexpr uint32_t kInfoHeaderSize = 40;
    constexpr uint32_t kPixelOffset = kFileHeaderSize + kInfoHeaderSize;
    // 2835 px/m == 72 dpi. Not meaningful for a screenshot, but zeroes make some readers guess.
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
    // Negative height == top-down. static_cast of a negative value to uint32_t is the two's
    // complement bit pattern, which is exactly what the format wants.
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

// One BGRA8Unorm texel, in the channel order readback() produces.
struct Pixel {
    uint8_t b = 0, g = 0, r = 0, a = 0;
};

// Logs one pixel in memory order (B,G,R,A) -- the same order the BMP stores -- so a script can
// check the file it just wrote against what the GPU actually produced. Out-of-range coordinates
// are reported rather than read (the probes below are derived from the camera, and a derivation
// that drifted off the image should say so instead of reading past the buffer).
bool probePixel(const char* what, const std::vector<uint8_t>& bgra, uint32_t width, uint32_t x,
                uint32_t y, Pixel& out) {
    const size_t offset = (size_t{y} * width + x) * 4;
    if (offset + 3 >= bgra.size()) {
        LMX_LOG_ERROR("screenshot probe {} at ({},{}) is outside the image", what, x, y);
        return false;
    }
    out = {bgra[offset], bgra[offset + 1], bgra[offset + 2], bgra[offset + 3]};
    LMX_LOG_INFO("screenshot probe {} at ({},{}): B={} G={} R={} A={}", what, x, y, out.b, out.g,
                 out.r, out.a);
    return true;
}

// BGRA8Unorm quantises a float channel by rounding, so the clear colour lands within a ulp or two
// of this. Tolerance 2, not 0, because the exact rounding of the conversion is not contractual --
// the same rule the GPU tests use.
bool isClearChannel(uint8_t actual, float expected) {
    const int want = static_cast<int>(expected * 255.0f + 0.5f);
    return std::abs(int{actual} - want) <= 2;
}

} // namespace

int runScreenshot(const std::filesystem::path& outPath) {
    auto device = rhi::createDevice();
    if (!device) {
        LMX_LOG_ERROR("createDevice failed: {}", device.error().message);
        return 1;
    }
    LMX_LOG_INFO("Metal 4 device: {}", (*device)->deviceName());

    auto meshes = createSceneMeshes(**device);
    if (!meshes) {
        LMX_LOG_ERROR("createSceneMeshes failed: {}", meshes.error().message);
        return 1;
    }
    const std::vector<SceneObject> scene = makeDefaultScene(meshes->cube, meshes->plane);

    // cpuReadback puts the Renderer's colour target in shared storage so readback() is a memcpy
    // rather than a blit. The editor leaves it false -- it hands the target to a second pass.
    auto renderer = render::Renderer::create(**device, kScreenshotWidth, kScreenshotHeight,
                                             /*cpuReadback=*/true);
    if (!renderer) {
        LMX_LOG_ERROR("Renderer::create failed: {}", renderer.error().message);
        return 1;
    }

    // t = 0: the spinning cube is at its rest orientation, so the image is reproducible.
    std::vector<render::DrawItem> items;
    makeDrawItems(scene, 0.0f, items);

    rhi::CommandList& commands = (*device)->beginFrame();
    // barrierForSampling false, and load-bearing: there is no UI pass in this frame, and a
    // barrier no later pass consumes is a dropped dependency edge the RHI aborts on at endFrame
    // (plan Amendment A4).
    (*renderer)->render(commands, makeDefaultCamera(), items, /*barrierForSampling=*/false);
    // nullptr: nothing to present, this frame only fills a texture.
    (*device)->endFrame(nullptr);

    // readback() copies out of shared storage with no synchronisation of its own, so the frame
    // has to be off the GPU before the copy -- otherwise the "screenshot" is whatever the
    // allocation happened to contain.
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kScreenshotWidth} * kScreenshotHeight * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    // Enough to tell "the scene rendered" from "the clear worked and nothing else did" without
    // opening the image. The corner is sky: the camera's pitch puts the horizon just under
    // halfway up the frame, so row 0 can only be the clear colour. The centre is the gold cube --
    // the default pose frames it there, and gold is the only red-dominant thing on the axis.
    Pixel corner;
    Pixel center;
    if (!probePixel("background", pixels, kScreenshotWidth, 0, 0, corner) ||
        !probePixel("gold cube", pixels, kScreenshotWidth, kScreenshotWidth / 2,
                    kScreenshotHeight / 2, center)) {
        return 1;
    }
    const float* clear = (*renderer)->clearColor;
    if (!isClearChannel(corner.b, clear[2]) || !isClearChannel(corner.g, clear[1]) ||
        !isClearChannel(corner.r, clear[0]) || corner.a != 255) {
        LMX_LOG_ERROR("screenshot: the background probe is not the clear colour ({}, {}, {})",
                      clear[0], clear[1], clear[2]);
        return 1;
    }
    if (center.r < 64 || center.r <= center.b || center.r <= center.g) {
        LMX_LOG_ERROR("screenshot: the centre probe is not a lit gold cube");
        return 1;
    }

    if (!writeBmp(outPath, pixels, kScreenshotWidth, kScreenshotHeight)) {
        return 1;
    }
    LMX_LOG_INFO("screenshot written: {} ({}x{}, {} bytes of pixels)", outPath.string(),
                 kScreenshotWidth, kScreenshotHeight, pixels.size());
    return 0;
}

} // namespace lmx::app
