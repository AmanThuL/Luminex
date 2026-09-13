#pragma once

#include "GpuTestSupport.h"

#include "Asset/GeometryGenerator.h"
#include "Render/GraphDump.h"
#include "Render/RenderGraph.h"
#include "Render/Temporal.h"
#include "Render/TemporalHistory.h"
#include "Render/TransientPool.h"
#include "Scene/Scene.h"

#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

using lmx::render::Camera;
using lmx::render::DrawItem;
using lmx::render::Mesh;
using lmx::render::Renderer;
using lmx::render::SceneView;

// TemporalLab's deliberately static object, at the position Tests/SceneTemporalLabTests.cpp pins.
[[maybe_unused]] constexpr glm::vec3 kReferenceCubeCenter{3.0f, 1.0f, 0.0f};

//======================================================================================================================
inline Camera temporalCamera() {
    Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    return camera;
}

//======================================================================================================================
inline SceneView temporalSceneView(std::span<const DrawItem> items) {
    SceneView view;
    view.items = items;
    view.lights[0] = {.strength = {0.5f, 0.5f, 0.5f}, .direction = {0.0f, 0.0f, -1.0f}};
    view.lights[1].strength = {0.0f, 0.0f, 0.0f};
    view.lights[2].strength = {0.0f, 0.0f, 0.0f};
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 4.0f};
    return view;
}

//======================================================================================================================
// The motion target is RG16Float, so a readback carries IEEE halves. Widening them here keeps the
// oracle comparisons in the same units Source/Render/Temporal.h states motion in.
inline float halfToFloat(uint16_t bits) {
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
inline glm::vec2 motionAt(const std::vector<uint8_t>& bytes, uint32_t x, uint32_t y) {
    uint16_t texel[2] = {0, 0};
    std::memcpy(texel, bytes.data() + (size_t{y} * kSize + x) * sizeof(texel), sizeof(texel));
    return {halfToFloat(texel[0]), halfToFloat(texel[1])};
}

//======================================================================================================================
inline std::vector<uint8_t> readMotion(Renderer& renderer) {
    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    lmx::rhi::Texture* motion = renderer.motionTarget();
    REQUIRE(motion != nullptr);
    motion->readback(pixels.data(), pixels.size());
    return pixels;
}

//======================================================================================================================
// The world point the ray through pixel (x, y)'s centre meets a fronto-parallel plane at world
// `planeZ` on, for a camera at the origin looking down -Z. It is what turns a probe texel into the
// surface point the motion oracle needs, without inverting a projection matrix in the test.
// `extent` is the square extent the pixel indexes into: the output one by default, and the render
// one for a probe read out of a target rasterised at a smaller active rectangle.
inline glm::vec4 planePointAtPixel(const Camera& camera, uint32_t x, uint32_t y, float planeZ,
                                   uint32_t extent = kSize) {
    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(extent);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(extent);
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = 1.0f - v * 2.0f;
    const float tangent = std::tan(camera.fovY * 0.5f);
    const float distance = -planeZ;
    return {ndcX * tangent * distance, ndcY * tangent * distance, planeZ, 1.0f};
}

//======================================================================================================================
// A large plane rotated to face a camera looking down -Z, so every probe texel lands on one known
// surface at one known depth.
inline glm::mat4 facingPlaneModel(float z) {
    return glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, z}) *
           glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
}

//======================================================================================================================
// One declared and executed frame through the renderer's own graph, which is the path --screenshot
// and the GPU tests take.
inline void renderFrame(lmx::rhi::Device& device, Renderer& renderer, const Camera& camera,
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
inline void renderFrameInFlight(lmx::rhi::Device& device, Renderer& renderer, const Camera& camera,
                                const SceneView& view) {
    lmx::rhi::CommandList& commands = device.beginFrame();
    renderer.render(commands, camera, view, /*barrierForSampling=*/false);
    device.endFrame(nullptr);
}

//======================================================================================================================
// The world-space direction the ray through pixel (x, y)'s centre travels in, for a square render
// extent. It stands in for the sky sphere's local vertex position: the sphere is drawn centred on
// the eye, so the view transform cancels the translation and only the direction reaches clip space
// -- which also makes the oracle independent of how far along the ray the point is taken.
inline glm::vec3 skyDirectionAtPixel(const Camera& camera, uint32_t x, uint32_t y) {
    const float ndcX = (static_cast<float>(x) + 0.5f) / static_cast<float>(kSize) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(kSize) * 2.0f;
    const float tangent = std::tan(camera.fovY * 0.5f);
    const glm::vec4 viewDirection{ndcX * tangent, ndcY * tangent, -1.0f, 0.0f};
    return glm::vec3(glm::inverse(camera.viewMatrix()) * viewDirection);
}

//======================================================================================================================
// The Tests binary runs with its own target dir as CWD, so the golden files are addressed from the
// repo root the build passes in.
inline std::string goldenPath(std::string_view name) {
    return std::string(LMX_REPO_ROOT) + "/Tests/Golden/" + std::string(name);
}

//======================================================================================================================
inline void requireMatchesGolden(const std::string& dump, std::string_view name) {
    const std::string path = goldenPath(name);
    std::ifstream file(path, std::ios::binary);
    INFO("golden file: " + path);
    REQUIRE(file.good());

    std::ostringstream expected;
    expected << file.rdbuf();

    if (expected.str() != dump) {
        std::ofstream actual(path + ".actual", std::ios::binary | std::ios::trunc);
        actual << dump;
        INFO("actual output written to: " + path + ".actual");
        INFO("--- actual ---\n" + dump);
    }
    REQUIRE(expected.str() == dump);
}

} // namespace

// The scripted stability scenarios (M6.2 spec 11). They run at a 16:9 extent rather than the
// 64x64 the declaration cases use, because thin geometry and a few pixels of motion per frame only
// mean something against a real aspect ratio, and they read back this frame's colour slot -- which
// holds the mode's own output either way -- beside the raw HDR target it was built from.
namespace {

[[maybe_unused]] constexpr uint32_t kScenarioWidth = 320;
[[maybe_unused]] constexpr uint32_t kScenarioHeight = 180;
[[maybe_unused]] constexpr size_t kScenarioPixels = size_t{kScenarioWidth} * kScenarioHeight;
// The tolerances are frozen before measurement; measurement itself starts after one jitter period.
[[maybe_unused]] constexpr uint32_t kMeasureFirstFrame = 17;

using lmx::render::ReconstructionMode;
using lmx::render::TemporalDebugView;

//======================================================================================================================
// One RGBA16Float target, widened to floats. Both the colour history and the raw scene target are
// half-precision, so a comparison between them has to speak the same units the shader wrote.
inline std::vector<glm::vec4> readHalf4(lmx::rhi::Texture& texture) {
    std::vector<uint8_t> bytes(kScenarioPixels * 8);
    texture.readback(bytes.data(), bytes.size());
    std::vector<glm::vec4> pixels(kScenarioPixels);
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
inline float luminance(const glm::vec4& color) {
    return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

//======================================================================================================================
inline std::vector<float> luminances(const std::vector<glm::vec4>& image) {
    std::vector<float> result(image.size());
    for (size_t i = 0; i < image.size(); ++i) {
        result[i] = luminance(image[i]);
    }
    return result;
}

//======================================================================================================================
// Mean |a - b| over the named pixels. An empty pixel set is a scenario that failed to find the
// geometry it meant to measure, which is a test bug rather than a passing measurement.
inline float meanAbsDiff(const std::vector<float>& a, const std::vector<float>& b,
                         const std::vector<uint32_t>& pixels) {
    REQUIRE_FALSE(pixels.empty());
    double sum = 0.0;
    for (uint32_t index : pixels) {
        sum += std::abs(double{a[index]} - double{b[index]});
    }
    return static_cast<float>(sum / static_cast<double>(pixels.size()));
}

//======================================================================================================================
inline float meanOver(const std::vector<float>& values, const std::vector<uint32_t>& pixels) {
    REQUIRE_FALSE(pixels.empty());
    double sum = 0.0;
    for (uint32_t index : pixels) {
        sum += values[index];
    }
    return static_cast<float>(sum / static_cast<double>(pixels.size()));
}

//======================================================================================================================
inline std::vector<uint32_t> allPixels() {
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
inline std::vector<ScenarioFrame> renderSequence(lmx::rhi::Device& device, Renderer& renderer,
                                                 uint32_t frames, ReconstructionMode mode,
                                                 const SceneView& base, const Camera& baseCamera,
                                                 TemporalDebugView debugView,
                                                 const PerFrame& perFrame) {
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
inline float frameToFrameChange(const std::vector<ScenarioFrame>& frames,
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
inline float pixelsPerUnit(const Camera& camera, float distance) {
    const float aspect = static_cast<float>(kScenarioWidth) / static_cast<float>(kScenarioHeight);
    return 0.5f * static_cast<float>(kScenarioWidth) /
           (distance * std::tan(camera.fovY * 0.5f) * aspect);
}

//======================================================================================================================
// The pixel column a world x lands in, for a camera at the origin looking down -Z.
inline float pixelForWorldX(const Camera& camera, float worldX, float distance) {
    return 0.5f * static_cast<float>(kScenarioWidth) + worldX * pixelsPerUnit(camera, distance);
}

//======================================================================================================================
// The flat colour Shaders/TemporalDebugView.slang draws the RejectionMask in for a reason code.
// One mirror of the shader's legend, so a case names the reason it expects by the shared constant
// in Render/TemporalResolve.h and never by a colour spelled out at the assertion.
//
// The clipped flag adds half a unit of green on top, which is why a case comparing all three
// channels has to expect a reason whose green is already saturated or absent.
inline glm::vec3 rejectionMaskColor(uint32_t reason) {
    switch (reason) {
    case lmx::render::kRejectionReasonOffScreen:
        return {0.0f, 0.0f, 1.0f};
    case lmx::render::kRejectionReasonInvalid:
        return {1.0f, 0.0f, 1.0f};
    case lmx::render::kRejectionReasonDisoccluded:
        return {1.0f, 0.0f, 0.0f};
    case lmx::render::kRejectionReasonReactive:
        return {1.0f, 1.0f, 0.0f};
    default:
        return {0.0f, 0.0f, 0.0f};
    }
}

//======================================================================================================================
// One BGRA8 texel of a display-target readback, as the debug views write them: unencoded, because
// the view replaces the display transform's output rather than feeding it.
inline glm::vec3 displayRgb(const std::vector<uint8_t>& display, uint32_t index) {
    return {display[index * 4 + 2] / 255.0f, display[index * 4 + 1] / 255.0f,
            display[index * 4] / 255.0f};
}

//======================================================================================================================
inline Camera scenarioCamera(glm::vec3 position, float pitch) {
    Camera camera;
    camera.position = position;
    camera.pitch = pitch;
    return camera;
}

//======================================================================================================================
inline SceneView scenarioSceneView(std::span<const DrawItem> items, glm::vec3 lightDirection) {
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
inline glm::mat4 boxModel(glm::vec3 center, glm::vec3 scale) {
    return glm::translate(glm::mat4{1.0f}, center) * glm::scale(glm::mat4{1.0f}, scale);
}

//======================================================================================================================
// A draw that did not move: previousModel matched to model, which is what makes the item reproject
// onto itself. Leaving it at the default would have every static object report the motion of a
// teleport from the identity transform.
inline DrawItem staticItem(const Mesh& mesh, const glm::mat4& model, glm::vec4 albedo) {
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
inline void appendCheckerFloor(std::vector<DrawItem>& items, const Mesh& cube, const Mesh& plane) {
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
// The mean luminance of a sequence's own output over the measured window, per pixel of `pixels`.
// It is what "mean Y over frames 17-32" is measured as, for both sides of a comparison between two
// sequences.
inline float meanOverWindow(const std::vector<ScenarioFrame>& frames,
                            const std::vector<uint32_t>& pixels) {
    REQUIRE(frames.size() >= kMeasureFirstFrame);
    double sum = 0.0;
    uint32_t counted = 0;
    for (size_t i = kMeasureFirstFrame - 1; i < frames.size(); ++i) {
        sum += meanOver(luminances(frames[i].history), pixels);
        ++counted;
    }
    return static_cast<float>(sum / counted);
}

//======================================================================================================================
// The static-stability scene the upscaling scenarios share with M6.2's: a receding checkerboard
// whose perspective compresses its high spatial frequency toward the horizon, and five poles two
// *output* pixels wide -- the width is stated in output pixels at every render scale, because the
// output extent is what the reconstruction has to resolve them at.
inline std::vector<DrawItem> checkerAndPoleItems(const Camera& camera, const Mesh& cube,
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
[[maybe_unused]] constexpr float kQuadDistance = 10.0f; // camera z 4 to quad z -6
[[maybe_unused]] constexpr float kQuadHalfWidth = 1.0f;
[[maybe_unused]] constexpr float kQuadStartX = -3.0f;
[[maybe_unused]] constexpr glm::vec3 kQuadScale{2.0f, 2.0f, 0.1f};

//======================================================================================================================
inline Camera movingQuadCamera() {
    return scenarioCamera({0.0f, 0.0f, 4.0f}, 0.0f);
}

//======================================================================================================================
// Four *output* pixels of translation a frame, whatever the render extent: the scenario is stated
// in the extent the picture is presented at.
inline float movingQuadCenterX(const Camera& camera, uint32_t frame) {
    const float stepX = 4.0f / pixelsPerUnit(camera, kQuadDistance);
    return kQuadStartX + stepX * static_cast<float>(frame - 1);
}

//======================================================================================================================
inline glm::mat4 movingQuadModel(const Camera& camera, uint32_t frame) {
    return boxModel({movingQuadCenterX(camera, frame), 0.0f, -6.0f}, kQuadScale);
}

//======================================================================================================================
// The quad's screen span on `frame`, from the same projection the scene was authored against.
inline std::pair<int32_t, int32_t> movingQuadColumns(const Camera& camera, uint32_t frame) {
    const float center = movingQuadCenterX(camera, frame);
    return {static_cast<int32_t>(
                std::ceil(pixelForWorldX(camera, center - kQuadHalfWidth, kQuadDistance))),
            static_cast<int32_t>(
                std::floor(pixelForWorldX(camera, center + kQuadHalfWidth, kQuadDistance)))};
}

//======================================================================================================================
// The backdrop band the quad vacated over the last two frames, well inside its top and bottom edges
// so the measured pixels are uncovered backdrop rather than the quad's own silhouette.
inline std::vector<uint32_t> movingQuadVacated(const Camera& camera, uint32_t frame) {
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
inline float movingQuadContrast(const Camera& camera, const std::vector<float>& image,
                                uint32_t frame) {
    const auto [left, right] = movingQuadColumns(camera, frame);
    const auto quadSample =
        static_cast<uint32_t>((kScenarioHeight / 2) * kScenarioWidth + (left + right) / 2);
    const auto wallSample = static_cast<uint32_t>((kScenarioHeight / 2) * kScenarioWidth + 4);
    return std::abs(image[quadSample] - image[wallSample]);
}

//======================================================================================================================
// The moving-quad scene, with the quad as item 1 so a PerFrame can drive it.
inline std::vector<DrawItem> movingQuadItems(const Camera& camera, const Mesh& cube,
                                             const Mesh& plane) {
    std::vector<DrawItem> items;
    items.push_back(staticItem(plane, facingPlaneModel(-12.0f), {0.6f, 0.6f, 0.6f, 1.0f}));
    items.push_back(staticItem(cube, movingQuadModel(camera, 1), {0.05f, 0.05f, 0.05f, 1.0f}));
    return items;
}

} // namespace
