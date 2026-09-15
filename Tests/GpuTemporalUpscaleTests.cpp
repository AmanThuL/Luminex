#include "GpuTemporalTestSupport.h"
#include "SceneTableTestSupport.h"

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

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
        lmx::test::fixtureMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.temporalPlane");
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
    const std::array<FixtureDrawItem, 1> items = {
        FixtureDrawItem{.mesh = &*plane, .model = model, .previousModel = model}};
    FixtureSceneView view = temporalSceneView(items);
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
        lmx::test::fixtureMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.constantPlane");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    auto renderer = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const glm::mat4 model = facingPlaneModel(-2.0f);
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
        lmx::test::fixtureMesh(**device, lmx::render::makePlane(10.0f), "lmx.test.temporalPlane");
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
    const std::array<FixtureDrawItem, 1> first = {
        FixtureDrawItem{.mesh = &*plane, .model = previousModel, .previousModel = previousModel}};
    const std::array<FixtureDrawItem, 1> second = {
        FixtureDrawItem{.mesh = &*plane, .model = model, .previousModel = previousModel}};

    FixtureSceneView view = temporalSceneView(first);
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

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    const std::vector<FixtureDrawItem> items = checkerAndPoleItems(camera, *cube, *plane);
    FixtureSceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});
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

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    const std::vector<FixtureDrawItem> items = checkerAndPoleItems(camera, *cube, *plane);
    FixtureSceneView upscaledBase = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});
    upscaledBase.temporal.renderScale = 0.5f;
    FixtureSceneView nativeBase = upscaledBase;
    nativeBase.temporal.renderScale = 1.0f;

    const auto sequence = [&](const FixtureSceneView& base, ReconstructionMode mode) {
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

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    // The M6.2 thin-geometry framing: the poles stand on the floor and entirely below the horizon,
    // so an uncovered sample reprojects onto real geometry rather than onto empty background.
    const Camera camera = scenarioCamera({0.0f, 2.0f, 4.0f}, -0.16f);
    const float poleWidth = 2.0f / pixelsPerUnit(camera, 14.0f);
    std::vector<FixtureDrawItem> items;
    items.push_back(staticItem(*plane, glm::mat4{1.0f}, {0.30f, 0.30f, 0.30f, 1.0f}));
    for (int32_t i = -2; i <= 2; ++i) {
        items.push_back(staticItem(
            *cube,
            boxModel({static_cast<float>(i) * 1.5f, 0.25f, -10.0f}, {poleWidth, 0.5f, poleWidth}),
            {1.0f, 1.0f, 1.0f, 1.0f}));
    }
    // Travelling -Z: the poles' camera-facing sides take the light and the floor's upward normal
    // takes none of it, so a pole pixel is the only thing a luminance threshold can find.
    FixtureSceneView upscaledBase = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
    upscaledBase.temporal.renderScale = 0.5f;
    FixtureSceneView nativeBase = upscaledBase;
    nativeBase.temporal.renderScale = 1.0f;

    const auto sequence = [&](const FixtureSceneView& base, ReconstructionMode mode) {
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

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::render::makePlane(32.0f), "lmx.test.scenarioWall");
    REQUIRE(plane.has_value());

    const Camera camera = movingQuadCamera();
    const std::vector<FixtureDrawItem> items = movingQuadItems(camera, *cube, *plane);
    // Facing the camera, so both the quad and the backdrop take the light and the two differ by
    // albedo alone -- which is what makes the luminance contrast a number the tolerance can scale.
    FixtureSceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
    base.temporal.renderScale = 0.5f;

    const PerFrame animate = [&camera](uint32_t frame, FixtureSceneView& view, Camera&) {
        auto* drawn = const_cast<FixtureDrawItem*>(view.items.data());
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

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::render::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<FixtureDrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const FixtureSceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});

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
    const PerFrame animate = [&scaleForFrame](uint32_t frame, FixtureSceneView& view, Camera&) {
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
    FixtureSceneView off = base;
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

    auto cube = lmx::test::fixtureMesh(**device, lmx::render::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::render::makePlane(32.0f), "lmx.test.scenarioWall");
    REQUIRE(plane.has_value());

    const Camera camera = movingQuadCamera();
    std::vector<FixtureDrawItem> items = movingQuadItems(camera, *cube, *plane);
    FixtureSceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});

    constexpr uint32_t kFrames = 32;
    // Odd frames at half scale, even frames at full: the extent changes on every single frame.
    const auto scaleForFrame = [](uint32_t frame) { return frame % 2 == 1 ? 0.5f : 1.0f; };
    const PerFrame animate = [&camera, &scaleForFrame](uint32_t frame, FixtureSceneView& view,
                                                       Camera&) {
        view.temporal.renderScale = scaleForFrame(frame);
        auto* drawn = const_cast<FixtureDrawItem*>(view.items.data());
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
        FixtureSceneView view = base;
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
            (*taauRenderer)
                ->declarePasses(graph, commands, frameCamera,
                                lmx::test::prepareSceneView(view, device));
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
