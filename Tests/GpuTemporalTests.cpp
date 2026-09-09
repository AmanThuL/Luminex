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
// `extent` is the square extent the pixel indexes into: the output one by default, and the render
// one for a probe read out of a target rasterised at a smaller active rectangle.
glm::vec4 planePointAtPixel(const Camera& camera, uint32_t x, uint32_t y, float planeZ,
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
// The declared temporal frame under the default reconstruction, as the spec's pass table states
// it: the exposure seed a manual-mode temporal frame declares to record the applied exposure pair
// (M6.2 spec 7, exported because its consumer is the next frame), the scene pass carrying the
// motion and reactive attachments, the reprojection diagnostic declared but culled because the
// Motion view does not sink it, the resolve writing this frame's colour slot with no copy behind
// it, and the debug view overwriting the display target after the display pass produced it.
TEST_CASE("a temporal frame declares the motion, resolve and debug view passes",
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
    view.temporal.reconstruction = lmx::render::ReconstructionMode::NativeTaa;
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
// The raw bypass declaring the same inputs and none of the accumulation: the commit copy makes this
// frame's raw colour the slot's contents, no resolve pass exists, and bloom and display read the
// raw scene colour. It is the declaration a test comparing the two modes at identical inputs rests
// on, so it is pinned by a golden of its own rather than inferred from the accumulated one.
TEST_CASE("a raw temporal frame declares the commit copy and no resolve", "[gpu][temporal]") {
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
    view.temporal.reconstruction = lmx::render::ReconstructionMode::Raw;
    view.temporal.debugView = lmx::render::TemporalDebugView::MotionVectors;

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
    requireMatchesGolden(lmx::render::dumpCompiledFrame(*record), "frame-temporal-raw.txt");

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// The same raw frame rasterising into half of each axis. The declaration differs in exactly three
// places: the scene pass carries a render area of the active rectangle, the one-to-one copy is
// replaced by the spatial commit that resamples that rectangle into the whole colour slot, and
// bloom and display read the slot the commit wrote rather than the scene colour behind it. Every
// target keeps its output-extent allocation, so no descriptor and no transient footprint moves.
TEST_CASE("an upscaled raw temporal frame declares the spatial commit", "[gpu][temporal]") {
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
    view.temporal.reconstruction = lmx::render::ReconstructionMode::Raw;
    view.temporal.debugView = lmx::render::TemporalDebugView::MotionVectors;
    view.temporal.renderScale = 0.5f;

    renderFrame(**device, **renderer, temporalCamera(), view);
    const lmx::render::TemporalStatus status = (*renderer)->temporalStatus();
    CHECK(status.upscaled);
    CHECK(status.extents.renderWidth == kSize / 2);
    CHECK(status.extents.renderHeight == kSize / 2);
    CHECK(status.extents.outputWidth == kSize);
    CHECK(status.extents.outputHeight == kSize);
    CHECK(status.renderScale == 0.5f);

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
    const std::string dump = lmx::render::dumpCompiledFrame(*record);
    requireMatchesGolden(dump, "frame-temporal-raw-upscaled.txt");
    // Said twice on purpose: the golden pins the whole record, and these two say which part of it
    // the case exists for, so a re-baselined golden cannot quietly drop either.
    CHECK(dump.find("render area 32x32") != std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.commitHistory") == std::string::npos);

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// The declared upscaled accumulation. At half of each axis under NativeTaa the frame carries the
// scene pass's render area and reaches lmx.pass.temporal.upscale instead of the native resolve --
// which reads the previous depth slot at this frame's extent and so cannot serve a frame whose
// render extent differs from its output extent. No commit copy is declared in either extent: the
// accumulation writes the colour slot itself.
TEST_CASE("an upscaled native TAA frame declares the temporal upscale", "[gpu][temporal]") {
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
    view.temporal.reconstruction = lmx::render::ReconstructionMode::NativeTaa;
    view.temporal.debugView = lmx::render::TemporalDebugView::MotionVectors;
    view.temporal.renderScale = 0.5f;

    // The dumped frame is the second one, so its history is valid and its predecessor rasterised at
    // the same render extent -- which is what makes this the steady upscaled declaration rather
    // than the first frame after a scale change.
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
    const std::string dump = lmx::render::dumpCompiledFrame(*record);
    requireMatchesGolden(dump, "frame-temporal-upscaled.txt");
    // Said twice on purpose: the golden pins the whole record, and these say which part of it the
    // case exists for, so a re-baselined golden cannot quietly drop any of them.
    CHECK(dump.find("render area 32x32") != std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.upscale") != std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.resolve") == std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.commitHistory") == std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.commitUpscaled") == std::string::npos);

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// The scale-1 frame declares the copy and never the spatial pass, whichever mode it runs: the
// upscaling kernels are selected by the extents, so a frame whose render extent is its output
// extent must not reach them at all.
TEST_CASE("a scale-1 temporal frame declares no upscaling pass", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    SceneView view = temporalSceneView({});
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::Raw;
    view.temporal.renderScale = 1.0f;
    renderFrame(**device, **renderer, temporalCamera(), view);
    CHECK_FALSE((*renderer)->temporalStatus().upscaled);

    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    graph.presentTexture((*renderer)->declarePasses(graph, commands, temporalCamera(), view));
    const auto record = graph.compileFrame(2);
    REQUIRE(record.has_value());
    const std::string dump = lmx::render::dumpCompiledFrame(*record);
    CHECK(dump.find("lmx.pass.temporal.commitHistory") != std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.commitUpscaled") == std::string::npos);
    CHECK(dump.find("lmx.pass.temporal.upscale") == std::string::npos);
    CHECK(dump.find("render area") == std::string::npos);

    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// ADR 0016's kernel-selection rule, stated directly rather than inferred from an upscaled frame:
// the native kernel addresses every input at one extent, the previous depth slot included, so the
// one scale-1 frame that follows a differently sized render extent runs the upscaling kernel --
// which carries the previous render extent explicitly -- and the frame after it is native again.
TEST_CASE("a scale-1 frame after a scale change declares the upscale kernel once",
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
    view.temporal.reconstruction = lmx::render::ReconstructionMode::NativeTaa;

    // The predecessor: a whole frame at half scale, so the history the following frames read was
    // accumulated with a 32x32 depth slot behind it.
    view.temporal.renderScale = 0.5f;
    renderFrame(**device, **renderer, temporalCamera(), view);

    lmx::render::TransientPool transients(**device);
    // Declares one frame at `scale` and answers its compiled record's dump. Every call is a
    // declared temporal frame, so the order of the calls is the order of the frames.
    const auto declareDump = [&](float scale) {
        view.temporal.renderScale = scale;
        CommandList& commands = (*device)->beginFrame();
        transients.beginFrame();
        lmx::render::RenderGraph graph(transients);
        graph.presentTexture((*renderer)->declarePasses(graph, commands, temporalCamera(), view));
        const auto record = graph.compileFrame(2);
        INFO((record.has_value() ? std::string{} : record.error().message));
        REQUIRE(record.has_value());
        std::string dump = lmx::render::dumpCompiledFrame(*record);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        return dump;
    };

    const std::string afterChange = declareDump(1.0f);
    CHECK(afterChange.find("lmx.pass.temporal.upscale") != std::string::npos);
    CHECK(afterChange.find("lmx.pass.temporal.resolve") == std::string::npos);

    const std::string steady = declareDump(1.0f);
    CHECK(steady.find("lmx.pass.temporal.resolve") != std::string::npos);
    CHECK(steady.find("lmx.pass.temporal.upscale") == std::string::npos);
}

//======================================================================================================================
// The picture an upscaled frame presents covers the whole output extent. The scene fills the view,
// so a display target holding the clear colour anywhere would mean the frame reached only the
// rectangle it rasterised into -- a margin the upscale failed to write -- and the colour slot the
// commit wrote is still the full output-extent allocation it always was.
TEST_CASE("an upscaled frame fills the display target", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.temporalPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    // What an empty frame's background reaches the display as, derived rather than hard-coded:
    // it is the renderer's own clear colour through the same display transform.
    auto empty = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(empty));
    REQUIRE(empty.has_value());
    renderFrame(**device, **empty, temporalCamera(), temporalSceneView({}));
    std::vector<uint8_t> background(size_t{kSize} * kSize * 4);
    (*empty)->colorTarget().readback(background.data(), background.size());
    const Pixel clearPixel = pixelAt(background, 0, 0);

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const glm::mat4 model = facingPlaneModel(-2.0f);
    const std::array<DrawItem, 1> items = {
        DrawItem{.mesh = &*plane, .model = model, .previousModel = model}};
    SceneView view = temporalSceneView(items);
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::Raw;
    view.temporal.renderScale = 0.5f;

    const Camera camera;
    for (uint32_t frame = 0; frame < 4; ++frame) {
        renderFrame(**device, **renderer, camera, view);
    }
    const lmx::render::TemporalStatus status = (*renderer)->temporalStatus();
    REQUIRE(status.upscaled);
    REQUIRE(status.extents.renderWidth == kSize / 2);
    // The colour slots never shrank with the render extent: both are still the output extent.
    REQUIRE(status.historyBytes == 2 * uint64_t{kSize} * kSize * 8);

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());
    uint32_t clearPixels = 0;
    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            const Pixel pixel = pixelAt(pixels, x, y);
            if (pixel.r == clearPixel.r && pixel.g == clearPixel.g && pixel.b == clearPixel.b) {
                INFO(describe("clear-coloured display pixel", x, y, pixel));
                ++clearPixels;
            }
        }
    }
    CHECK(clearPixels == 0);

    // The colour slot the commit wrote is readable over the whole output extent, which is the
    // allocation the spec's table states for it whatever the frame rasterised at.
    lmx::rhi::Texture* history = (*renderer)->historyTarget();
    REQUIRE(history != nullptr);
    std::vector<uint8_t> slot(size_t{kSize} * kSize * 8);
    history->readback(slot.data(), slot.size());
    // Alpha is the accumulation age, and the spatial commit writes 1 for every output texel, so
    // the whole allocation carrying it is what says the pass covered the output extent.
    for (size_t texel = 0; texel < size_t{kSize} * kSize; ++texel) {
        uint16_t alpha = 0;
        std::memcpy(&alpha, slot.data() + texel * 8 + 6, sizeof(alpha));
        REQUIRE(halfToFloat(alpha) == 1.0f);
    }
}

//======================================================================================================================
// The upscale's kernel preserves a constant field. The five-tap Catmull-Rom drops the four corner
// taps, so a fetch that did not renormalise the five it keeps would scale every texel of a flat
// emissive surface by the missing weight -- a whole-image brightness shift no probe of a shaded
// scene would separate from its shading.
TEST_CASE("the spatial upscale keeps a constant radiance constant", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.constantPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const glm::mat4 model = facingPlaneModel(-2.0f);
    const std::array<DrawItem, 1> items = {
        DrawItem{.mesh = &*plane,
                 .model = model,
                 .material = {.albedo = {0.0f, 0.0f, 0.0f, 1.0f}, .emissive = {0.5f, 0.5f, 0.5f}},
                 .previousModel = model}};
    SceneView view = temporalSceneView(items);
    for (auto& light : view.lights) {
        light.strength = {};
    }
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = false;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::Raw;
    view.temporal.renderScale = 0.5f;

    const Camera camera;
    renderFrame(**device, **renderer, camera, view);
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().upscaled);

    // The value the plane shaded to, read out of the active rectangle the frame rasterised into.
    std::vector<uint8_t> scene(size_t{kSize} * kSize * 8);
    (*renderer)->hdrColorTarget().readback(scene.data(), scene.size());
    const auto texelAt = [](const std::vector<uint8_t>& bytes, uint32_t x, uint32_t y) {
        uint16_t halves[4] = {0, 0, 0, 0};
        std::memcpy(halves, bytes.data() + (size_t{y} * kSize + x) * 8, sizeof(halves));
        return glm::vec4{halfToFloat(halves[0]), halfToFloat(halves[1]), halfToFloat(halves[2]),
                         halfToFloat(halves[3])};
    };
    const glm::vec4 rendered = texelAt(scene, kSize / 4, kSize / 4);
    REQUIRE(rendered.r > 0.0f);

    lmx::rhi::Texture* history = (*renderer)->historyTarget();
    REQUIRE(history != nullptr);
    std::vector<uint8_t> committed(scene.size());
    history->readback(committed.data(), committed.size());
    for (const std::pair<uint32_t, uint32_t> probe :
         {std::pair<uint32_t, uint32_t>{1, 1}, {kSize / 2, kSize / 2}, {kSize - 2, kSize - 2}}) {
        const glm::vec4 upscaled = texelAt(committed, probe.first, probe.second);
        INFO("probe (" + std::to_string(probe.first) + "," + std::to_string(probe.second) +
             ") rendered " + std::to_string(rendered.r) + " upscaled " +
             std::to_string(upscaled.r));
        REQUIRE(upscaled.r == Catch::Approx(rendered.r).epsilon(1e-2));
        REQUIRE(upscaled.g == Catch::Approx(rendered.g).epsilon(1e-2));
        REQUIRE(upscaled.b == Catch::Approx(rendered.b).epsilon(1e-2));
    }
}

//======================================================================================================================
// Motion at a render scale below 1 is still the UV delta of the render extent it was rasterised
// into: the probe reads the active rectangle of a target allocated at the output extent, and the
// oracle indexes its pixels over the render extent.
TEST_CASE("motion at half scale matches the render-extent oracle", "[gpu][temporal]") {
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

    constexpr uint32_t kRender = kSize / 2;
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
    view.temporal.renderScale = 0.5f;
    renderFrame(**device, **renderer, camera, view);
    view.items = second;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().extents.renderWidth == kRender);

    // The projection's aspect comes from the output extent, so a square frame's matrices are the
    // ones the full-extent oracle uses; only the pixel-to-ray mapping is at the render extent.
    const lmx::render::FrameExtents extents{.renderWidth = kRender,
                                            .renderHeight = kRender,
                                            .outputWidth = kSize,
                                            .outputHeight = kSize};
    const lmx::render::CameraFrameState state =
        lmx::render::buildCameraFrameState(camera, extents, {});

    const std::vector<uint8_t> pixels = readMotion(**renderer);
    for (const std::pair<uint32_t, uint32_t> probe :
         {std::pair<uint32_t, uint32_t>{8, 8}, {16, 16}, {24, 20}}) {
        const glm::vec4 point =
            planePointAtPixel(camera, probe.first, probe.second, kPlaneZ, kRender);
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
// The Raw age view samples its newly committed colour slot. When that slot is recycled two frames
// later, its imported terminal use must drain that read before the next history copy overwrites it.
TEST_CASE("raw history age records the sampled slot before recycling",
          "[gpu][temporal][taa-diagnostics]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());
    SceneView view = temporalSceneView({});
    view.temporal.enabled = true;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::Raw;
    view.temporal.debugView = lmx::render::TemporalDebugView::HistoryAge;
    renderFrame(**device, **renderer, temporalCamera(), view);
    view.temporal.debugView = lmx::render::TemporalDebugView::Off;
    renderFrame(**device, **renderer, temporalCamera(), view);

    lmx::render::TransientPool transients(**device);
    CommandList& commands = (*device)->beginFrame();
    transients.beginFrame();
    lmx::render::RenderGraph graph(transients);
    graph.presentTexture((*renderer)->declarePasses(graph, commands, temporalCamera(), view));
    const auto record = graph.compileFrame(3);
    REQUIRE(record.has_value());
    bool sawRecycledSlot = false;
    for (const auto& transition : record->debug.transitions) {
        if (record->debug.resources[transition.resource].name == "lmx.render.historyColor0" &&
            transition.textureTo == TextureUse::CopyDestination) {
            sawRecycledSlot = true;
            CHECK(transition.textureFrom == TextureUse::ShaderRead);
        }
    }
    CHECK(sawRecycledSlot);
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
}

//======================================================================================================================
// Disabled diagnostic outputs retain a poison texel, including on reset frames. A bound fallback
// is still a real writable texture at (0,0); suppressing its graph declaration alone is not enough.
TEST_CASE("temporal resolve writes only enabled diagnostics", "[gpu][temporal][taa-diagnostics]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    auto library = (*device)->loadShaderLibrary("Shaders/TemporalResolve");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeTemporalResolve",
                                                      .threadsPerThreadgroup = {8, 8, 1},
                                                      .label = "lmx.test.diagnosticWritePipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());
    auto sampler = (*device)->createSampler({.filter = FilterMode::Linear,
                                             .addressMode = AddressMode::Clamp,
                                             .label = "lmx.test.diagnosticSampler"});
    REQUIRE(sampler.has_value());
    constexpr std::array<float, 2> kExposure{1.0f, 1.0f};
    auto exposure = (*device)->createBuffer(
        {.size = sizeof(kExposure), .storageRead = true, .label = "lmx.test.diagnosticExposure"},
        kExposure.data());
    REQUIRE(exposure.has_value());

    const auto makeTexel = [&](const std::array<uint16_t, 4>& texel, const char* label) {
        const TextureMip mip{.data = texel.data(), .bytesPerRow = sizeof(texel)};
        return (*device)->createTexture({.width = 1,
                                         .height = 1,
                                         .format = Format::RGBA16Float,
                                         .sampled = true,
                                         .storageWrite = true,
                                         .cpuReadback = true,
                                         .label = label},
                                        std::span{&mip, 1});
    };
    auto input = makeTexel({0, 0, 0, 0x3c00}, "lmx.test.diagnosticInput");
    REQUIRE(input.has_value());
    // Mirrors the resolve ABI; the identity cameras and zero depth keep this one texel valid.
    struct ResolveParams {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t historyValid = 0;
        uint32_t writeDiagnostics = 0;
        glm::mat4 inverseViewProjection{1.0f};
        glm::mat4 previousViewProjection{1.0f};
        float previousNearZ = 0.1f;
        float pad[3]{};
    };
    static_assert(sizeof(ResolveParams) == 160);
    constexpr std::array<uint16_t, 4> kPoison{0x4200, 0x4200, 0x4200, 0x4200};
    for (const uint32_t valid : {0u, 1u}) {
        for (const uint32_t mask : {0u, 1u, 2u, 3u}) {
            INFO("history valid " << valid << ", diagnostic mask " << mask);
            auto output = makeTexel(kPoison, "lmx.test.diagnosticResolved");
            auto rejection = makeTexel(kPoison, "lmx.test.diagnosticRejection");
            auto reprojected = makeTexel(kPoison, "lmx.test.diagnosticReprojected");
            REQUIRE(output.has_value());
            REQUIRE(rejection.has_value());
            REQUIRE(reprojected.has_value());
            CommandList& commands = (*device)->beginFrame();
            commands.beginComputePass("lmx.test.diagnosticWrites");
            commands.bindComputePipeline(**pipeline);
            for (uint32_t slot = 0; slot < 6; ++slot) {
                commands.bindTexture(slot, **input);
            }
            commands.bindStorageTexture(6, **output, {}, StorageAccess::Write);
            commands.bindStorageTexture(7, **rejection, {}, StorageAccess::Write);
            commands.bindStorageTexture(8, **reprojected, {}, StorageAccess::Write);
            commands.bindStorageBuffer(0, **exposure, StorageAccess::Read);
            commands.bindFrameData(1,
                                   ResolveParams{.historyValid = valid, .writeDiagnostics = mask});
            commands.bindSampler(0, **sampler);
            commands.dispatch(1, 1, 1);
            commands.endComputePass();
            (*device)->endFrame(nullptr);
            (*device)->waitIdle();
            std::array<uint16_t, 4> actual{};
            (*rejection)->readback(actual.data(), sizeof(actual));
            CHECK((actual != kPoison) == ((mask & 1u) != 0u));
            (*reprojected)->readback(actual.data(), sizeof(actual));
            CHECK((actual != kPoison) == ((mask & 2u) != 0u));
        }
    }
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

    // A render-scale change is not an extent change: the history lives at the output extent and is
    // reprojected in UV, so it survives and the frame reports the change instead of resetting.
    view.temporal.cameraCut = false;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    const uint64_t beforeChange = (*renderer)->temporalStatus().lastRenderExtentChangeFrame;

    view.temporal.renderScale = 0.5f;
    renderFrame(**device, **renderer, camera, view);
    const lmx::render::TemporalStatus scaled = (*renderer)->temporalStatus();
    REQUIRE(scaled.lastReset == HistoryResetReason::None);
    REQUIRE(scaled.historyValid);
    REQUIRE(scaled.upscaled);
    REQUIRE(scaled.extents.renderWidth == scaled.extents.outputWidth / 2);
    REQUIRE(scaled.lastRenderExtentChangeFrame > beforeChange);
    // Nothing was reallocated: the output extent is what the targets are sized by.
    REQUIRE(scaled.historyBytes == historyBytes / 4);

    // Holding the scale leaves the count where the change put it.
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastRenderExtentChangeFrame ==
            scaled.lastRenderExtentChangeFrame);
}

//======================================================================================================================
// The diagnostic's motion texel is the one its current-colour sample falls in, which below scale 1
// is not the texel the output pixel's own corner maps to. A vertical split between a surface whose
// motion is the invalid sentinel and a static one puts the two mappings on opposite sides of the
// seam for a whole column of output pixels: taking motion from the corner reports "nothing to
// compare" for pixels that sample the static surface, and compares pixels that sample the invalid
// one. The expected texel is computed on the CPU with Source/Render/Temporal.h's
// renderSamplePosition, so what the case pins is the mapping and not a hand-picked pixel.
TEST_CASE("the reprojection diagnostic reads motion at the sampled texel", "[gpu][temporal]") {
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

    constexpr float kScale = 0.75f;
    const lmx::render::FrameExtents extents =
        lmx::render::renderExtentsForScale(kSize, kSize, kScale);
    REQUIRE(extents.renderWidth == 48);
    REQUIRE(extents.renderHeight == 48);

    // The seam sits on the boundary between render texels 21 and 22, a column where the corner
    // mapping and the sample mapping disagree: output pixel 29 samples at 22.125, inside the
    // static surface, while its corner maps to 21.75, inside the invalid one.
    constexpr uint32_t kSeamTexel = 22;
    constexpr float kPlaneZ = -2.0f;
    constexpr float kHalfExtent = 10.0f;
    const Camera camera;
    const float ndcX =
        2.0f * static_cast<float>(kSeamTexel) / static_cast<float>(extents.renderWidth) - 1.0f;
    const float seamX = ndcX * std::tan(camera.fovY * 0.5f) * -kPlaneZ;

    const glm::mat4 invalid =
        glm::translate(glm::mat4{1.0f}, glm::vec3{seamX - kHalfExtent, 0.0f, 0.0f}) *
        facingPlaneModel(kPlaneZ);
    const glm::mat4 stat =
        glm::translate(glm::mat4{1.0f}, glm::vec3{seamX + kHalfExtent, 0.0f, 0.0f}) *
        facingPlaneModel(kPlaneZ);
    const std::array<DrawItem, 2> items = {
        DrawItem{.mesh = &*plane,
                 .model = invalid,
                 .previousModel = invalid,
                 .motionClass = lmx::render::MotionClass::Invalid},
        DrawItem{.mesh = &*plane, .model = stat, .previousModel = stat},
    };
    SceneView view = temporalSceneView(items);
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = false; // The mapping under test, without a sub-pixel offset.
    view.temporal.renderScale = kScale;
    view.temporal.debugView = lmx::render::TemporalDebugView::ReprojectionError;

    renderFrame(**device, **renderer, camera, view);
    renderFrame(**device, **renderer, camera, view);
    renderFrame(**device, **renderer, camera, view);
    const lmx::render::TemporalStatus status = (*renderer)->temporalStatus();
    REQUIRE(status.lastReset == lmx::render::HistoryResetReason::None);
    REQUIRE(status.historyValid);
    REQUIRE(status.extents.renderWidth == extents.renderWidth);

    const std::vector<uint8_t> motion = readMotion(**renderer);
    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(pixels.data(), pixels.size());

    // The view paints "nothing to compare" -- the diagnostic's alpha 0 -- flat blue, and every
    // comparable pixel a grey.
    const auto sentinelAt = [&motion](glm::ivec2 texel) {
        const glm::vec2 value =
            motionAt(motion, static_cast<uint32_t>(texel.x), static_cast<uint32_t>(texel.y));
        return std::isinf(value.x) || std::isinf(value.y);
    };
    const auto texelFor = [&extents](uint32_t x, uint32_t y) {
        const glm::vec2 position =
            lmx::render::renderSamplePosition({x, y}, extents, glm::vec2{0.0f});
        return glm::ivec2{glm::clamp(static_cast<int>(std::floor(position.x)), 0,
                                     static_cast<int>(extents.renderWidth) - 1),
                          glm::clamp(static_cast<int>(std::floor(position.y)), 0,
                                     static_cast<int>(extents.renderHeight) - 1)};
    };

    uint32_t comparable = 0;
    uint32_t rejected = 0;
    uint32_t distinguishing = 0;
    uint32_t mismatched = 0;
    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            const glm::ivec2 texel = texelFor(x, y);
            const bool expectSentinel = sentinelAt(texel);
            const Pixel pixel = pixelAt(pixels, x, y);
            const bool sawSentinel = pixel.r == 0 && pixel.g == 0 && pixel.b == 255;
            // The corner mapping this case exists to reject, and whether it would answer
            // differently here.
            const glm::ivec2 corner{
                glm::clamp(static_cast<int>(static_cast<float>(x) * static_cast<float>(kScale)), 0,
                           static_cast<int>(extents.renderWidth) - 1),
                glm::clamp(static_cast<int>(static_cast<float>(y) * static_cast<float>(kScale)), 0,
                           static_cast<int>(extents.renderHeight) - 1)};
            if (sentinelAt(corner) != expectSentinel) {
                ++distinguishing;
            }
            if (expectSentinel) {
                ++rejected;
            } else {
                ++comparable;
            }
            if (sawSentinel != expectSentinel) {
                ++mismatched;
                INFO(describe("reprojection error", x, y, pixel));
                INFO("sample texel (" + std::to_string(texel.x) + "," + std::to_string(texel.y) +
                     ") sentinel " + (expectSentinel ? "yes" : "no"));
                CHECK(sawSentinel == expectSentinel);
            }
        }
    }
    INFO("comparable " + std::to_string(comparable) + " rejected " + std::to_string(rejected) +
         " distinguishing " + std::to_string(distinguishing));
    REQUIRE(comparable > 0);
    REQUIRE(rejected > 0);
    // Without these the case would pass under either mapping and prove nothing.
    REQUIRE(distinguishing > 0);
    REQUIRE(mismatched == 0);
}

//======================================================================================================================
// The age's own bookkeeping across the two events that are not resets and the one that is. A frame
// with temporal off does not merely fail to advance the count -- it starts it over, because the
// history the next temporal frame finds is not the one the count described; a change of
// reconstruction mode does the opposite, because both modes leave a real frame in the colour slot.
// Status values only: what is asserted here is the counter, not the picture.
TEST_CASE("history age restarts across temporal off and survives a mode switch",
          "[gpu][temporal]") {
    using namespace lmx::rhi;
    using lmx::render::HistoryResetReason;
    using lmx::render::ReconstructionMode;

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
    view.temporal.reconstruction = ReconstructionMode::NativeTaa;

    // Three accumulated frames: the first is the reset that starts the count at 1.
    for (uint32_t frame = 1; frame <= 3; ++frame) {
        renderFrame(**device, **renderer, camera, view);
        REQUIRE((*renderer)->temporalStatus().historyAge == frame);
    }
    REQUIRE((*renderer)->temporalStatus().historyAge >= 3);
    REQUIRE_FALSE((*renderer)->temporalStatus().warmupComplete);

    // Temporal off: no history is kept, so nothing is counted.
    view.temporal.enabled = false;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().historyAge == 0);
    REQUIRE_FALSE((*renderer)->temporalStatus().warmupComplete);
    REQUIRE_FALSE((*renderer)->temporalStatus().historyValid);

    // Re-enabling is a reset, so the count starts over at the reset frame's own 1.
    view.temporal.enabled = true;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::TemporalEnabled);
    REQUIRE((*renderer)->temporalStatus().historyAge == 1);

    // A change of reconstruction is not a reset: the raw commit leaves a real frame in the slot,
    // so the count keeps running through it and through the switch back.
    view.temporal.reconstruction = ReconstructionMode::Raw;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().reconstruction == ReconstructionMode::Raw);
    REQUIRE((*renderer)->temporalStatus().historyAge == 2);

    view.temporal.reconstruction = ReconstructionMode::NativeTaa;
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    REQUIRE((*renderer)->temporalStatus().reconstruction == ReconstructionMode::NativeTaa);
    REQUIRE((*renderer)->temporalStatus().historyAge == 3);
    REQUIRE_FALSE((*renderer)->temporalStatus().warmupComplete);
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
// A constant radiance stays constant under a fractional history fetch. Inspect the reprojected
// signal before neighbourhood clipping, which would hide a sampler's energy loss on a flat patch.
TEST_CASE("subpixel diagonal motion preserves constant reprojected radiance",
          "[gpu][temporal][taa-reprojection]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.constantPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());
    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    constexpr float kPlaneZ = -2.0f;
    const glm::mat4 model = facingPlaneModel(kPlaneZ);
    const std::array<DrawItem, 1> items = {
        DrawItem{.mesh = &*plane,
                 .model = model,
                 .material = {.albedo = {0.0f, 0.0f, 0.0f, 1.0f}, .emissive = {0.5f, 0.5f, 0.5f}},
                 .previousModel = model}};
    SceneView view = temporalSceneView(items);
    for (auto& light : view.lights) {
        light.strength = {};
    }
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = false;
    view.temporal.reconstruction = lmx::render::ReconstructionMode::NativeTaa;
    view.temporal.debugView = lmx::render::TemporalDebugView::ReprojectedHistory;
    Camera camera;
    renderFrame(**device, **renderer, camera, view);
    renderFrame(**device, **renderer, camera, view);
    std::vector<uint8_t> stationary(size_t{kSize} * kSize * 4);
    (*renderer)->colorTarget().readback(stationary.data(), stationary.size());
    const Pixel expected = pixelAt(stationary, kSize / 2, kSize / 2);
    REQUIRE(expected.r > 0);
    REQUIRE(expected.r == expected.g);
    REQUIRE(expected.g == expected.b);

    const float halfPixel = -kPlaneZ * std::tan(camera.fovY * 0.5f) / float(kSize);
    camera.position = {halfPixel, halfPixel, 0.0f};
    renderFrame(**device, **renderer, camera, view);
    const glm::vec2 motion = motionAt(readMotion(**renderer), kSize / 2, kSize / 2);
    REQUIRE(std::abs(motion.x) * float(kSize) == Catch::Approx(0.5f).margin(1e-3));
    REQUIRE(std::abs(motion.y) * float(kSize) == Catch::Approx(0.5f).margin(1e-3));

    std::vector<uint8_t> moving(stationary.size());
    (*renderer)->colorTarget().readback(moving.data(), moving.size());
    const Pixel actual = pixelAt(moving, kSize / 2, kSize / 2);
    INFO(describe("stationary radiance", kSize / 2, kSize / 2, expected));
    INFO(describe("reprojected radiance", kSize / 2, kSize / 2, actual));
    REQUIRE(actual.r == expected.r);
    REQUIRE(actual.g == expected.g);
    REQUIRE(actual.b == expected.b);
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

    // Five accumulated frames over the same two pairs, still undrained, with the debug view
    // switched on, off and on again: the resolve reads the other slot of both pairs while writing
    // this one, so the imports have to name what the frame two back actually left there rather than
    // what either mode alone would have.
    view.temporal.reconstruction = lmx::render::ReconstructionMode::NativeTaa;
    for (uint32_t frame = 0; frame < 5; ++frame) {
        view.temporal.debugView =
            frame % 2 == 0 ? TemporalDebugView::RejectionMask : TemporalDebugView::Off;
        renderFrameInFlight(**device, **renderer, camera, view);
    }
    REQUIRE((*renderer)->temporalStatus().reconstruction ==
            lmx::render::ReconstructionMode::NativeTaa);

    // Five more, still undrained, alternating the render scale with the debug view: an upscaled
    // frame declares a different reconstruction pass and a different terminal use for the scene
    // colour than the frame before and after it, so the imports have to name what each of those
    // frames actually left rather than what one scale alone would have.
    for (uint32_t frame = 0; frame < 5; ++frame) {
        view.temporal.renderScale = frame % 2 == 0 ? 0.5f : 1.0f;
        view.temporal.debugView =
            frame % 2 == 0 ? TemporalDebugView::MotionVectors : TemporalDebugView::Off;
        renderFrameInFlight(**device, **renderer, camera, view);
        REQUIRE((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
    }
    REQUIRE((*renderer)->temporalStatus().upscaled);

    // The one drain, after every submission: the targets were never reallocated, so the run's own
    // completion is the assertion the overlap makes.
    (*device)->waitIdle();
    REQUIRE((*renderer)->temporalStatus().historyBytes == historyBytes);
    REQUIRE((*renderer)->temporalStatus().depthHistoryBytes > 0);
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

// The scripted stability scenarios (M6.2 spec 11). They run at a 16:9 extent rather than the
// 64x64 the declaration cases use, because thin geometry and a few pixels of motion per frame only
// mean something against a real aspect ratio, and they read back this frame's colour slot -- which
// holds the mode's own output either way -- beside the raw HDR target it was built from.
namespace {

constexpr uint32_t kScenarioWidth = 320;
constexpr uint32_t kScenarioHeight = 180;
constexpr size_t kScenarioPixels = size_t{kScenarioWidth} * kScenarioHeight;
// The tolerances are frozen before measurement; measurement itself starts after one jitter period.
constexpr uint32_t kMeasureFirstFrame = 17;

using lmx::render::ReconstructionMode;
using lmx::render::TemporalDebugView;

//======================================================================================================================
// One RGBA16Float target, widened to floats. Both the colour history and the raw scene target are
// half-precision, so a comparison between them has to speak the same units the shader wrote.
std::vector<glm::vec4> readHalf4(lmx::rhi::Texture& texture) {
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
// The flat colour Shaders/TemporalDebugView.slang draws the RejectionMask in for a reason code.
// One mirror of the shader's legend, so a case names the reason it expects by the shared constant
// in Render/TemporalResolve.h and never by a colour spelled out at the assertion.
//
// The clipped flag adds half a unit of green on top, which is why a case comparing all three
// channels has to expect a reason whose green is already saturated or absent.
glm::vec3 rejectionMaskColor(uint32_t reason) {
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
glm::vec3 displayRgb(const std::vector<uint8_t>& display, uint32_t index) {
    return {display[index * 4 + 2] / 255.0f, display[index * 4 + 1] / 255.0f,
            display[index * 4] / 255.0f};
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
// The mean luminance of a sequence's own output over the measured window, per pixel of `pixels`.
// It is what "mean Y over frames 17-32" is measured as, for both sides of a comparison between two
// sequences.
float meanOverWindow(const std::vector<ScenarioFrame>& frames,
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
// Static stability (spec 11): a checker floor and five poles, camera still, jitter on, 32 frames.
// Jitter moves where every frame is sampled, so the raw picture keeps changing at every edge the
// checker's perspective compresses toward the horizon; accumulation is what is supposed to settle
// that without the scene having moved at all.
TEST_CASE("accumulation settles a static jittered frame", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<DrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const float poleWidth = 2.0f / pixelsPerUnit(camera, 12.0f);
    for (int32_t i = -2; i <= 2; ++i) {
        items.push_back(staticItem(
            *cube,
            boxModel({static_cast<float>(i) * 1.5f, 1.5f, -8.0f}, {poleWidth, 3.0f, poleWidth}),
            {1.0f, 1.0f, 1.0f, 1.0f}));
    }
    const SceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});

    auto rawRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    INFO(errorOf(rawRenderer));
    REQUIRE(rawRenderer.has_value());
    const std::vector<ScenarioFrame> raw =
        renderSequence(**device, **rawRenderer, 32, ReconstructionMode::Raw, base, camera,
                       TemporalDebugView::Off, nullptr);

    auto taaRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    INFO(errorOf(taaRenderer));
    REQUIRE(taaRenderer.has_value());
    const std::vector<ScenarioFrame> taa =
        renderSequence(**device, **taaRenderer, 32, ReconstructionMode::NativeTaa, base, camera,
                       TemporalDebugView::Off, nullptr);

    const std::vector<uint32_t> pixels = allPixels();
    const float rawChange = frameToFrameChange(raw, pixels);
    const float taaChange = frameToFrameChange(taa, pixels);
    INFO("raw frame-to-frame |dY| " << rawChange << ", TAA " << taaChange);
    // A raw sequence that does not shimmer would make the ratio vacuous, so the scene has to be
    // one that actually moves under jitter before the ratio says anything.
    REQUIRE(rawChange > 1e-4f);
    REQUIRE(taaChange <= 0.25f * rawChange);
    REQUIRE(taaChange <= 0.005f);
    REQUIRE(taa.back().status.warmupComplete);
}

//======================================================================================================================
// Thin geometry (spec 11): five poles two pixels wide over a dark floor. A pixel on a pole is
// covered only on the jitter samples that happen to land inside it, so the accumulation has to
// carry the pole's brightness across the samples that miss it rather than averaging it away.
TEST_CASE("accumulation keeps thin geometry's brightness", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    // Looking down at the floor, with the poles standing on it and entirely below the horizon:
    // "over the floor" is what puts real geometry behind every pole pixel, so an uncovered sample
    // reprojects onto the floor rather than onto the empty background a silhouette against the sky
    // would leave -- which the disocclusion test must reject, and rightly does.
    const Camera camera = scenarioCamera({0.0f, 2.0f, 4.0f}, -0.16f);
    const float poleWidth = 2.0f / pixelsPerUnit(camera, 14.0f);
    std::vector<DrawItem> items;
    items.push_back(staticItem(*plane, glm::mat4{1.0f}, {0.30f, 0.30f, 0.30f, 1.0f}));
    for (int32_t i = -2; i <= 2; ++i) {
        items.push_back(staticItem(
            *cube,
            boxModel({static_cast<float>(i) * 1.5f, 0.25f, -10.0f}, {poleWidth, 0.5f, poleWidth}),
            {1.0f, 1.0f, 1.0f, 1.0f}));
    }
    // Travelling -Z: the poles' camera-facing sides take the light and the floor's upward normal
    // takes none of it, so a pole pixel is the only thing a luminance threshold can find.
    const SceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});

    auto rawRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(rawRenderer.has_value());
    const std::vector<ScenarioFrame> raw =
        renderSequence(**device, **rawRenderer, 32, ReconstructionMode::Raw, base, camera,
                       TemporalDebugView::Off, nullptr);
    auto taaRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(taaRenderer.has_value());
    const std::vector<ScenarioFrame> taa =
        renderSequence(**device, **taaRenderer, 32, ReconstructionMode::NativeTaa, base, camera,
                       TemporalDebugView::Off, nullptr);

    // A pole pixel is one the poles covered on at least one measured jitter sample; the floor never
    // reaches this luminance under a light that grazes it.
    constexpr float kPoleThreshold = 0.02f;
    std::vector<uint32_t> polePixels;
    std::vector<float> rawSum(kScenarioPixels, 0.0f);
    for (size_t frame = kMeasureFirstFrame - 1; frame < raw.size(); ++frame) {
        const std::vector<float> y = luminances(raw[frame].history);
        for (size_t i = 0; i < y.size(); ++i) {
            rawSum[i] += y[i];
        }
    }
    const auto measuredFrames = static_cast<float>(raw.size() - (kMeasureFirstFrame - 1));
    for (uint32_t i = 0; i < kScenarioPixels; ++i) {
        if (rawSum[i] / measuredFrames > kPoleThreshold) {
            polePixels.push_back(i);
        }
    }
    INFO("pole pixels " << polePixels.size());
    REQUIRE(polePixels.size() > 50);

    const float rawMean = meanOver(rawSum, polePixels) / measuredFrames;
    const float taaMean = meanOver(luminances(taa.back().history), polePixels);
    INFO("raw mean Y " << rawMean << ", TAA mean Y " << taaMean);
    REQUIRE(taaMean >= 0.75f * rawMean);

    const float rawChange = frameToFrameChange(raw, polePixels);
    const float taaChange = frameToFrameChange(taa, polePixels);
    INFO("raw pole |dY| " << rawChange << ", TAA " << taaChange);
    REQUIRE(rawChange > 1e-4f);
    REQUIRE(taaChange <= 0.25f * rawChange);
}

//======================================================================================================================
// Motion and ghosting (spec 11): a quad translating four pixels a frame over a backdrop. The band
// it vacates is the classic trail -- history that describes the quad blended over a background the
// quad has left -- so the region has to converge on the raw picture rather than on what was there,
// and the rejection mask has to say why in the pixels that were vacated this frame.
TEST_CASE("a moving quad leaves no trail behind it", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(32.0f), "lmx.test.scenarioWall");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 0.0f, 4.0f}, 0.0f);
    // Camera z 4 to quad z -6; the backdrop sits six units further back, which is the depth gap
    // the disocclusion test has to see in the band the quad vacates.
    constexpr float kQuadDistance = 10.0f;
    constexpr float kQuadHalfWidth = 1.0f;
    constexpr float kStartX = -3.0f;
    const float stepX = 4.0f / pixelsPerUnit(camera, kQuadDistance);

    std::vector<DrawItem> items;
    items.push_back(staticItem(*plane, facingPlaneModel(-12.0f), {0.6f, 0.6f, 0.6f, 1.0f}));
    items.push_back(staticItem(*cube, boxModel({kStartX, 0.0f, -6.0f}, {2.0f, 2.0f, 0.1f}),
                               {0.05f, 0.05f, 0.05f, 1.0f}));

    const auto quadCenterX = [&](uint32_t frame) {
        return kStartX + stepX * static_cast<float>(frame - 1);
    };
    const PerFrame animate = [&](uint32_t frame, SceneView& view, Camera&) {
        auto* drawn = const_cast<DrawItem*>(view.items.data());
        drawn[1].model = boxModel({quadCenterX(frame), 0.0f, -6.0f}, {2.0f, 2.0f, 0.1f});
        drawn[1].previousModel =
            boxModel({quadCenterX(frame == 1 ? 1 : frame - 1), 0.0f, -6.0f}, {2.0f, 2.0f, 0.1f});
    };
    // Facing the camera, so both the quad and the backdrop take the light and the two differ by
    // albedo alone -- which is what makes the luminance contrast a number the tolerance can scale.
    const SceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});

    constexpr uint32_t kFrames = 28;
    auto rawRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(rawRenderer.has_value());
    const std::vector<ScenarioFrame> raw =
        renderSequence(**device, **rawRenderer, kFrames, ReconstructionMode::Raw, base, camera,
                       TemporalDebugView::Off, animate);
    // The debug view replaces the display target and nothing else, so one accumulated run answers
    // both halves: its colour slot is the picture, its display target the rejection mask.
    auto taaRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(taaRenderer.has_value());
    const std::vector<ScenarioFrame> taa =
        renderSequence(**device, **taaRenderer, kFrames, ReconstructionMode::NativeTaa, base,
                       camera, TemporalDebugView::RejectionMask, animate);

    // The quad's screen span, from the same projection the scene was authored against.
    const auto quadColumns = [&](uint32_t frame) {
        const float center = quadCenterX(frame);
        return std::pair<int32_t, int32_t>{
            static_cast<int32_t>(
                std::ceil(pixelForWorldX(camera, center - kQuadHalfWidth, kQuadDistance))),
            static_cast<int32_t>(
                std::floor(pixelForWorldX(camera, center + kQuadHalfWidth, kQuadDistance)))};
    };
    // Rows well inside the quad, so the measured band is vacated backdrop rather than the quad's
    // own top and bottom edges.
    const auto [currentLeft, currentRight] = quadColumns(kFrames);
    const auto [previousLeft, previousRight] = quadColumns(kFrames - 1);
    const auto [olderLeft, olderRight] = quadColumns(kFrames - 2);
    REQUIRE(previousLeft < currentLeft);
    const float quadHalfHeightPixels = kQuadHalfWidth * pixelsPerUnit(camera, kQuadDistance);
    const int32_t rowFirst =
        static_cast<int32_t>(0.5f * kScenarioHeight - quadHalfHeightPixels) + 2;
    const int32_t rowLast = static_cast<int32_t>(0.5f * kScenarioHeight + quadHalfHeightPixels) - 2;

    std::vector<uint32_t> vacatedThisFrame;
    std::vector<uint32_t> vacatedRecently;
    for (int32_t row = rowFirst; row <= rowLast; ++row) {
        for (int32_t column = olderLeft; column < currentLeft; ++column) {
            if (column < 0 || column >= static_cast<int32_t>(kScenarioWidth)) {
                continue;
            }
            const auto index = static_cast<uint32_t>(row * kScenarioWidth + column);
            vacatedRecently.push_back(index);
            if (column >= previousLeft) {
                vacatedThisFrame.push_back(index);
            }
        }
    }
    INFO("vacated this frame " << vacatedThisFrame.size() << ", recently "
                               << vacatedRecently.size());
    REQUIRE(vacatedThisFrame.size() > 20);
    REQUIRE(vacatedRecently.size() > vacatedThisFrame.size());

    // The contrast the tolerance is stated against: the quad's own luminance beside the backdrop's,
    // both read out of the raw frame rather than derived from the material.
    const std::vector<float> rawY = luminances(raw[kFrames - 1].history);
    const auto quadSample = static_cast<uint32_t>((kScenarioHeight / 2) * kScenarioWidth +
                                                  (currentLeft + currentRight) / 2);
    const auto wallSample = static_cast<uint32_t>((kScenarioHeight / 2) * kScenarioWidth + 4);
    const float contrast = std::abs(rawY[quadSample] - rawY[wallSample]);
    INFO("quad-to-backdrop contrast " << contrast);
    REQUIRE(contrast > 0.05f);

    const std::vector<float> taaY = luminances(taa[kFrames - 1].history);
    const float trail = meanAbsDiff(taaY, rawY, vacatedRecently);
    INFO("mean |Y(TAA) - Y(Raw)| in the vacated region " << trail);
    REQUIRE(trail <= 0.1f * contrast);

    // Disoccluded is drawn flat red, with the clipped flag adding green; the mask is written into
    // the display target unencoded, so the channel test is on the code the shader chose.
    uint32_t disoccluded = 0;
    for (uint32_t index : vacatedThisFrame) {
        const uint8_t blue = taa[kFrames - 1].display[index * 4];
        const uint8_t red = taa[kFrames - 1].display[index * 4 + 2];
        if (red > 200 && blue < 50) {
            ++disoccluded;
        }
    }
    INFO("disoccluded share " << static_cast<float>(disoccluded) / vacatedThisFrame.size());
    REQUIRE(disoccluded * 2 >= vacatedThisFrame.size());
}

//======================================================================================================================
// Emissive step (spec 11): a quad whose emissive strength steps from 0 to 4 between two frames. It
// is the change no motion vector can describe -- the surface did not move, its radiance did -- so
// the reactive attachment is what has to make the pixel take this frame's colour instead of fading
// the step in over the accumulation's time constant.
TEST_CASE("a reactive emissive step lands without a fade", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(32.0f), "lmx.test.scenarioWall");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 0.0f, 4.0f}, 0.0f);
    std::vector<DrawItem> items;
    items.push_back(staticItem(*plane, facingPlaneModel(-12.0f), {0.4f, 0.4f, 0.4f, 1.0f}));
    items.push_back(staticItem(*cube, boxModel({0.0f, 0.0f, -6.0f}, {2.0f, 2.0f, 0.1f}),
                               {0.05f, 0.05f, 0.05f, 1.0f}));

    constexpr uint32_t kStepFrame = 24;
    const PerFrame animate = [](uint32_t frame, SceneView& view, Camera&) {
        auto* drawn = const_cast<DrawItem*>(view.items.data());
        drawn[1].material.emissive = frame >= kStepFrame ? glm::vec3{4.0f} : glm::vec3{0.0f};
    };
    const SceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});

    auto rawRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(rawRenderer.has_value());
    const std::vector<ScenarioFrame> raw =
        renderSequence(**device, **rawRenderer, kStepFrame + 1, ReconstructionMode::Raw, base,
                       camera, TemporalDebugView::Off, animate);
    // The debug view replaces the display target and nothing else, so the accumulated run answers
    // both halves at once: its colour slot is the picture the frozen tolerances measure, its
    // display target the reason the resolve chose. A second run reads the weight the same way.
    auto taaRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(taaRenderer.has_value());
    const std::vector<ScenarioFrame> taa =
        renderSequence(**device, **taaRenderer, kStepFrame + 1, ReconstructionMode::NativeTaa, base,
                       camera, TemporalDebugView::RejectionMask, animate);
    auto weightRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(weightRenderer.has_value());
    const std::vector<ScenarioFrame> weights =
        renderSequence(**device, **weightRenderer, kStepFrame + 1, ReconstructionMode::NativeTaa,
                       base, camera, TemporalDebugView::BlendWeight, animate);

    // A block at the quad's centre, far enough inside that no jittered edge reaches it.
    std::vector<uint32_t> center;
    constexpr int32_t kCenterRow = kScenarioHeight / 2;
    constexpr int32_t kCenterColumn = kScenarioWidth / 2;
    for (int32_t row = kCenterRow - 8; row <= kCenterRow + 8; ++row) {
        for (int32_t column = kCenterColumn - 8; column <= kCenterColumn + 8; ++column) {
            center.push_back(static_cast<uint32_t>(row * kScenarioWidth + column));
        }
    }

    const float rawStep = meanOver(luminances(raw[kStepFrame - 1].history), center);
    const float taaStep = meanOver(luminances(taa[kStepFrame - 1].history), center);
    INFO("step frame raw Y " << rawStep << ", TAA Y " << taaStep);
    REQUIRE(rawStep > 1.0f);
    REQUIRE(taaStep >= 0.9f * rawStep);

    const float rawAfter = meanOver(luminances(raw[kStepFrame].history), center);
    const float taaAfter = meanOver(luminances(taa[kStepFrame].history), center);
    INFO("frame after raw Y " << rawAfter << ", TAA Y " << taaAfter);
    REQUIRE(taaAfter >= 0.95f * rawAfter);

    // Why it landed, not just that it did. A uniformly emissive quad has a uniform neighbourhood,
    // so the clip box collapses onto this frame's colour and would reach the same luminance with
    // the reactive attachment dead -- which is exactly the state a missing pipeline format left it
    // in. Naming the reason and the weight is what tells the two apart.
    const glm::vec3 expected = rejectionMaskColor(lmx::render::kRejectionReasonReactive);
    for (uint32_t index : center) {
        const glm::vec3 actual = displayRgb(taa[kStepFrame - 1].display, index);
        INFO("rejection mask at pixel " << index << " = " << actual.r << "," << actual.g << ","
                                        << actual.b);
        // Reactive's green is already saturated, so the clipped flag's half unit cannot move it.
        REQUIRE(actual == expected);
        // Rejection drives the blend to this frame alone; 255 is the weight's one exact value.
        REQUIRE(weights[kStepFrame - 1].display[index * 4] == 255);
    }
}

//======================================================================================================================
// Manual EV step (spec 11): the slider moves two stops between two frames. Every history texel then
// describes a different brightness than the frame it is blended with, and the exposure pair the
// seed records is what the resolve corrects it by -- without which the accumulated picture would
// lag the raw one by the accumulation's whole time constant.
TEST_CASE("an exposure step is corrected in the history it blends", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<DrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const SceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});

    constexpr uint32_t kStepFrame = 24;
    const PerFrame animate = [](uint32_t frame, SceneView& view, Camera&) {
        view.exposureEv = frame >= kStepFrame ? 2.0f : 0.0f;
    };

    auto rawRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(rawRenderer.has_value());
    const std::vector<ScenarioFrame> raw =
        renderSequence(**device, **rawRenderer, kStepFrame, ReconstructionMode::Raw, base, camera,
                       TemporalDebugView::Off, animate);
    auto taaRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(taaRenderer.has_value());
    const std::vector<ScenarioFrame> taa =
        renderSequence(**device, **taaRenderer, kStepFrame, ReconstructionMode::NativeTaa, base,
                       camera, TemporalDebugView::Off, animate);

    const std::vector<uint32_t> pixels = allPixels();
    const float rawMean = meanOver(luminances(raw.back().history), pixels);
    const float taaMean = meanOver(luminances(taa.back().history), pixels);
    INFO("raw mean Y " << rawMean << ", TAA mean Y " << taaMean);
    REQUIRE(rawMean > 0.0f);
    REQUIRE(std::abs(taaMean - rawMean) <= 0.02f * rawMean);
}

//======================================================================================================================
// Reset and warmup (spec 11): an explicit camera cut throws the history away, so the frame that
// raises it can only show what it shaded; the count then climbs one declared frame at a time until
// the accumulation is a full jitter period old.
TEST_CASE("a camera cut restarts the accumulation and its warmup", "[gpu][temporal]") {
    using namespace lmx::rhi;
    using lmx::render::HistoryResetReason;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<DrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const SceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});

    constexpr uint32_t kCutFrame = 20;
    constexpr uint32_t kFrames = kCutFrame + lmx::render::kTemporalWarmupFrames;
    const PerFrame animate = [](uint32_t frame, SceneView& view, Camera&) {
        view.temporal.cameraCut = frame == kCutFrame;
    };

    auto renderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(renderer.has_value());
    const std::vector<ScenarioFrame> frames =
        renderSequence(**device, **renderer, kFrames, ReconstructionMode::NativeTaa, base, camera,
                       TemporalDebugView::Off, animate);

    const ScenarioFrame& cut = frames[kCutFrame - 1];
    REQUIRE(cut.status.lastReset == HistoryResetReason::CameraCut);
    REQUIRE(cut.status.historyAge == 1);
    REQUIRE_FALSE(cut.status.warmupComplete);
    // Bit-exact, not near: a reset frame writes what it shaded, so the two halves are the same
    // half-precision values rather than a blend that happens to be close.
    for (size_t i = 0; i < cut.history.size(); ++i) {
        if (cut.history[i].r != cut.raw[i].r || cut.history[i].g != cut.raw[i].g ||
            cut.history[i].b != cut.raw[i].b) {
            INFO("pixel " << i << " differs on the cut frame");
            REQUIRE(false);
        }
    }
    // The per-pixel count in the alpha channel restarts with the CPU one.
    REQUIRE(cut.history[0].a == 1.0f);

    // The 16th declared frame counting the cut itself is the first with a full period behind it.
    const uint32_t lastWarming = kCutFrame + lmx::render::kTemporalWarmupFrames - 2;
    REQUIRE(frames[lastWarming - 1].status.historyAge == lmx::render::kTemporalWarmupFrames - 1);
    REQUIRE_FALSE(frames[lastWarming - 1].status.warmupComplete);
    REQUIRE(frames[lastWarming].status.historyAge == lmx::render::kTemporalWarmupFrames);
    REQUIRE(frames[lastWarming].status.warmupComplete);
}

//======================================================================================================================
// A mode switch is not a reset (spec 4): both modes leave a real frame in the colour slot, so the
// first accumulated frame after a raw one derives no reason at all and blends from the copy the raw
// frame committed.
TEST_CASE("switching from raw to native TAA needs no reset", "[gpu][temporal]") {
    using namespace lmx::rhi;
    using lmx::render::HistoryResetReason;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<DrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    SceneView view = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;

    auto renderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(renderer.has_value());

    view.temporal.reconstruction = ReconstructionMode::Raw;
    for (uint32_t frame = 0; frame < 4; ++frame) {
        renderFrame(**device, **renderer, camera, view);
    }
    REQUIRE((*renderer)->temporalStatus().reconstruction == ReconstructionMode::Raw);
    const uint32_t ageBeforeSwitch = (*renderer)->temporalStatus().historyAge;
    const std::vector<glm::vec4> rawCommit = readHalf4(*(*renderer)->historyTarget());

    view.temporal.reconstruction = ReconstructionMode::NativeTaa;
    renderFrame(**device, **renderer, camera, view);
    const lmx::render::TemporalStatus status = (*renderer)->temporalStatus();
    REQUIRE(status.lastReset == HistoryResetReason::None);
    REQUIRE(status.reconstruction == ReconstructionMode::NativeTaa);
    // The count keeps running across the switch: neither mode threw a history away.
    REQUIRE(status.historyAge == ageBeforeSwitch + 1);

    // The first accumulated frame is a blend, not a copy: it differs from its own raw frame, and it
    // moved toward it from what the raw commit had left in the other slot.
    const std::vector<glm::vec4> blended = readHalf4(*(*renderer)->historyTarget());
    const std::vector<glm::vec4> raw = readHalf4((*renderer)->hdrColorTarget());
    const std::vector<uint32_t> pixels = allPixels();
    const float towardRaw = meanAbsDiff(luminances(blended), luminances(raw), pixels);
    const float rawToCommit = meanAbsDiff(luminances(raw), luminances(rawCommit), pixels);
    INFO("blended-to-raw " << towardRaw << ", raw-to-previous-commit " << rawToCommit);
    REQUIRE(towardRaw > 0.0f);
    REQUIRE(towardRaw < rawToCommit);
}

//======================================================================================================================
// The raw bypass (spec 11): with jitter off, Raw declares the temporal inputs, commits a history
// nothing reads back and shows the pre-temporal picture. It is the reference the accumulated mode
// is compared against, so it has to be the pre-temporal image and not merely close to it.
TEST_CASE("the raw bypass matches the temporal-off picture", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<DrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const SceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});

    const auto capture = [&](bool temporal) {
        auto renderer =
            Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
        REQUIRE(renderer.has_value());
        SceneView view = base;
        view.temporal.enabled = temporal;
        view.temporal.jitterEnabled = false;
        view.temporal.reconstruction = ReconstructionMode::Raw;
        renderFrame(**device, **renderer, camera, view);
        std::vector<uint8_t> display(kScenarioPixels * 4);
        (*renderer)->colorTarget().readback(display.data(), display.size());
        return display;
    };

    const std::vector<uint8_t> off = capture(false);
    const std::vector<uint8_t> raw = capture(true);
    REQUIRE(off.size() == raw.size());
    uint32_t differing = 0;
    for (size_t i = 0; i < off.size(); ++i) {
        const int delta = std::abs(int{off[i]} - int{raw[i]});
        REQUIRE(delta <= 1);
        differing += delta != 0 ? 1u : 0u;
    }
    INFO("channels differing by one LSB " << differing);
}

//======================================================================================================================
// Upscaled static stability (M6.3 spec 10): the checker floor and poles at half of each axis,
// camera still, 32 frames. The spatial upscale resamples a different set of jittered samples every
// frame, so its picture keeps moving at every edge the checker's perspective compresses; the
// accumulation has to settle that without the scene having moved at all -- and at the same output
// extent, which is what makes the two comparable at all.
TEST_CASE("upscaled accumulation settles a static jittered frame", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    const std::vector<DrawItem> items = checkerAndPoleItems(camera, *cube, *plane);
    SceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});
    base.temporal.renderScale = 0.5f;

    auto rawRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(rawRenderer.has_value());
    const std::vector<ScenarioFrame> spatial =
        renderSequence(**device, **rawRenderer, 32, ReconstructionMode::Raw, base, camera,
                       TemporalDebugView::Off, nullptr);
    REQUIRE(spatial.back().status.upscaled);

    auto taauRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(taauRenderer.has_value());
    const std::vector<ScenarioFrame> taau =
        renderSequence(**device, **taauRenderer, 32, ReconstructionMode::NativeTaa, base, camera,
                       TemporalDebugView::Off, nullptr);
    REQUIRE(taau.back().status.upscaled);

    const std::vector<uint32_t> pixels = allPixels();
    const float spatialChange = frameToFrameChange(spatial, pixels);
    const float taauChange = frameToFrameChange(taau, pixels);
    INFO("spatial raw frame-to-frame |dY| " << spatialChange << ", TAAU " << taauChange);
    // A spatial sequence that does not shimmer would make the ratio vacuous.
    REQUIRE(spatialChange > 1e-4f);
    REQUIRE(taauChange <= 0.25f * spatialChange);
    REQUIRE(taauChange <= 0.005f);
    REQUIRE(taau.back().status.warmupComplete);
}

//======================================================================================================================
// Detail recovery (M6.3 spec 10): the same scene, frame 32, measured against the native-resolution
// accumulation of the same scene at scale 1. Half the samples per frame is not half the detail --
// jitter puts a different sub-pixel sample under every output pixel over a jitter period, and the
// sample-proximity weight is what lets the accumulation collect them -- so the upscaled picture has
// to sit closer to the native one than a single frame's resample of the same samples does.
TEST_CASE("upscaled accumulation recovers detail the render extent cannot hold",
          "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    const std::vector<DrawItem> items = checkerAndPoleItems(camera, *cube, *plane);
    SceneView upscaledBase = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});
    upscaledBase.temporal.renderScale = 0.5f;
    SceneView nativeBase = upscaledBase;
    nativeBase.temporal.renderScale = 1.0f;

    const auto sequence = [&](const SceneView& base, ReconstructionMode mode) {
        auto renderer =
            Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
        REQUIRE(renderer.has_value());
        return renderSequence(**device, **renderer, 32, mode, base, camera, TemporalDebugView::Off,
                              nullptr);
    };
    const std::vector<ScenarioFrame> spatial = sequence(upscaledBase, ReconstructionMode::Raw);
    const std::vector<ScenarioFrame> taau = sequence(upscaledBase, ReconstructionMode::NativeTaa);
    const std::vector<ScenarioFrame> native = sequence(nativeBase, ReconstructionMode::NativeTaa);
    REQUIRE_FALSE(native.back().status.upscaled);

    const std::vector<uint32_t> pixels = allPixels();
    const std::vector<float> reference = luminances(native.back().history);
    const float taauError = meanAbsDiff(luminances(taau.back().history), reference, pixels);
    const float spatialError = meanAbsDiff(luminances(spatial.back().history), reference, pixels);
    INFO("mean |Y - Y(native TAA)|: TAAU " << taauError << ", spatial raw " << spatialError);
    REQUIRE(spatialError > 0.0f);
    REQUIRE(taauError <= 0.75f * spatialError);
}

//======================================================================================================================
// Thin geometry upscaled (M6.3 spec 10): five poles two output pixels wide, which is one render
// pixel at half scale. A pole is covered only on the jitter samples that land inside it, so the
// accumulation has to carry its brightness across the samples that miss it rather than averaging it
// into the floor -- and it has to reach the brightness the native-resolution accumulation does.
TEST_CASE("upscaled accumulation keeps thin geometry's brightness", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    // The M6.2 thin-geometry framing: the poles stand on the floor and entirely below the horizon,
    // so an uncovered sample reprojects onto real geometry rather than onto empty background.
    const Camera camera = scenarioCamera({0.0f, 2.0f, 4.0f}, -0.16f);
    const float poleWidth = 2.0f / pixelsPerUnit(camera, 14.0f);
    std::vector<DrawItem> items;
    items.push_back(staticItem(*plane, glm::mat4{1.0f}, {0.30f, 0.30f, 0.30f, 1.0f}));
    for (int32_t i = -2; i <= 2; ++i) {
        items.push_back(staticItem(
            *cube,
            boxModel({static_cast<float>(i) * 1.5f, 0.25f, -10.0f}, {poleWidth, 0.5f, poleWidth}),
            {1.0f, 1.0f, 1.0f, 1.0f}));
    }
    // Travelling -Z: the poles' camera-facing sides take the light and the floor's upward normal
    // takes none of it, so a pole pixel is the only thing a luminance threshold can find.
    SceneView upscaledBase = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
    upscaledBase.temporal.renderScale = 0.5f;
    SceneView nativeBase = upscaledBase;
    nativeBase.temporal.renderScale = 1.0f;

    const auto sequence = [&](const SceneView& base, ReconstructionMode mode) {
        auto renderer =
            Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
        REQUIRE(renderer.has_value());
        return renderSequence(**device, **renderer, 32, mode, base, camera, TemporalDebugView::Off,
                              nullptr);
    };
    const std::vector<ScenarioFrame> spatial = sequence(upscaledBase, ReconstructionMode::Raw);
    const std::vector<ScenarioFrame> taau = sequence(upscaledBase, ReconstructionMode::NativeTaa);
    const std::vector<ScenarioFrame> native = sequence(nativeBase, ReconstructionMode::NativeTaa);

    // A pole pixel is one the poles covered on at least one measured jitter sample; the floor never
    // reaches this luminance under a light that grazes it.
    constexpr float kPoleThreshold = 0.02f;
    std::vector<uint32_t> polePixels;
    std::vector<float> spatialSum(kScenarioPixels, 0.0f);
    for (size_t frame = kMeasureFirstFrame - 1; frame < spatial.size(); ++frame) {
        const std::vector<float> y = luminances(spatial[frame].history);
        for (size_t i = 0; i < y.size(); ++i) {
            spatialSum[i] += y[i];
        }
    }
    const auto measuredFrames = static_cast<float>(spatial.size() - (kMeasureFirstFrame - 1));
    for (uint32_t i = 0; i < kScenarioPixels; ++i) {
        if (spatialSum[i] / measuredFrames > kPoleThreshold) {
            polePixels.push_back(i);
        }
    }
    INFO("pole pixels " << polePixels.size());
    REQUIRE(polePixels.size() > 50);

    const float taauMean = meanOverWindow(taau, polePixels);
    const float nativeMean = meanOverWindow(native, polePixels);
    INFO("pole mean Y: TAAU " << taauMean << ", native TAA " << nativeMean);
    REQUIRE(nativeMean > 0.0f);
    REQUIRE(taauMean >= 0.75f * nativeMean);

    const float spatialChange = frameToFrameChange(spatial, polePixels);
    const float taauChange = frameToFrameChange(taau, polePixels);
    INFO("pole frame-to-frame |dY|: spatial raw " << spatialChange << ", TAAU " << taauChange);
    REQUIRE(spatialChange > 1e-4f);
    REQUIRE(taauChange <= 0.25f * spatialChange);
}

//======================================================================================================================
// Motion and ghosting upscaled (M6.3 spec 10): a quad translating four output pixels a frame over a
// backdrop. The band it vacates is the classic trail -- history describing the quad blended over a
// background the quad has left -- and reprojecting an output-extent history through a render-extent
// motion target is exactly where an upscaling kernel would smear one. The region has to converge on
// the spatial picture instead, and the rejection mask has to name the reason.
TEST_CASE("an upscaled moving quad leaves no trail behind it", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(32.0f), "lmx.test.scenarioWall");
    REQUIRE(plane.has_value());

    const Camera camera = movingQuadCamera();
    const std::vector<DrawItem> items = movingQuadItems(camera, *cube, *plane);
    // Facing the camera, so both the quad and the backdrop take the light and the two differ by
    // albedo alone -- which is what makes the luminance contrast a number the tolerance can scale.
    SceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
    base.temporal.renderScale = 0.5f;

    const PerFrame animate = [&camera](uint32_t frame, SceneView& view, Camera&) {
        auto* drawn = const_cast<DrawItem*>(view.items.data());
        drawn[1].model = movingQuadModel(camera, frame);
        drawn[1].previousModel = movingQuadModel(camera, frame == 1 ? 1 : frame - 1);
    };

    constexpr uint32_t kFrames = 28;
    auto spatialRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(spatialRenderer.has_value());
    const std::vector<ScenarioFrame> spatial =
        renderSequence(**device, **spatialRenderer, kFrames, ReconstructionMode::Raw, base, camera,
                       TemporalDebugView::Off, animate);
    // The debug view replaces the display target and nothing else, so one accumulated run answers
    // both halves: its colour slot is the picture, its display target the rejection mask.
    auto taauRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(taauRenderer.has_value());
    const std::vector<ScenarioFrame> taau =
        renderSequence(**device, **taauRenderer, kFrames, ReconstructionMode::NativeTaa, base,
                       camera, TemporalDebugView::RejectionMask, animate);
    REQUIRE(taau.back().status.upscaled);

    const std::vector<uint32_t> vacatedRecently = movingQuadVacated(camera, kFrames);
    std::vector<uint32_t> vacatedThisFrame;
    const auto [currentLeft, currentRight] = movingQuadColumns(camera, kFrames);
    const auto [previousLeft, previousRight] = movingQuadColumns(camera, kFrames - 1);
    REQUIRE(previousLeft < currentLeft);
    for (uint32_t index : vacatedRecently) {
        if (static_cast<int32_t>(index % kScenarioWidth) >= previousLeft) {
            vacatedThisFrame.push_back(index);
        }
    }
    INFO("vacated this frame " << vacatedThisFrame.size() << ", recently "
                               << vacatedRecently.size());
    REQUIRE(vacatedThisFrame.size() > 20);
    REQUIRE(vacatedRecently.size() > vacatedThisFrame.size());

    const std::vector<float> spatialY = luminances(spatial[kFrames - 1].history);
    const float contrast = movingQuadContrast(camera, spatialY, kFrames);
    INFO("quad-to-backdrop contrast " << contrast);
    REQUIRE(contrast > 0.05f);

    const std::vector<float> taauY = luminances(taau[kFrames - 1].history);
    const float trail = meanAbsDiff(taauY, spatialY, vacatedRecently);
    INFO("mean |Y(TAAU) - Y(spatial raw)| in the vacated region " << trail);
    // The upscaled tolerance of the 2026-09-09 amendment to M6.3 spec 10, not the native 0.1:
    // dilating motion and depth over 3x3 render texels spans two output pixels at half scale, so
    // the vacated band's two trailing columns blend rather than reject. The native case above
    // keeps 0.1.
    REQUIRE(trail <= 0.125f * contrast);

    // Disoccluded is drawn flat red, with the clipped flag adding green; the mask is written into
    // the display target unencoded, so the channel test is on the code the shader chose.
    uint32_t disoccluded = 0;
    for (uint32_t index : vacatedThisFrame) {
        const uint8_t blue = taau[kFrames - 1].display[index * 4];
        const uint8_t red = taau[kFrames - 1].display[index * 4 + 2];
        if (red > 200 && blue < 50) {
            ++disoccluded;
        }
    }
    INFO("disoccluded share " << static_cast<float>(disoccluded) / vacatedThisFrame.size());
    REQUIRE(disoccluded * 2 >= vacatedThisFrame.size());
}

//======================================================================================================================
// Gradual scale change (M6.3 spec 10): a static checker stepped from scale 1.0 down to 0.5 in
// twentieths on frames 17 to 27. The history lives at the output extent and is reprojected in
// normalised UV, so none of those steps is an extent change: every one of them derives None, the
// accumulation age keeps climbing through them, the render-extent change is reported instead of
// reset -- and the picture must not jump on the frame the extent moves.
TEST_CASE("a gradual render-scale change reuses the history", "[gpu][temporal]") {
    using namespace lmx::rhi;
    using lmx::render::HistoryResetReason;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<DrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const SceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});

    constexpr uint32_t kFirstStepFrame = 17;
    constexpr uint32_t kLastStepFrame = 27;
    // Clamped rather than trusted: 1 - 0.05f * 10 lands a hair below kMinRenderScale in binary
    // floating point, and the scale a frame declares has to be a legal one.
    const auto scaleForFrame = [](uint32_t frame) {
        if (frame < kFirstStepFrame) {
            return 1.0f;
        }
        const uint32_t step = std::min(frame, uint32_t{kLastStepFrame}) - uint32_t{kFirstStepFrame};
        return std::max(0.5f, 1.0f - 0.05f * static_cast<float>(step));
    };
    const PerFrame animate = [&scaleForFrame](uint32_t frame, SceneView& view, Camera&) {
        view.temporal.renderScale = scaleForFrame(frame);
    };

    auto renderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(renderer.has_value());
    const std::vector<ScenarioFrame> frames =
        renderSequence(**device, **renderer, kLastStepFrame, ReconstructionMode::NativeTaa, base,
                       camera, TemporalDebugView::Off, animate);

    const std::vector<uint32_t> pixels = allPixels();
    uint64_t previousChangeFrame = frames[kFirstStepFrame - 1].status.lastRenderExtentChangeFrame;
    uint32_t previousAge = frames[kFirstStepFrame - 1].status.historyAge;
    uint32_t steps = 0;
    for (uint32_t frame = kFirstStepFrame + 1; frame <= kLastStepFrame; ++frame) {
        const ScenarioFrame& current = frames[frame - 1];
        const ScenarioFrame& before = frames[frame - 2];
        INFO("step frame " << frame << " scale " << current.status.renderScale << " extent "
                           << current.status.extents.renderWidth << "x"
                           << current.status.extents.renderHeight);
        // The extent really moved: a step that rounded onto its predecessor would prove nothing.
        REQUIRE(current.status.extents.renderWidth != before.status.extents.renderWidth);
        REQUIRE(current.status.lastReset == HistoryResetReason::None);
        REQUIRE(current.status.historyValid);
        REQUIRE(current.status.historyAge == previousAge + 1);
        REQUIRE(current.status.lastRenderExtentChangeFrame > previousChangeFrame);
        // Nothing was reallocated across any of the steps.
        REQUIRE(current.status.historyBytes == before.status.historyBytes);
        REQUIRE(current.status.depthHistoryBytes == before.status.depthHistoryBytes);

        const float change =
            meanAbsDiff(luminances(current.history), luminances(before.history), pixels);
        INFO("whole-image mean |dY| across the step " << change);
        REQUIRE(change <= 0.02f);

        previousChangeFrame = current.status.lastRenderExtentChangeFrame;
        previousAge = current.status.historyAge;
        ++steps;
    }
    REQUIRE(steps == kLastStepFrame - kFirstStepFrame);

    // A frame with temporal off rasterises at the output extent whatever the scale field says, and
    // has no history to have survived anything, so it must not be recorded as a scale change.
    SceneView off = base;
    off.temporal.enabled = false;
    off.temporal.renderScale = 0.5f;
    renderFrame(**device, **renderer, camera, off);
    REQUIRE((*renderer)->temporalStatus().lastRenderExtentChangeFrame == previousChangeFrame);
}

//======================================================================================================================
// Scale oscillation (M6.3 spec 10): the render scale alternates between 0.5 and 1.0 on every frame
// for 32 frames while a quad crosses the view. Every render-extent target is allocated at the
// output extent and used through an active rectangle, so the worst case a controller could produce
// must allocate nothing: the histories keep their footprint, the transient heap keeps its size, and
// the pool never retires a generation. The picture has to survive it too -- the ghosting tolerance
// holds on every measured frame, not just at the end.
//
// The graph is built here rather than through Renderer::render() so the test owns the TransientPool
// whose generation counts it reads, and so each frame's compiled record is available for its heap
// size.
TEST_CASE("an oscillating render scale allocates nothing and does not ghost", "[gpu][temporal]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::render::createMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::render::createMesh(**device, lmx::render::makePlane(32.0f), "lmx.test.scenarioWall");
    REQUIRE(plane.has_value());

    const Camera camera = movingQuadCamera();
    std::vector<DrawItem> items = movingQuadItems(camera, *cube, *plane);
    SceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});

    constexpr uint32_t kFrames = 32;
    // Odd frames at half scale, even frames at full: the extent changes on every single frame.
    const auto scaleForFrame = [](uint32_t frame) { return frame % 2 == 1 ? 0.5f : 1.0f; };
    const PerFrame animate = [&camera, &scaleForFrame](uint32_t frame, SceneView& view, Camera&) {
        view.temporal.renderScale = scaleForFrame(frame);
        auto* drawn = const_cast<DrawItem*>(view.items.data());
        drawn[1].model = movingQuadModel(camera, frame);
        drawn[1].previousModel = movingQuadModel(camera, frame == 1 ? 1 : frame - 1);
    };

    // The spatial reference the ghosting tolerance is measured against, at the same scales.
    auto spatialRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(spatialRenderer.has_value());
    const std::vector<ScenarioFrame> spatial =
        renderSequence(**device, **spatialRenderer, kFrames, ReconstructionMode::Raw, base, camera,
                       TemporalDebugView::Off, animate);

    auto taauRenderer =
        Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
    REQUIRE(taauRenderer.has_value());
    lmx::render::TransientPool transients(**device);
    std::vector<ScenarioFrame> taau;
    std::vector<uint64_t> heapBytes;
    taau.reserve(kFrames);
    heapBytes.reserve(kFrames);
    for (uint32_t frame = 1; frame <= kFrames; ++frame) {
        SceneView view = base;
        Camera frameCamera = camera;
        view.temporal.enabled = true;
        view.temporal.jitterEnabled = true;
        view.temporal.reconstruction = ReconstructionMode::NativeTaa;
        view.temporal.debugView = TemporalDebugView::Off;
        animate(frame, view, frameCamera);

        CommandList& commands = (*device)->beginFrame();
        transients.beginFrame();
        lmx::render::RenderGraph graph(transients);
        const lmx::render::GraphTexture display =
            (*taauRenderer)->declarePasses(graph, commands, frameCamera, view);
        graph.exportTexture(display);
        const lmx::render::CompiledFrameRecord record =
            graph.execute(commands, (*device)->frameNumber());
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();

        ScenarioFrame captured;
        captured.history = readHalf4(*(*taauRenderer)->historyTarget());
        captured.raw = readHalf4((*taauRenderer)->hdrColorTarget());
        captured.status = (*taauRenderer)->temporalStatus();
        taau.push_back(std::move(captured));
        heapBytes.push_back(record.debug.memory.highWater);
    }

    // The histories are output-extent resources and never move.
    for (const ScenarioFrame& frame : taau) {
        REQUIRE(frame.status.historyBytes == taau.front().status.historyBytes);
        REQUIRE(frame.status.depthHistoryBytes == taau.front().status.depthHistoryBytes);
    }
    // From the fourth frame every slot of the pool has seen both scales, so a footprint that
    // depended on the render extent would show as a difference from here on.
    for (size_t i = 3; i < heapBytes.size(); ++i) {
        INFO("frame " << i + 1 << " transient heap bytes " << heapBytes[i] << ", frame 4 "
                      << heapBytes[3]);
        REQUIRE(heapBytes[i] == heapBytes[3]);
    }
    INFO("live generations " << transients.liveGenerationCount() << ", retiring "
                             << transients.retiringGenerationCount());
    REQUIRE(transients.retiringGenerationCount() == 0);
    REQUIRE(transients.liveGenerationCount() <= 3);

    // The picture, on every measured frame rather than only at the end: an oscillating scale is the
    // input a reprojection bug would smear a trail on.
    for (uint32_t frame = kMeasureFirstFrame; frame <= kFrames; ++frame) {
        const std::vector<uint32_t> vacated = movingQuadVacated(camera, frame);
        REQUIRE(vacated.size() > 20);
        const std::vector<float> spatialY = luminances(spatial[frame - 1].history);
        const std::vector<float> taauY = luminances(taau[frame - 1].history);
        const float contrast = movingQuadContrast(camera, spatialY, frame);
        const float trail = meanAbsDiff(taauY, spatialY, vacated);
        INFO("frame " << frame << " scale " << taau[frame - 1].status.renderScale << " trail "
                      << trail << ", contrast " << contrast);
        REQUIRE(contrast > 0.05f);
        // The oscillation clause's own bound in the 2026-09-09 amendment to M6.3 spec 10, wider
        // than the moving-quad case's 0.125 and for a different reason: the alternation's scale-1
        // frames run the upscaling kernel against a history accumulated at half scale. Asserted on
        // every measured frame, so a ghost outliving its transition still fails.
        REQUIRE(trail <= 0.15f * contrast);
    }
}
