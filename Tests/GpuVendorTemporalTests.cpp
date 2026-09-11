#include "GpuTestSupport.h"

#include "Engine/GeometryGenerator.h"
#include "Render/Temporal.h"
#include "Render/TemporalHistory.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace {

using lmx::render::Camera;
using lmx::render::DrawItem;
using lmx::render::HistoryResetReason;
using lmx::render::Mesh;
using lmx::render::ReconstructionMode;
using lmx::render::Renderer;
using lmx::render::SceneView;
using lmx::render::TemporalDebugView;

constexpr uint32_t kScenarioWidth = 320;
constexpr uint32_t kScenarioHeight = 180;
constexpr size_t kScenarioPixels = size_t{kScenarioWidth} * kScenarioHeight;
constexpr uint32_t kMeasureFirstFrame = 17;

//======================================================================================================================
// The motion target is RG16Float, so a readback carries IEEE halves. Widening them here keeps the
// oracle comparisons in the same units Source/Render/Temporal.h states motion in.
float halfToFloat(uint16_t bits) {
    const uint32_t sign = uint32_t{bits & 0x8000u} << 16;
    const uint32_t exponent = (bits >> 10) & 0x1Fu;
    const uint32_t mantissa = bits & 0x3FFu;
    uint32_t wide = 0;
    if (exponent == 0) {
        // Subnormal halves are normalised by hand; zero falls out of the loop untouched.
        if (mantissa != 0) {
            uint32_t value = mantissa;
            uint32_t shift = 0;
            while ((value & 0x400u) == 0) {
                value <<= 1;
                ++shift;
            }
            wide = ((127 - 15 - shift) << 23) | ((value & 0x3FFu) << 13);
        }
    } else if (exponent == 0x1Fu) {
        wide = 0x7F800000u | (mantissa << 13);
    } else {
        wide = ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    wide |= sign;
    float result = 0.0f;
    std::memcpy(&result, &wide, sizeof(result));
    return result;
}

//======================================================================================================================
// A large plane rotated to face a camera looking down -Z, so every probe texel lands on one known
// surface at one known depth.
glm::mat4 facingPlaneModel(float z) {
    return glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, z}) *
           glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
}

//======================================================================================================================
// One declared and executed frame through the renderer's own graph, which is the path --screenshot
// and the GPU tests take.
void renderFrame(lmx::rhi::Device& device, Renderer& renderer, const Camera& camera,
                 const SceneView& view) {
    lmx::rhi::CommandList& commands = device.beginFrame();
    renderer.render(commands, camera, view, /*barrierForSampling=*/false);
    device.endFrame(nullptr);
    device.waitIdle();
}

//======================================================================================================================
// The same frame without the drain: exactly what App/main.cpp submits every frame, so a temporal
// resource a frame still in flight holds is left held rather than quietly retired by a waitIdle
// the shipped loop never performs.
void renderFrameInFlight(lmx::rhi::Device& device, Renderer& renderer, const Camera& camera,
                         const SceneView& view) {
    lmx::rhi::CommandList& commands = device.beginFrame();
    renderer.render(commands, camera, view, /*barrierForSampling=*/false);
    device.endFrame(nullptr);
}

//======================================================================================================================
// One RGBA16Float target, widened to floats. Both the colour history and the raw scene target are
// half-precision, so a comparison between them has to speak the same units the shader wrote.
std::vector<glm::vec4> readHalf4(lmx::rhi::Texture& texture) {
    const size_t count = size_t{texture.width()} * texture.height();
    std::vector<uint8_t> bytes(count * 8);
    texture.readback(bytes.data(), bytes.size());
    std::vector<glm::vec4> pixels(count);
    for (size_t i = 0; i < pixels.size(); ++i) {
        uint16_t texel[4] = {0, 0, 0, 0};
        std::memcpy(texel, bytes.data() + i * 8, sizeof(texel));
        pixels[i] = {halfToFloat(texel[0]), halfToFloat(texel[1]), halfToFloat(texel[2]),
                     halfToFloat(texel[3])};
    }
    return pixels;
}

//======================================================================================================================
// Rec. 709 luminance of a pre-exposed linear value, which is what every scenario metric is stated
// in.
float luminance(const glm::vec4& color) {
    return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

//======================================================================================================================
std::vector<float> luminances(const std::vector<glm::vec4>& image) {
    std::vector<float> result(image.size());
    for (size_t i = 0; i < image.size(); ++i) {
        result[i] = luminance(image[i]);
    }
    return result;
}

//======================================================================================================================
// Mean |a - b| over the named pixels. An empty pixel set is a scenario that failed to find the
// geometry it meant to measure, which is a test bug rather than a passing measurement.
float meanAbsDiff(const std::vector<float>& a, const std::vector<float>& b,
                  const std::vector<uint32_t>& pixels) {
    REQUIRE_FALSE(pixels.empty());
    double sum = 0.0;
    for (uint32_t index : pixels) {
        sum += std::abs(double{a[index]} - double{b[index]});
    }
    return static_cast<float>(sum / static_cast<double>(pixels.size()));
}

//======================================================================================================================
float meanOver(const std::vector<float>& values, const std::vector<uint32_t>& pixels) {
    REQUIRE_FALSE(pixels.empty());
    double sum = 0.0;
    for (uint32_t index : pixels) {
        sum += values[index];
    }
    return static_cast<float>(sum / static_cast<double>(pixels.size()));
}

//======================================================================================================================
std::vector<uint32_t> allPixels() {
    std::vector<uint32_t> pixels(kScenarioPixels);
    for (uint32_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = i;
    }
    return pixels;
}

// What one declared scenario frame left behind.
struct ScenarioFrame {
    std::vector<glm::vec4> history; ///< This frame's colour slot: the mode's own output.
    std::vector<glm::vec4> raw;     ///< The jittered scene colour the frame rasterised.
    std::vector<uint8_t> display;   ///< The display target, BGRA8, after any debug view.
    lmx::render::TemporalStatus status;
};

// Mutates the frame's scene before it is declared: the caller owns the draw list, so an item's
// model and previousModel are set here rather than rebuilt.
using PerFrame = std::function<void(uint32_t frame, SceneView& view, Camera& camera)>;

//======================================================================================================================
// `frames` declared temporal frames through the shipped path, all in one reconstruction mode, each
// read back. Frame numbering is one-based, so `frame` 17 is the first the tolerances measure.
//
// The render scale is not one of the fixed fields: `base.temporal.renderScale` carries a whole
// sequence's scale and a PerFrame may set `view.temporal.renderScale` to change it per frame,
// which is what the scale-change scenarios drive the extent with.
std::vector<ScenarioFrame> renderSequence(lmx::rhi::Device& device, Renderer& renderer,
                                          uint32_t frames, ReconstructionMode mode,
                                          const SceneView& base, const Camera& baseCamera,
                                          TemporalDebugView debugView, const PerFrame& perFrame) {
    std::vector<ScenarioFrame> result;
    result.reserve(frames);
    for (uint32_t frame = 1; frame <= frames; ++frame) {
        SceneView view = base;
        Camera camera = baseCamera;
        view.temporal.enabled = true;
        view.temporal.jitterEnabled = true;
        view.temporal.reconstruction = mode;
        view.temporal.debugView = debugView;
        if (perFrame) {
            perFrame(frame, view, camera);
        }
        renderFrame(device, renderer, camera, view);

        ScenarioFrame record;
        record.history = readHalf4(*renderer.historyTarget());
        record.raw = readHalf4(renderer.hdrColorTarget());
        record.display.resize(kScenarioPixels * 4);
        renderer.colorTarget().readback(record.display.data(), record.display.size());
        record.status = renderer.temporalStatus();
        result.push_back(std::move(record));
    }
    return result;
}

//======================================================================================================================
// The mean frame-to-frame |dY| of a sequence's own output over the measured window, which is what
// "stability" is measured as: a still scene whose picture keeps changing is one that shimmers.
float frameToFrameChange(const std::vector<ScenarioFrame>& frames,
                         const std::vector<uint32_t>& pixels) {
    double sum = 0.0;
    uint32_t counted = 0;
    for (size_t i = kMeasureFirstFrame; i < frames.size(); ++i) {
        sum +=
            meanAbsDiff(luminances(frames[i].history), luminances(frames[i - 1].history), pixels);
        ++counted;
    }
    REQUIRE(counted > 0);
    return static_cast<float>(sum / counted);
}

//======================================================================================================================
// Horizontal pixels one world unit spans at view distance `distance`, which is what turns "four
// pixels per frame" or "two pixels wide" into the world quantity a scenario has to author.
float pixelsPerUnit(const Camera& camera, float distance) {
    const float aspect = static_cast<float>(kScenarioWidth) / static_cast<float>(kScenarioHeight);
    return 0.5f * static_cast<float>(kScenarioWidth) /
           (distance * std::tan(camera.fovY * 0.5f) * aspect);
}

//======================================================================================================================
// The pixel column a world x lands in, for a camera at the origin looking down -Z.
float pixelForWorldX(const Camera& camera, float worldX, float distance) {
    return 0.5f * static_cast<float>(kScenarioWidth) + worldX * pixelsPerUnit(camera, distance);
}

//======================================================================================================================
Camera scenarioCamera(glm::vec3 position, float pitch) {
    Camera camera;
    camera.position = position;
    camera.pitch = pitch;
    return camera;
}

//======================================================================================================================
SceneView scenarioSceneView(std::span<const DrawItem> items, glm::vec3 lightDirection) {
    SceneView view;
    view.items = items;
    view.lights[0] = {.strength = {1.0f, 1.0f, 1.0f}, .direction = lightDirection};
    view.lights[1].strength = {0.0f, 0.0f, 0.0f};
    view.lights[2].strength = {0.0f, 0.0f, 0.0f};
    view.boundingSphere = {0.0f, 0.0f, -20.0f, 60.0f};
    // Bloom composites a blurred copy of whatever the display reads, which would mix the two modes'
    // outputs into a comparison meant to be about the accumulation alone.
    view.bloomEnabled = false;
    return view;
}

//======================================================================================================================
glm::mat4 boxModel(glm::vec3 center, glm::vec3 scale) {
    return glm::translate(glm::mat4{1.0f}, center) * glm::scale(glm::mat4{1.0f}, scale);
}

//======================================================================================================================
// A draw that did not move: previousModel matched to model, which is what makes the item reproject
// onto itself. Leaving it at the default would have every static object report the motion of a
// teleport from the identity transform.
DrawItem staticItem(const Mesh& mesh, const glm::mat4& model, glm::vec4 albedo) {
    DrawItem item;
    item.mesh = &mesh;
    item.model = model;
    item.previousModel = model;
    item.material.albedo = albedo;
    return item;
}

//======================================================================================================================
// A receding checkerboard: the bright tiles only, laid over a dark floor plane, so the high spatial
// frequency near the horizon is what jitter makes shimmer and accumulation is meant to settle.
void appendCheckerFloor(std::vector<DrawItem>& items, const Mesh& cube, const Mesh& plane) {
    items.push_back(staticItem(plane, glm::mat4{1.0f}, {0.04f, 0.04f, 0.04f, 1.0f}));
    for (int32_t ix = -4; ix < 4; ++ix) {
        for (int32_t iz = -60; iz < 4; ++iz) {
            if (((ix + iz) & 1) != 0) {
                continue;
            }
            items.push_back(staticItem(
                cube,
                boxModel({static_cast<float>(ix) + 0.5f, 0.01f, static_cast<float>(iz) + 0.5f},
                         {1.0f, 0.02f, 1.0f}),
                {0.8f, 0.8f, 0.8f, 1.0f}));
        }
    }
}

//======================================================================================================================
// A receding checkerboard
// whose perspective compresses its high spatial frequency toward the horizon, and five poles two
// *output* pixels wide -- the width is stated in output pixels at every render scale, because the
// output extent is what the reconstruction has to resolve them at.
std::vector<DrawItem> checkerAndPoleItems(const Camera& camera, const Mesh& cube,
                                          const Mesh& plane) {
    std::vector<DrawItem> items;
    appendCheckerFloor(items, cube, plane);
    const float poleWidth = 2.0f / pixelsPerUnit(camera, 12.0f);
    for (int32_t i = -2; i <= 2; ++i) {
        items.push_back(staticItem(
            cube,
            boxModel({static_cast<float>(i) * 1.5f, 1.5f, -8.0f}, {poleWidth, 3.0f, poleWidth}),
            {1.0f, 1.0f, 1.0f, 1.0f}));
    }
    return items;
}

// The moving-quad scene's geometry, shared by the ghosting scenarios so the region one of them
// measures is the region the other one does. A dark quad translates across a bright backdrop six
// units behind it, which is the depth gap the disocclusion test has to see in the band it vacates.
constexpr float kQuadDistance = 10.0f; // camera z 4 to quad z -6
constexpr float kQuadHalfWidth = 1.0f;
constexpr float kQuadStartX = -3.0f;
constexpr glm::vec3 kQuadScale{2.0f, 2.0f, 0.1f};

//======================================================================================================================
Camera movingQuadCamera() {
    return scenarioCamera({0.0f, 0.0f, 4.0f}, 0.0f);
}

//======================================================================================================================
// Four *output* pixels of translation a frame, whatever the render extent: the scenario is stated
// in the extent the picture is presented at.
float movingQuadCenterX(const Camera& camera, uint32_t frame) {
    const float stepX = 4.0f / pixelsPerUnit(camera, kQuadDistance);
    return kQuadStartX + stepX * static_cast<float>(frame - 1);
}

//======================================================================================================================
glm::mat4 movingQuadModel(const Camera& camera, uint32_t frame) {
    return boxModel({movingQuadCenterX(camera, frame), 0.0f, -6.0f}, kQuadScale);
}

//======================================================================================================================
// The quad's screen span on `frame`, from the same projection the scene was authored against.
std::pair<int32_t, int32_t> movingQuadColumns(const Camera& camera, uint32_t frame) {
    const float center = movingQuadCenterX(camera, frame);
    return {static_cast<int32_t>(
                std::ceil(pixelForWorldX(camera, center - kQuadHalfWidth, kQuadDistance))),
            static_cast<int32_t>(
                std::floor(pixelForWorldX(camera, center + kQuadHalfWidth, kQuadDistance)))};
}

//======================================================================================================================
// The backdrop band the quad vacated over the last two frames, well inside its top and bottom edges
// so the measured pixels are uncovered backdrop rather than the quad's own silhouette.
std::vector<uint32_t> movingQuadVacated(const Camera& camera, uint32_t frame) {
    const auto [currentLeft, currentRight] = movingQuadColumns(camera, frame);
    const auto [olderLeft, olderRight] = movingQuadColumns(camera, frame - 2);
    const float quadHalfHeightPixels = kQuadHalfWidth * pixelsPerUnit(camera, kQuadDistance);
    const int32_t rowFirst =
        static_cast<int32_t>(0.5f * kScenarioHeight - quadHalfHeightPixels) + 2;
    const int32_t rowLast = static_cast<int32_t>(0.5f * kScenarioHeight + quadHalfHeightPixels) - 2;
    std::vector<uint32_t> pixels;
    for (int32_t row = rowFirst; row <= rowLast; ++row) {
        for (int32_t column = olderLeft; column < currentLeft; ++column) {
            if (column < 0 || column >= static_cast<int32_t>(kScenarioWidth)) {
                continue;
            }
            pixels.push_back(static_cast<uint32_t>(row * kScenarioWidth + column));
        }
    }
    return pixels;
}

//======================================================================================================================
// The quad's luminance beside the backdrop's on one frame, read out of the picture rather than
// derived from the material: it is the contrast the ghosting tolerance is stated as a share of.
float movingQuadContrast(const Camera& camera, const std::vector<float>& image, uint32_t frame) {
    const auto [left, right] = movingQuadColumns(camera, frame);
    const auto quadSample =
        static_cast<uint32_t>((kScenarioHeight / 2) * kScenarioWidth + (left + right) / 2);
    const auto wallSample = static_cast<uint32_t>((kScenarioHeight / 2) * kScenarioWidth + 4);
    return std::abs(image[quadSample] - image[wallSample]);
}

//======================================================================================================================
// The moving-quad scene, with the quad as item 1 so a PerFrame can drive it.
std::vector<DrawItem> movingQuadItems(const Camera& camera, const Mesh& cube, const Mesh& plane) {
    std::vector<DrawItem> items;
    items.push_back(staticItem(plane, facingPlaneModel(-12.0f), {0.6f, 0.6f, 0.6f, 1.0f}));
    items.push_back(staticItem(cube, movingQuadModel(camera, 1), {0.05f, 0.05f, 0.05f, 1.0f}));
    return items;
}

} // namespace

//======================================================================================================================
TEST_CASE("vendor reconstruction settles a static jittered frame", "[gpu][temporal][vendor]") {
    using namespace lmx::rhi;
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    if (!(*device)->capabilities().temporalScaler.available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.vendorCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.vendorFloor");
    REQUIRE(plane.has_value());
    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    const std::vector<DrawItem> items = checkerAndPoleItems(camera, *cube, *plane);
    SceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});
    for (float scale : {0.5f, 1.0f}) {
        CAPTURE(scale);
        base.temporal.renderScale = scale;
        const auto sequence = [&](ReconstructionMode mode) {
            auto renderer = Renderer::create(**device, kScenarioWidth, kScenarioHeight, true);
            REQUIRE(renderer.has_value());
            return renderSequence(**device, **renderer, 32, mode, base, camera,
                                  TemporalDebugView::Off, nullptr);
        };
        const auto spatial = sequence(ReconstructionMode::Raw);
        const auto vendor = sequence(ReconstructionMode::VendorTemporal);
        REQUIRE(vendor.back().status.reconstruction == ReconstructionMode::VendorTemporal);
        const float rawChange = frameToFrameChange(spatial, allPixels());
        const float vendorChange = frameToFrameChange(vendor, allPixels());
        std::printf("vendor static scale %.2f: raw dY %.9f, vendor dY %.9f, ratio %.9f\n", scale,
                    rawChange, vendorChange, vendorChange / rawChange);
        REQUIRE(rawChange > 0.0f);
        CHECK(vendorChange <= 0.5f * rawChange);
    }
}

//======================================================================================================================
TEST_CASE("vendor reconstruction fills the display extent", "[gpu][temporal][vendor]") {
    using namespace lmx::rhi;
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    if (!(*device)->capabilities().temporalScaler.available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(32.0f), "lmx.test.vendorCoverage");
    REQUIRE(plane.has_value());
    const Camera camera;
    auto empty = Renderer::create(**device, kScenarioWidth, kScenarioHeight, true);
    REQUIRE(empty.has_value());
    renderFrame(**device, **empty, camera, scenarioSceneView({}, {0.0f, 0.0f, -1.0f}));
    std::vector<uint8_t> background(kScenarioPixels * 4);
    (*empty)->colorTarget().readback(background.data(), background.size());
    const std::array<DrawItem, 1> items = {
        staticItem(*plane, facingPlaneModel(-2.0f), {0.6f, 0.6f, 0.6f, 1.0f})};
    for (float scale : {0.5f, 1.0f}) {
        CAPTURE(scale);
        SceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
        base.temporal.renderScale = scale;
        auto renderer = Renderer::create(**device, kScenarioWidth, kScenarioHeight, true);
        REQUIRE(renderer.has_value());
        const auto frames =
            renderSequence(**device, **renderer, 32, ReconstructionMode::VendorTemporal, base,
                           camera, TemporalDebugView::Off, nullptr);
        uint32_t clearPixels = 0;
        for (size_t pixel = 0; pixel < kScenarioPixels; ++pixel) {
            const auto& display = frames.back().display;
            if (display[pixel * 4] == background[0] && display[pixel * 4 + 1] == background[1] &&
                display[pixel * 4 + 2] == background[2]) {
                ++clearPixels;
            }
        }
        std::printf("vendor coverage scale %.2f: clear pixels %u\n", scale, clearPixels);
        CHECK(clearPixels == 0);
    }
}

//======================================================================================================================
TEST_CASE("a vendor reconstructed moving quad leaves no trail", "[gpu][temporal][vendor]") {
    using namespace lmx::rhi;
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    if (!(*device)->capabilities().temporalScaler.available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.vendorCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(32.0f), "lmx.test.vendorWall");
    REQUIRE(plane.has_value());
    const Camera camera = movingQuadCamera();
    std::vector<DrawItem> items = movingQuadItems(camera, *cube, *plane);
    SceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
    base.temporal.renderScale = 0.5f;
    const PerFrame animate = [&items, &camera](uint32_t frame, SceneView&, Camera&) {
        items[1].model = movingQuadModel(camera, frame);
        items[1].previousModel = movingQuadModel(camera, frame == 1 ? 1 : frame - 1);
    };
    constexpr uint32_t kFrames = 28;
    const auto sequence = [&](ReconstructionMode mode) {
        auto renderer = Renderer::create(**device, kScenarioWidth, kScenarioHeight, true);
        REQUIRE(renderer.has_value());
        return renderSequence(**device, **renderer, kFrames, mode, base, camera,
                              TemporalDebugView::Off, animate);
    };
    const auto spatial = sequence(ReconstructionMode::Raw);
    const auto vendor = sequence(ReconstructionMode::VendorTemporal);
    const auto spatialY = luminances(spatial.back().history);
    const float contrast = movingQuadContrast(camera, spatialY, kFrames);
    const auto vacated = movingQuadVacated(camera, kFrames);
    REQUIRE(vacated.size() > 20);
    REQUIRE(contrast > 0.05f);
    const float trail = meanAbsDiff(luminances(vendor.back().history), spatialY, vacated);
    std::printf("vendor moving quad: trail %.9f, contrast %.9f, ratio %.9f\n", trail, contrast,
                trail / contrast);
    CHECK(trail <= 0.25f * contrast);
}

//======================================================================================================================
TEST_CASE("native and vendor reconstruction retain separate valid histories",
          "[gpu][temporal][vendor]") {
    using namespace lmx::rhi;
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    if (!(*device)->capabilities().temporalScaler.available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto plane = lmx::render::createMesh(**device, lmx::render::makePlane(32.0f),
                                         "lmx.test.vendorSwitchPlane");
    REQUIRE(plane.has_value());
    const Camera camera;
    auto empty = Renderer::create(**device, kScenarioWidth, kScenarioHeight, true);
    REQUIRE(empty.has_value());
    renderFrame(**device, **empty, camera, scenarioSceneView({}, {0.0f, 0.0f, -1.0f}));
    const auto clear = readHalf4((*empty)->hdrColorTarget());
    const std::array<DrawItem, 1> items = {
        staticItem(*plane, facingPlaneModel(-2.0f), {0.6f, 0.6f, 0.6f, 1.0f})};
    SceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
    const PerFrame switchMode = [](uint32_t frame, SceneView& view, Camera&) {
        view.temporal.reconstruction = frame >= 9 && frame <= 16
                                           ? ReconstructionMode::VendorTemporal
                                           : ReconstructionMode::NativeTaa;
    };
    auto renderer = Renderer::create(**device, kScenarioWidth, kScenarioHeight, true);
    REQUIRE(renderer.has_value());
    const auto frames = renderSequence(**device, **renderer, 24, ReconstructionMode::NativeTaa,
                                       base, camera, TemporalDebugView::Off, switchMode);
    CHECK(frames[8].status.lastReset == HistoryResetReason::None);
    CHECK(frames[16].status.lastReset == HistoryResetReason::None);
    CHECK(frames[8].status.vendorReset);
    CHECK_FALSE(frames[9].status.vendorReset);
    CHECK(frames[16].status.historyAge == frames[15].status.historyAge + 1);
    for (size_t frame = 0; frame < frames.size(); ++frame) {
        CAPTURE(frame);
        CHECK(frames[frame].status.historyAge == frame + 1);
        CHECK(meanAbsDiff(luminances(frames[frame].history), luminances(clear), allPixels()) >
              0.01f);
    }
}

//======================================================================================================================
TEST_CASE("vendor reconstruction follows an exposure step", "[gpu][temporal][vendor]") {
    using namespace lmx::rhi;
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    if (!(*device)->capabilities().temporalScaler.available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.vendorCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.vendorFloor");
    REQUIRE(plane.has_value());
    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<DrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const SceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});
    constexpr uint32_t kStepFrame = 24;
    constexpr uint32_t kFrames = kStepFrame + 7;
    const PerFrame animate = [](uint32_t frame, SceneView& view, Camera&) {
        view.exposureEv = frame >= kStepFrame ? 2.0f : 0.0f;
    };
    const auto sequence = [&](ReconstructionMode mode) {
        auto renderer = Renderer::create(**device, kScenarioWidth, kScenarioHeight, true);
        REQUIRE(renderer.has_value());
        return renderSequence(**device, **renderer, kFrames, mode, base, camera,
                              TemporalDebugView::Off, animate);
    };
    const auto raw = sequence(ReconstructionMode::Raw);
    const auto vendor = sequence(ReconstructionMode::VendorTemporal);
    const auto pixels = allPixels();
    for (uint32_t frame = kStepFrame - 1; frame <= kFrames; ++frame) {
        std::printf("vendor exposure frame %u: raw mean Y %.9f, vendor mean Y %.9f\n", frame,
                    meanOver(luminances(raw[frame - 1].history), pixels),
                    meanOver(luminances(vendor[frame - 1].history), pixels));
    }
    const float rawMean = meanOver(luminances(raw.back().history), pixels);
    const float vendorMean = meanOver(luminances(vendor.back().history), pixels);
    REQUIRE(rawMean > 0.0f);
    CHECK(std::abs(vendorMean - rawMean) <= 0.05f * rawMean);
}

//======================================================================================================================
TEST_CASE("vendor scaler recreates on output resize and survives content scale changes",
          "[gpu][temporal][vendor]") {
    using namespace lmx::rhi;
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    if (!(*device)->capabilities().temporalScaler.available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto renderer = Renderer::create(**device, kScenarioWidth, kScenarioHeight, true);
    REQUIRE(renderer.has_value());
    const Camera camera;
    SceneView view = scenarioSceneView({}, {0.0f, 0.0f, -1.0f});
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = ReconstructionMode::VendorTemporal;
    renderFrame(**device, **renderer, camera, view);
    const auto first = (*renderer)->temporalStatus();
    REQUIRE(first.vendorScalerGeneration > 0);
    REQUIRE(first.vendorReset);
    view.temporal.renderScale = 0.5f;
    renderFrame(**device, **renderer, camera, view);
    const auto scaled = (*renderer)->temporalStatus();
    CHECK(scaled.lastReset == HistoryResetReason::None);
    CHECK_FALSE(scaled.vendorReset);
    CHECK(scaled.vendorScalerGeneration == first.vendorScalerGeneration);
    CHECK(scaled.extents.renderWidth == 160);
    CHECK(scaled.extents.renderHeight == 90);
    CHECK(scaled.historyBytes == first.historyBytes);
    CHECK(scaled.depthHistoryBytes == first.depthHistoryBytes);
    auto resized = (*renderer)->resize(256, 144);
    INFO(errorOf(resized));
    REQUIRE(resized.has_value());
    renderFrame(**device, **renderer, camera, view);
    const auto afterResize = (*renderer)->temporalStatus();
    CHECK(afterResize.lastReset == HistoryResetReason::ExtentChanged);
    CHECK(afterResize.vendorReset);
    CHECK(afterResize.vendorScalerGeneration == first.vendorScalerGeneration + 1);
    CHECK(afterResize.extents.renderWidth == 128);
    CHECK(afterResize.extents.renderHeight == 72);
    const auto output = readHalf4(*(*renderer)->historyTarget());
    CHECK(output.size() == 256 * 144);

    view.temporal.cameraCut = true;
    renderFrame(**device, **renderer, camera, view);
    CHECK((*renderer)->temporalStatus().lastReset == HistoryResetReason::CameraCut);
    CHECK((*renderer)->temporalStatus().vendorReset);
    CHECK((*renderer)->temporalStatus().vendorScalerGeneration ==
          afterResize.vendorScalerGeneration);
    view.temporal.cameraCut = false;
    view.temporal.enabled = false;
    renderFrame(**device, **renderer, camera, view);
    view.temporal.enabled = true;
    renderFrame(**device, **renderer, camera, view);
    CHECK((*renderer)->temporalStatus().vendorReset);
    CHECK((*renderer)->temporalStatus().vendorScalerGeneration ==
          afterResize.vendorScalerGeneration);
}

//======================================================================================================================
TEST_CASE("vendor frames overlap while scale and diagnostics change", "[gpu][temporal][vendor]") {
    using namespace lmx::rhi;
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    if (!(*device)->capabilities().temporalScaler.available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(32.0f), "lmx.test.vendorOverlap");
    REQUIRE(plane.has_value());
    const std::array<DrawItem, 1> items = {
        staticItem(*plane, facingPlaneModel(-2.0f), {0.6f, 0.6f, 0.6f, 1.0f})};
    SceneView view = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = ReconstructionMode::VendorTemporal;
    auto renderer = Renderer::create(**device, kScenarioWidth, kScenarioHeight, true);
    REQUIRE(renderer.has_value());
    for (uint32_t frame = 0; frame < 5; ++frame) {
        view.temporal.renderScale = frame % 2 == 0 ? 0.5f : 1.0f;
        view.temporal.debugView =
            frame % 2 == 0 ? TemporalDebugView::Off : TemporalDebugView::MotionVectors;
        renderFrameInFlight(**device, **renderer, Camera{}, view);
        CHECK((*renderer)->temporalStatus().vendorScalerGeneration == 1);
        if (frame > 0) {
            CHECK((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
            CHECK_FALSE((*renderer)->temporalStatus().vendorReset);
        }
    }
    (*device)->waitIdle();
    const float mean = meanOver(luminances(readHalf4(*(*renderer)->historyTarget())), allPixels());
    CHECK(std::isfinite(mean));
    CHECK(mean > 0.01f);
}
