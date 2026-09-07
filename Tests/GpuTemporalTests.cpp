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
