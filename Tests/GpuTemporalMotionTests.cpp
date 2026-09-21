#include "Engine/Catalog/CatalogScenes.h"
#include "GpuTemporalTestSupport.h"
#include "Render/SceneViewBuilder.h"
#include "SceneTableTestSupport.h"

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
// A camera that moved between two frames over a static surface: every probe texel must carry the
// UV delta motionBetween() derives for the surface point that texel sees.
TEST_CASE("motion vectors reproject a moved camera", "[gpu][temporal]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(10.0f), "lmx.test.temporalPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    constexpr float kPlaneZ = -2.0f;
    const glm::mat4 model = facingPlaneModel(kPlaneZ);
    const std::array<FixtureDrawItem, 1> items = {
        FixtureDrawItem{.mesh = &*plane, .model = model, .previousModel = model}};
    FixtureSceneView view = temporalSceneView(items);
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
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(10.0f), "lmx.test.temporalPlane");
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
    const std::array<FixtureDrawItem, 1> first = {
        FixtureDrawItem{.mesh = &*plane, .model = previousModel, .previousModel = previousModel}};
    const std::array<FixtureDrawItem, 1> second = {
        FixtureDrawItem{.mesh = &*plane, .model = model, .previousModel = previousModel}};

    FixtureSceneView view = temporalSceneView(first);
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
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(10.0f), "lmx.test.temporalPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const glm::mat4 model = facingPlaneModel(-2.0f);
    const std::array<FixtureDrawItem, 1> items = {
        FixtureDrawItem{.mesh = &*plane, .model = model, .previousModel = model}};
    FixtureSceneView view = temporalSceneView(items);
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
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(10.0f), "lmx.test.temporalPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const glm::mat4 model = facingPlaneModel(-2.0f);
    const std::array<FixtureDrawItem, 1> items = {
        FixtureDrawItem{.mesh = &*plane,
                        .model = model,
                        .previousModel = model,
                        .motionClass = lmx::engine::MotionClass::Invalid}};
    FixtureSceneView view = temporalSceneView(items);
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
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(10.0f), "lmx.test.temporalPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const glm::mat4 model = facingPlaneModel(-2.0f);
    const std::array<FixtureDrawItem, 1> items = {
        FixtureDrawItem{.mesh = &*plane, .model = model, .previousModel = model}};
    FixtureSceneView view = temporalSceneView(items);
    view.temporal.enabled = true;
    view.temporal.debugView = lmx::render::TemporalDebugView::ReprojectionError;

    const Camera camera;
    renderFrame(**device, **renderer, camera, view);
    renderFrame(**device, **renderer, camera, view);
    REQUIRE((*renderer)->temporalStatus().lastReset == lmx::render::HistoryResetReason::None);

    // The committed history is this frame's scene colour, and a still scene reprojects onto
    // itself: history and scene colour must agree bit for bit, which is the difference the
    // diagnostic reports as exactly zero.
    rojoRHI::Texture* history = (*renderer)->historyTarget();
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
// The diagnostic's motion texel is the one its current-colour sample falls in, which below scale 1
// is not the texel the output pixel's own corner maps to. A vertical split between a surface whose
// motion is the invalid sentinel and a static one puts the two mappings on opposite sides of the
// seam for a whole column of output pixels: taking motion from the corner reports "nothing to
// compare" for pixels that sample the static surface, and compares pixels that sample the invalid
// one. The expected texel is computed on the CPU with Source/Render/Temporal.h's
// renderSamplePosition, so what the case pins is the mapping and not a hand-picked pixel.
TEST_CASE("the reprojection diagnostic reads motion at the sampled texel", "[gpu][temporal]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(10.0f), "lmx.test.temporalPlane");
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
    const std::array<FixtureDrawItem, 2> items = {
        FixtureDrawItem{.mesh = &*plane,
                        .model = invalid,
                        .previousModel = invalid,
                        .motionClass = lmx::engine::MotionClass::Invalid},
        FixtureDrawItem{.mesh = &*plane, .model = stat, .previousModel = stat},
    };
    FixtureSceneView view = temporalSceneView(items);
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
// TemporalLab through the shipped path: the scene's own rigid tracks, advanced one animation step,
// declared with temporal on. It is the case that joins the engine's previous transforms to the
// renderer's motion attachment -- moving tracks carry motion, the static reference carries none,
// and the invalid cube carries the sentinel, all in one image.
TEST_CASE("TemporalLab writes motion for its animated tracks", "[gpu][temporal]") {
    using namespace rojoRHI;

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
    std::vector<lmx::engine::DrawItem> items;
    for (int frame = 0; frame < 2; ++frame) {
        auto& commands = (*device)->beginFrame();
        REQUIRE((*scene)->prepareFrame((*device)->frameNumber()));
        auto view = lmx::render::buildSceneView(**scene, items, lmx::render::ShadowFilter::PCF,
                                                /*wireframe=*/false);
        view.temporal.enabled = true;
        view.temporal.debugView = lmx::render::TemporalDebugView::MotionVectors;
        (*renderer)->render(commands, camera, view, false);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
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
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(10.0f), "lmx.test.temporalPlane");
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
    const std::array<FixtureDrawItem, 2> items = {
        FixtureDrawItem{
            .mesh = &*plane,
            .model = left,
            .material = {.albedo = {0.0f, 0.0f, 0.0f, 1.0f}, .emissive = {4.0f, 4.0f, 4.0f}},
            .previousModel = left},
        FixtureDrawItem{.mesh = &*plane,
                        .model = right,
                        .material = {.albedo = {0.0f, 0.0f, 0.0f, 1.0f}},
                        .previousModel = right},
    };
    FixtureSceneView view = temporalSceneView(items);
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
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(10.0f), "lmx.test.constantPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());
    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    constexpr float kPlaneZ = -2.0f;
    const glm::mat4 model = facingPlaneModel(kPlaneZ);
    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{
        .mesh = &*plane,
        .model = model,
        .material = {.albedo = {0.0f, 0.0f, 0.0f, 1.0f}, .emissive = {0.5f, 0.5f, 0.5f}},
        .previousModel = model}};
    FixtureSceneView view = temporalSceneView(items);
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
// The sky's motion is rotation-only: the sphere is drawn centred on the previous eye for the
// previous frame's matrices, so a translation cancels exactly and only the camera's rotation
// survives. Both halves are pinned here against the same motionBetween() oracle the geometry cases
// use, over a scene that is nothing but sky -- the sphere covers every texel, so the probes read
// the sky path rather than a piece of geometry in front of it.
TEST_CASE("sky motion follows the camera's rotation alone", "[gpu][temporal]") {
    using namespace rojoRHI;

    constexpr std::array<uint8_t, 4> kSkyTexel = {0, 128, 255, 255};
    const TextureMip skyMip{.data = kSkyTexel.data(), .bytesPerRow = 4};
    const std::array<TextureMip, 6> skyFaces = {skyMip, skyMip, skyMip, skyMip, skyMip, skyMip};

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto skySphere =
        lmx::test::fixtureMesh(**device, lmx::engine::fromGeo(lmx::asset::makeSphere(0.5f, 20, 20)),
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
    FixtureSceneView view = temporalSceneView({});
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
