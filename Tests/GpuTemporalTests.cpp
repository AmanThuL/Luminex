#include "GpuTestSupport.h"

#include "Engine/GeometryGenerator.h"
#include "Engine/Scene.h"
#include "Render/GraphDump.h"
#include "Render/RenderGraph.h"
#include "Render/Temporal.h"
#include "Render/TemporalHistory.h"
#include "Render/TransientPool.h"

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

// TemporalLab's deliberately static object, at the position Tests/EngineSceneTests.cpp pins.
constexpr glm::vec3 kReferenceCubeCenter{3.0f, 1.0f, 0.0f};

//======================================================================================================================
Camera temporalCamera() {
    Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    return camera;
}

//======================================================================================================================
SceneView temporalSceneView(std::span<const DrawItem> items) {
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
glm::vec2 motionAt(const std::vector<uint8_t>& bytes, uint32_t x, uint32_t y) {
    uint16_t texel[2] = {0, 0};
    std::memcpy(texel, bytes.data() + (size_t{y} * kSize + x) * sizeof(texel), sizeof(texel));
    return {halfToFloat(texel[0]), halfToFloat(texel[1])};
}

//======================================================================================================================
std::vector<uint8_t> readMotion(Renderer& renderer) {
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
glm::vec4 planePointAtPixel(const Camera& camera, uint32_t x, uint32_t y, float planeZ) {
    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kSize);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kSize);
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = 1.0f - v * 2.0f;
    const float tangent = std::tan(camera.fovY * 0.5f);
    const float distance = -planeZ;
    return {ndcX * tangent * distance, ndcY * tangent * distance, planeZ, 1.0f};
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
// The world-space direction the ray through pixel (x, y)'s centre travels in, for a square render
// extent. It stands in for the sky sphere's local vertex position: the sphere is drawn centred on
// the eye, so the view transform cancels the translation and only the direction reaches clip space
// -- which also makes the oracle independent of how far along the ray the point is taken.
glm::vec3 skyDirectionAtPixel(const Camera& camera, uint32_t x, uint32_t y) {
    const float ndcX = (static_cast<float>(x) + 0.5f) / static_cast<float>(kSize) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(kSize) * 2.0f;
    const float tangent = std::tan(camera.fovY * 0.5f);
    const glm::vec4 viewDirection{ndcX * tangent, ndcY * tangent, -1.0f, 0.0f};
    return glm::vec3(glm::inverse(camera.viewMatrix()) * viewDirection);
}

//======================================================================================================================
// The Tests binary runs with its own target dir as CWD, so the golden files are addressed from the
// repo root the build passes in.
std::string goldenPath(std::string_view name) {
    return std::string(LMX_REPO_ROOT) + "/Tests/Golden/" + std::string(name);
}

//======================================================================================================================
void requireMatchesGolden(const std::string& dump, std::string_view name) {
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

//======================================================================================================================
// Parity: with temporal off the renderer declares the frame it declared before temporal existed.
// The golden matches the M5.5-tip renderer's dump of the same frame, so a declaration, an import,
// or an attachment that the temporal path leaked into the off path fails here rather than in a
// screenshot nobody diffs. It is a regression guard over this renderer's own output, not
// independent evidence: the parity claim rests on the three screenshot hashes.
TEST_CASE("the temporal-off frame declares the pre-temporal graph", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 1> items = {DrawItem{.mesh = &*cube}};
    const SceneView view = temporalSceneView(items);

    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const lmx::render::GraphTexture display =
        (*renderer)->declarePasses(graph, commands, temporalCamera(), view);
    graph.presentTexture(display);

    const auto record = graph.compileFrame(1);
    INFO((record.has_value() ? std::string{} : record.error().message));
    REQUIRE(record.has_value());
    requireMatchesGolden(lmx::render::dumpCompiledFrame(*record), "frame-temporal-off.txt");

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// The declared temporal frame, as the spec's pass table states it: the scene pass carrying the
// motion attachment, the reprojection diagnostic declared but culled because the Motion view does
// not sink it, the debug view overwriting the display target, and the history commit copy.
TEST_CASE("a temporal frame declares the motion, debug view and history passes",
          "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 1> items = {DrawItem{.mesh = &*cube}};
    SceneView view = temporalSceneView(items);
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.debugView = lmx::render::TemporalDebugView::MotionVectors;

    // The dumped frame is the second one, so its history is valid and the record shows the
    // reprojection pass a frame with no history could not declare at all.
    renderFrame(**device, **renderer, temporalCamera(), view);

    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const lmx::render::GraphTexture display =
        (*renderer)->declarePasses(graph, commands, temporalCamera(), view);
    graph.presentTexture(display);

    const auto record = graph.compileFrame(2);
    INFO((record.has_value() ? std::string{} : record.error().message));
    REQUIRE(record.has_value());
    requireMatchesGolden(lmx::render::dumpCompiledFrame(*record), "frame-temporal.txt");

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// A camera that moved between two frames over a static surface: every probe texel must carry the
// UV delta motionBetween() derives for the surface point that texel sees.
TEST_CASE("motion vectors reproject a moved camera", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.temporalPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    constexpr float kPlaneZ = -2.0f;
    const glm::mat4 model = facingPlaneModel(kPlaneZ);
    const std::array<DrawItem, 1> items = {
        DrawItem{.mesh = &*plane, .model = model, .previousModel = model}};
    SceneView view = temporalSceneView(items);
    view.temporal.enabled = true;

    Camera previousCamera;
    previousCamera.position = {0.15f, 0.05f, 0.0f};
    Camera currentCamera; // at the origin, which is what planePointAtPixel's ray assumes

    renderFrame(**device, **renderer, previousCamera, view);
    renderFrame(**device, **renderer, currentCamera, view);

    const lmx::render::FrameExtents extents{
        .renderWidth = kSize, .renderHeight = kSize, .outputWidth = kSize, .outputHeight = kSize};
    const lmx::render::CameraFrameState current =
        lmx::render::buildCameraFrameState(currentCamera, extents, {});
    const lmx::render::CameraFrameState previous =
        lmx::render::buildCameraFrameState(previousCamera, extents, {});

    const std::vector<uint8_t> pixels = readMotion(**renderer);
    for (const std::pair<uint32_t, uint32_t> probe :
         {std::pair<uint32_t, uint32_t>{16, 16}, {32, 32}, {48, 40}}) {
        const glm::vec4 point =
            planePointAtPixel(currentCamera, probe.first, probe.second, kPlaneZ);
        const glm::vec2 expected = lmx::render::motionBetween(current.viewProjection * point,
                                                              previous.viewProjection * point);
        const glm::vec2 actual = motionAt(pixels, probe.first, probe.second);
        INFO("probe (" + std::to_string(probe.first) + "," + std::to_string(probe.second) +
             ") expected " + std::to_string(expected.x) + "," + std::to_string(expected.y) +
             " actual " + std::to_string(actual.x) + "," + std::to_string(actual.y));
        REQUIRE(actual.x == Catch::Approx(expected.x).margin(2e-3));
        REQUIRE(actual.y == Catch::Approx(expected.y).margin(2e-3));
    }
}

//======================================================================================================================
// The same oracle with the camera still and the object moved: motion has to come from the item's
// own previous transform, not from the camera pair alone.
TEST_CASE("motion vectors reproject a moved object", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.temporalPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    constexpr float kPlaneZ = -2.0f;
    const glm::vec3 delta{0.2f, 0.1f, 0.0f};
    const glm::mat4 model = facingPlaneModel(kPlaneZ);
    const glm::mat4 previousModel = glm::translate(glm::mat4{1.0f}, delta) * model;

    const Camera camera;
    const std::array<DrawItem, 1> first = {
        DrawItem{.mesh = &*plane, .model = previousModel, .previousModel = previousModel}};
    const std::array<DrawItem, 1> second = {
        DrawItem{.mesh = &*plane, .model = model, .previousModel = previousModel}};

    SceneView view = temporalSceneView(first);
    view.temporal.enabled = true;
    renderFrame(**device, **renderer, camera, view);
    view.items = second;
    renderFrame(**device, **renderer, camera, view);

    const lmx::render::FrameExtents extents{
        .renderWidth = kSize, .renderHeight = kSize, .outputWidth = kSize, .outputHeight = kSize};
    const lmx::render::CameraFrameState state =
        lmx::render::buildCameraFrameState(camera, extents, {});

    const std::vector<uint8_t> pixels = readMotion(**renderer);
    for (const std::pair<uint32_t, uint32_t> probe :
         {std::pair<uint32_t, uint32_t>{16, 16}, {32, 32}, {48, 40}}) {
        const glm::vec4 point = planePointAtPixel(camera, probe.first, probe.second, kPlaneZ);
        const glm::vec4 before = point + glm::vec4(delta, 0.0f);
        const glm::vec2 expected =
            lmx::render::motionBetween(state.viewProjection * point, state.viewProjection * before);
        const glm::vec2 actual = motionAt(pixels, probe.first, probe.second);
        INFO("probe (" + std::to_string(probe.first) + "," + std::to_string(probe.second) +
             ") expected " + std::to_string(expected.x) + "," + std::to_string(expected.y) +
             " actual " + std::to_string(actual.x) + "," + std::to_string(actual.y));
        REQUIRE(actual.x == Catch::Approx(expected.x).margin(2e-3));
        REQUIRE(actual.y == Catch::Approx(expected.y).margin(2e-3));
    }
}

//======================================================================================================================
// Jitter moves where the frame is sampled, never where the surface is: motion is built from the
// unjittered pair, so a still camera over a still surface writes exactly zero however the sequence
// has advanced.
TEST_CASE("jitter leaves a static scene's motion at zero", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.temporalPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const glm::mat4 model = facingPlaneModel(-2.0f);
    const std::array<DrawItem, 1> items = {
        DrawItem{.mesh = &*plane, .model = model, .previousModel = model}};
    SceneView view = temporalSceneView(items);
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;

    const Camera camera;
    renderFrame(**device, **renderer, camera, view);
    renderFrame(**device, **renderer, camera, view);
    renderFrame(**device, **renderer, camera, view);

    const std::vector<uint8_t> pixels = readMotion(**renderer);
    for (const std::pair<uint32_t, uint32_t> probe :
         {std::pair<uint32_t, uint32_t>{16, 16}, {32, 32}, {48, 40}}) {
        const glm::vec2 actual = motionAt(pixels, probe.first, probe.second);
        INFO("probe (" + std::to_string(probe.first) + "," + std::to_string(probe.second) +
             ") motion " + std::to_string(actual.x) + "," + std::to_string(actual.y));
        REQUIRE(actual.x == 0.0f);
        REQUIRE(actual.y == 0.0f);
    }
}

//======================================================================================================================
// An item whose motion is undefined writes the sentinel rather than a plausible zero, because a
// consumer has to be able to tell "did not move" from "cannot be reprojected".
TEST_CASE("an invalid motion class writes the sentinel", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.temporalPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const glm::mat4 model = facingPlaneModel(-2.0f);
    const std::array<DrawItem, 1> items = {
        DrawItem{.mesh = &*plane,
                 .model = model,
                 .previousModel = model,
                 .motionClass = lmx::render::MotionClass::Invalid}};
    SceneView view = temporalSceneView(items);
    view.temporal.enabled = true;

    const Camera camera;
    renderFrame(**device, **renderer, camera, view);

    const std::vector<uint8_t> pixels = readMotion(**renderer);
    const glm::vec2 sentinel = motionAt(pixels, kSize / 2, kSize / 2);
    INFO("sentinel " + std::to_string(sentinel.x) + "," + std::to_string(sentinel.y));
    REQUIRE(std::isinf(sentinel.x));
    REQUIRE(std::isinf(sentinel.y));
}

//======================================================================================================================
// The reprojection diagnostic over a scene that did not move: history is the previous frame's
// scene colour, motion is zero, so the difference the reprojection view shows is black everywhere
// the geometry covers.
TEST_CASE("the reprojection diagnostic is zero on a static scene", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.temporalPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const glm::mat4 model = facingPlaneModel(-2.0f);
    const std::array<DrawItem, 1> items = {
        DrawItem{.mesh = &*plane, .model = model, .previousModel = model}};
    SceneView view = temporalSceneView(items);
    view.temporal.enabled = true;
    view.temporal.debugView = lmx::render::TemporalDebugView::ReprojectionError;

    const Camera camera;
    renderFrame(**device, **renderer, camera, view);
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == lmx::render::HistoryResetReason::None);

    // The committed history is this frame's scene colour, and a still scene reprojects onto
    // itself: history and scene colour must agree bit for bit, which is the difference the
    // diagnostic reports as exactly zero.
    lmx::rhi::Texture* history = (*renderer)->historyTarget();
    REQUIRE(history != nullptr);
    std::vector<uint8_t> historyTexels(size_t{kSize} * kSize * 8);
    history->readback(historyTexels.data(), historyTexels.size());
    std::vector<uint8_t> sceneTexels(historyTexels.size());
    (*renderer)->hdrColorTarget().readback(sceneTexels.data(), sceneTexels.size());
    REQUIRE(historyTexels == sceneTexels);

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
    for (const std::pair<uint32_t, uint32_t> probe :
         {std::pair<uint32_t, uint32_t>{16, 16}, {32, 32}, {48, 40}}) {
        const Pixel pixel = pixelAt(pixels, probe.first, probe.second);
        INFO(describe("reprojection error", probe.first, probe.second, pixel));
        REQUIRE(pixel.r == 0);
        REQUIRE(pixel.g == 0);
        REQUIRE(pixel.b == 0);
    }
}

//======================================================================================================================
// Six frames of the history's own life: the first frame, a valid one, temporal switched off and
// back on, a resize, and an explicit cut. Every frame's reason is asserted, and the allocation
// survives the frame temporal spent switched off.
TEST_CASE("history reset reasons follow the frames that caused them", "[gpu][temporal]") {
    using namespace lmx::rhi;
    using lmx::render::HistoryResetReason;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 1> items = {DrawItem{.mesh = &*cube}};
    SceneView view = temporalSceneView(items);
    const Camera camera = temporalCamera();

    view.temporal.enabled = true;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::FirstFrame);
    const uint64_t historyBytes = (*renderer)->temporalStatus().historyBytes;
    REQUIRE(historyBytes > 0);

    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().historyValid);

    view.temporal.enabled = false;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    // Disabling keeps the allocation rather than freeing it under frames still in flight.
    REQUIRE((*renderer)->temporalStatus().historyBytes == historyBytes);

    view.temporal.enabled = true;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::TemporalEnabled);

    (*device)->waitIdle();
    const auto resized = (*renderer)->resize(kSize / 2, kSize / 2);
    INFO((resized.has_value() ? std::string{} : resized.error().message));
    REQUIRE(resized.has_value());
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::ExtentChanged);
    REQUIRE((*renderer)->temporalStatus().historyBytes == historyBytes / 4);

    view.temporal.cameraCut = true;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::CameraCut);
    REQUIRE_FALSE((*renderer)->temporalStatus().historyValid);
}

//======================================================================================================================
// TemporalLab through the shipped path: the scene's own rigid tracks, advanced one animation step,
// declared with temporal on. It is the case that joins the engine's previous transforms to the
// renderer's motion attachment -- moving tracks carry motion, the static reference carries none,
// and the invalid cube carries the sentinel, all in one image.
TEST_CASE("TemporalLab writes motion for its animated tracks", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto scene = lmx::engine::loadTemporalLabScene(**device);
    INFO(errorOf(scene));
    REQUIRE(scene.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    Camera camera;
    camera.position = (*scene)->initialCamera.position;
    camera.yaw = (*scene)->initialCamera.yaw;
    camera.pitch = (*scene)->initialCamera.pitch;
    camera.fovY = (*scene)->initialCamera.fovY;
    camera.nearZ = (*scene)->initialCamera.nearZ;

    (*scene)->resetMotion();
    std::vector<DrawItem> items;
    for (int frame = 0; frame < 2; ++frame) {
        SceneView view = (*scene)->view(items, lmx::render::ShadowFilter::PCF, /*wireframe=*/false);
        view.temporal.enabled = true;
        view.temporal.debugView = lmx::render::TemporalDebugView::MotionVectors;
        renderFrame(**device, **renderer, camera, view);
        (*scene)->commitFrame();
        (*scene)->advanceAnimation(1.0 / 60.0);
        (*scene)->animate((*scene)->animationTime);
    }

    const std::vector<uint8_t> pixels = readMotion(**renderer);
    bool sawMotion = false;
    bool sawSentinel = false;
    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            const glm::vec2 motion = motionAt(pixels, x, y);
            if (std::isinf(motion.x) || std::isinf(motion.y)) {
                sawSentinel = true;
            } else if (std::abs(motion.x) > 1e-3f || std::abs(motion.y) > 1e-3f) {
                sawMotion = true;
            }
        }
    }
    REQUIRE(sawMotion);
    REQUIRE(sawSentinel);

    // The reference cube is the scene's deliberately still object, so the texel its centre projects
    // to is the one that must read exactly zero -- a still surface, not merely some texel no draw
    // covered.
    const lmx::render::FrameExtents extents{
        .renderWidth = kSize, .renderHeight = kSize, .outputWidth = kSize, .outputHeight = kSize};
    const glm::vec2 uv = lmx::render::clipToMotionUv(
        lmx::render::buildCameraFrameState(camera, extents, {}).viewProjection *
        glm::vec4(kReferenceCubeCenter, 1.0f));
    const uint32_t x = static_cast<uint32_t>(uv.x * static_cast<float>(kSize));
    const uint32_t y = static_cast<uint32_t>(uv.y * static_cast<float>(kSize));
    INFO("reference cube texel (" + std::to_string(x) + "," + std::to_string(y) + ")");
    REQUIRE(x < kSize);
    REQUIRE(y < kSize);
    const glm::vec2 still = motionAt(pixels, x, y);
    INFO("reference cube motion " + std::to_string(still.x) + "," + std::to_string(still.y));
    REQUIRE(still.x == 0.0f);
    REQUIRE(still.y == 0.0f);
}

//======================================================================================================================
// A history fetch that lands inside the extent but within half a texel of its right edge. Its
// bilinear footprint reaches past the border, so the sampler's addressing decides what the missing
// half is: clamped, it is the edge texel itself and the diagnostic reports nothing; wrapped, it is
// the opposite edge -- here a bright emissive surface against a black one -- and the reprojection
// view lights up along a whole column that never moved relative to what it reprojects onto.
TEST_CASE("a history fetch at the border does not read the opposite edge", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.temporalPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    // Two half-screen planes at one depth: the left one emits, the right one is black. Both move
    // with the camera, so the right edge carries motion rather than the motion target's clear.
    constexpr float kPlaneZ = -2.0f;
    constexpr float kHalfExtent = 10.0f;
    const glm::mat4 left = glm::translate(glm::mat4{1.0f}, glm::vec3{-kHalfExtent, 0.0f, 0.0f}) *
                           facingPlaneModel(kPlaneZ);
    const glm::mat4 right = glm::translate(glm::mat4{1.0f}, glm::vec3{kHalfExtent, 0.0f, 0.0f}) *
                            facingPlaneModel(kPlaneZ);
    const std::array<DrawItem, 2> items = {
        DrawItem{.mesh = &*plane,
                 .model = left,
                 .material = {.albedo = {0.0f, 0.0f, 0.0f, 1.0f}, .emissive = {4.0f, 4.0f, 4.0f}},
                 .previousModel = left},
        DrawItem{.mesh = &*plane,
                 .model = right,
                 .material = {.albedo = {0.0f, 0.0f, 0.0f, 1.0f}},
                 .previousModel = right},
    };
    SceneView view = temporalSceneView(items);
    view.bloomEnabled = false; // bloom would spread the emitter's energy across the split
    view.temporal.enabled = true;
    view.temporal.debugView = lmx::render::TemporalDebugView::ReprojectionError;

    // A fraction of a texel to the right: the history UV moves the other way, past the last texel's
    // centre but not past the edge, which is the one interval where addressing decides the answer.
    Camera previousCamera;
    Camera currentCamera;
    currentCamera.position = {0.002f, 0.0f, 0.0f};

    renderFrame(**device, **renderer, previousCamera, view);
    renderFrame(**device, **renderer, currentCamera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == lmx::render::HistoryResetReason::None);

    const std::vector<uint8_t> motion = readMotion(**renderer);
    const glm::vec2 edgeMotion = motionAt(motion, kSize - 1, kSize / 2);
    INFO("edge motion " + std::to_string(edgeMotion.x) + "," + std::to_string(edgeMotion.y));
    // The fetch has to land inside the extent -- outside it the diagnostic reports nothing at all
    // and the addressing would never be consulted.
    const float edgeUv = (static_cast<float>(kSize) - 0.5f) / static_cast<float>(kSize);
    REQUIRE(edgeMotion.x < 0.0f);
    REQUIRE(edgeUv - edgeMotion.x > edgeUv);
    REQUIRE(edgeUv - edgeMotion.x <= 1.0f);

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
    for (const uint32_t y : {kSize / 4, kSize / 2, 3 * kSize / 4}) {
        const Pixel pixel = pixelAt(pixels, kSize - 1, y);
        INFO(describe("border reprojection error", kSize - 1, y, pixel));
        REQUIRE(pixel.r <= 8);
        REQUIRE(pixel.g <= 8);
        REQUIRE(pixel.b <= 8);
    }
}

//======================================================================================================================
// Enable with a debug view, spend a frame with temporal off, then re-enable. The off frame touches
// the motion target nowhere, so the re-enabling frame's import has to still name the read the last
// temporal frame ended with -- a fragment-stage barrier against its own attachment write would not
// drain that dispatch-stage read.
TEST_CASE("a re-enabling frame imports motion as the last temporal frame left it",
          "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 1> items = {DrawItem{.mesh = &*cube}};
    SceneView view = temporalSceneView(items);
    const Camera camera = temporalCamera();

    view.temporal.enabled = true;
    view.temporal.debugView = lmx::render::TemporalDebugView::ReprojectionError;
    renderFrame(**device, **renderer, camera, view);
    renderFrame(**device, **renderer, camera, view);

    view.temporal.enabled = false;
    view.temporal.debugView = lmx::render::TemporalDebugView::Off;
    renderFrame(**device, **renderer, camera, view);

    view.temporal.enabled = true;
    view.temporal.debugView = lmx::render::TemporalDebugView::ReprojectionError;
    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    const lmx::render::GraphTexture display =
        (*renderer)->declarePasses(graph, commands, camera, view);
    graph.presentTexture(display);

    const auto record = graph.compileFrame(1);
    INFO((record.has_value() ? std::string{} : record.error().message));
    REQUIRE(record.has_value());
    const std::string dump = lmx::render::dumpCompiledFrame(*record);
    INFO(dump);
    REQUIRE(dump.contains("r4 texture \"lmx.render.motion\" RG16Float"));
    REQUIRE(dump.contains("texture r4 mips[0..] layers[0..] ShaderRead -> RenderTarget"));

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// The shipped submission pattern rather than the drained one: five temporal frames with no
// waitIdle between them, so three of them are in flight over the motion and history targets at
// once while the debug view is switched on, off and on again. Under MTL_DEBUG_LAYER a frame that
// read a resource another frame had already retired, or a barrier the graph derived against the
// wrong previous use, is reported here rather than in a drained case that never overlaps.
TEST_CASE("temporal frames overlap in flight over one history", "[gpu][temporal]") {
    using namespace lmx::rhi;
    using lmx::render::HistoryResetReason;
    using lmx::render::TemporalDebugView;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.temporalCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<DrawItem, 1> items = {DrawItem{.mesh = &*cube}};
    SceneView view = temporalSceneView(items);
    const Camera camera = temporalCamera();

    // The debug view is what makes the frame *read* motion and the history, so the overlap covers
    // the reprojection dispatch and not just the attachment write.
    view.temporal.enabled = true;
    view.temporal.debugView = TemporalDebugView::ReprojectionError;
    renderFrameInFlight(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::FirstFrame);
    const uint64_t historyBytes = (*renderer)->temporalStatus().historyBytes;
    REQUIRE(historyBytes > 0);

    renderFrameInFlight(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().historyValid);

    view.temporal.enabled = false;
    view.temporal.debugView = TemporalDebugView::Off;
    renderFrameInFlight(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE_FALSE((*renderer)->temporalStatus().historyValid);

    view.temporal.enabled = true;
    view.temporal.debugView = TemporalDebugView::MotionVectors;
    renderFrameInFlight(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::TemporalEnabled);

    renderFrameInFlight(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().historyValid);

    // The one drain, after every submission: the targets were never reallocated, so the run's own
    // completion is the assertion the overlap makes.
    (*device)->waitIdle();
    REQUIRE((*renderer)->temporalStatus().historyBytes == historyBytes);
}

//======================================================================================================================
// The sky's motion is rotation-only: the sphere is drawn centred on the previous eye for the
// previous frame's matrices, so a translation cancels exactly and only the camera's rotation
// survives. Both halves are pinned here against the same motionBetween() oracle the geometry cases
// use, over a scene that is nothing but sky -- the sphere covers every texel, so the probes read
// the sky path rather than a piece of geometry in front of it.
TEST_CASE("sky motion follows the camera's rotation alone", "[gpu][temporal]") {
    using namespace lmx::rhi;

    constexpr std::array<uint8_t, 4> kSkyTexel = {0, 128, 255, 255};
    const TextureMip skyMip{.data = kSkyTexel.data(), .bytesPerRow = 4};
    const std::array<TextureMip, 6> skyFaces = {skyMip, skyMip, skyMip, skyMip, skyMip, skyMip};

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto skySphere = lmx::render::createMesh(
        **device, lmx::render::fromGeo(lmx::engine::makeSphere(0.5f, 20, 20)),
        "lmx.test.temporalSkySphere");
    INFO(errorOf(skySphere));
    REQUIRE(skySphere.has_value());

    auto skyCubemap = (*device)->createTexture({.width = 1,
                                                .height = 1,
                                                .format = Format::RGBA8Unorm,
                                                .kind = TextureKind::Cube,
                                                .sampled = true,
                                                .label = "lmx.test.temporalSkyCubemap"},
                                               skyFaces);
    INFO(errorOf(skyCubemap));
    REQUIRE(skyCubemap.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    // No items at all: the sky sphere is the frame's only geometry.
    SceneView view = temporalSceneView({});
    view.skySphere = &*skySphere;
    view.skyCubemap = skyCubemap->get();
    view.temporal.enabled = true;

    constexpr std::array<std::pair<uint32_t, uint32_t>, 3> kProbes = {
        std::pair<uint32_t, uint32_t>{12, 12}, {32, 32}, {50, 44}};

    Camera origin;
    Camera translated;
    translated.position = {0.4f, 0.15f, -0.25f};
    Camera rotated = translated;
    rotated.yaw = 0.05f;

    renderFrame(**device, **renderer, origin, view);
    renderFrame(**device, **renderer, translated, view);

    // Translation alone: the sphere moves with the eye, so every sky texel reprojects onto itself.
    const std::vector<uint8_t> translationPixels = readMotion(**renderer);
    for (const std::pair<uint32_t, uint32_t> probe : kProbes) {
        const glm::vec2 actual = motionAt(translationPixels, probe.first, probe.second);
        INFO("translated probe (" + std::to_string(probe.first) + "," +
             std::to_string(probe.second) + ") motion " + std::to_string(actual.x) + "," +
             std::to_string(actual.y));
        REQUIRE(actual.x == 0.0f);
        REQUIRE(actual.y == 0.0f);
    }

    renderFrame(**device, **renderer, rotated, view);

    const lmx::render::FrameExtents extents{
        .renderWidth = kSize, .renderHeight = kSize, .outputWidth = kSize, .outputHeight = kSize};
    const lmx::render::CameraFrameState current =
        lmx::render::buildCameraFrameState(rotated, extents, {});
    const lmx::render::CameraFrameState previous =
        lmx::render::buildCameraFrameState(translated, extents, {});

    const std::vector<uint8_t> rotationPixels = readMotion(**renderer);
    for (const std::pair<uint32_t, uint32_t> probe : kProbes) {
        const glm::vec3 direction = skyDirectionAtPixel(rotated, probe.first, probe.second);
        const glm::vec4 currentPoint{direction + rotated.position, 1.0f};
        const glm::vec4 previousPoint{direction + translated.position, 1.0f};
        const glm::vec2 expected = lmx::render::motionBetween(
            current.viewProjection * currentPoint, previous.viewProjection * previousPoint);
        const glm::vec2 actual = motionAt(rotationPixels, probe.first, probe.second);
        INFO("rotated probe (" + std::to_string(probe.first) + "," + std::to_string(probe.second) +
             ") expected " + std::to_string(expected.x) + "," + std::to_string(expected.y) +
             " actual " + std::to_string(actual.x) + "," + std::to_string(actual.y));
        REQUIRE(actual.x == Catch::Approx(expected.x).margin(2e-3));
        REQUIRE(actual.y == Catch::Approx(expected.y).margin(2e-3));
    }
}
