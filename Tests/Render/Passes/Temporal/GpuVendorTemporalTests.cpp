#include "Support/GpuTemporalTestSupport.h"

#include "Engine/Asset/Model/GeometryGenerator.h"
#include "Render/Passes/Temporal/Temporal.h"
#include "Render/Passes/Temporal/TemporalHistory.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

using lmx::render::HistoryResetReason;

//======================================================================================================================
TEST_CASE("vendor reconstruction settles a static jittered frame", "[gpu][temporal][vendor]") {
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    if (!gpu->upscalerSupport().available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.vendorCube");
    REQUIRE(cube.has_value());
    auto plane = lmx::test::fixtureMesh(gpu->device(), lmx::engine::makePlane(64.0f),
                                        "lmx.test.vendorFloor");
    REQUIRE(plane.has_value());
    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    const std::vector<FixtureDrawItem> items = checkerAndPoleItems(camera, *cube, *plane);
    FixtureSceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});
    for (float scale : {0.5f, 1.0f}) {
        CAPTURE(scale);
        base.temporal.renderScale = scale;
        const auto sequence = [&](ReconstructionMode mode) {
            auto renderer = Renderer::create(*gpu, kScenarioWidth, kScenarioHeight, true);
            REQUIRE(renderer.has_value());
            return renderSequence(*gpu, **renderer, 32, mode, base, camera, TemporalDebugView::Off,
                                  nullptr);
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
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    if (!gpu->upscalerSupport().available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto plane = lmx::test::fixtureMesh(gpu->device(), lmx::engine::makePlane(32.0f),
                                        "lmx.test.vendorCoverage");
    REQUIRE(plane.has_value());
    const Camera camera;
    auto empty = Renderer::create(*gpu, kScenarioWidth, kScenarioHeight, true);
    REQUIRE(empty.has_value());
    renderFrame(*gpu, **empty, camera, scenarioSceneView({}, {0.0f, 0.0f, -1.0f}));
    std::vector<uint8_t> background(kScenarioPixels * 4);
    {
        requireRecordedUse((*empty)->textureStates(), (*empty)->colorTarget(),
                           lmx::render::TextureUse::ShaderRead);
        const auto readbackBytes =
            lmx::render::readbackNow(*gpu, (*empty)->colorTarget(), (*empty)->textureStates());
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(background.data(), readbackBytes->data(), background.size());
    }
    const std::array<FixtureDrawItem, 1> items = {
        staticItem(*plane, facingPlaneModel(-2.0f), {0.6f, 0.6f, 0.6f, 1.0f})};
    for (float scale : {0.5f, 1.0f}) {
        CAPTURE(scale);
        FixtureSceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
        base.temporal.renderScale = scale;
        auto renderer = Renderer::create(*gpu, kScenarioWidth, kScenarioHeight, true);
        REQUIRE(renderer.has_value());
        const auto frames = renderSequence(*gpu, **renderer, 32, ReconstructionMode::VendorTemporal,
                                           base, camera, TemporalDebugView::Off, nullptr);
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
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    if (!gpu->upscalerSupport().available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.vendorCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makePlane(32.0f), "lmx.test.vendorWall");
    REQUIRE(plane.has_value());
    const Camera camera = movingQuadCamera();
    std::vector<FixtureDrawItem> items = movingQuadItems(camera, *cube, *plane);
    FixtureSceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
    base.temporal.renderScale = 0.5f;
    const PerFrame animate = [&items, &camera](uint32_t frame, FixtureSceneView&, Camera&) {
        items[1].model = movingQuadModel(camera, frame);
        items[1].previousModel = movingQuadModel(camera, frame == 1 ? 1 : frame - 1);
    };
    constexpr uint32_t kFrames = 28;
    const auto sequence = [&](ReconstructionMode mode) {
        auto renderer = Renderer::create(*gpu, kScenarioWidth, kScenarioHeight, true);
        REQUIRE(renderer.has_value());
        return renderSequence(*gpu, **renderer, kFrames, mode, base, camera, TemporalDebugView::Off,
                              animate);
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
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    if (!gpu->upscalerSupport().available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto plane = lmx::test::fixtureMesh(gpu->device(), lmx::engine::makePlane(32.0f),
                                        "lmx.test.vendorSwitchPlane");
    REQUIRE(plane.has_value());
    const Camera camera;
    auto empty = Renderer::create(*gpu, kScenarioWidth, kScenarioHeight, true);
    REQUIRE(empty.has_value());
    renderFrame(*gpu, **empty, camera, scenarioSceneView({}, {0.0f, 0.0f, -1.0f}));
    const auto clear = readHalf4(*gpu, (*empty)->hdrColorTexture(), (*empty)->textureStates(),
                                 lmx::render::TextureUse::ShaderRead);
    const std::array<FixtureDrawItem, 1> items = {
        staticItem(*plane, facingPlaneModel(-2.0f), {0.6f, 0.6f, 0.6f, 1.0f})};
    FixtureSceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
    const PerFrame switchMode = [](uint32_t frame, FixtureSceneView& view, Camera&) {
        view.temporal.reconstruction = frame >= 9 && frame <= 16
                                           ? ReconstructionMode::VendorTemporal
                                           : ReconstructionMode::NativeTaa;
    };
    auto renderer = Renderer::create(*gpu, kScenarioWidth, kScenarioHeight, true);
    REQUIRE(renderer.has_value());
    const auto frames = renderSequence(*gpu, **renderer, 24, ReconstructionMode::NativeTaa, base,
                                       camera, TemporalDebugView::Off, switchMode);
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
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    if (!gpu->upscalerSupport().available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.vendorCube");
    REQUIRE(cube.has_value());
    auto plane = lmx::test::fixtureMesh(gpu->device(), lmx::engine::makePlane(64.0f),
                                        "lmx.test.vendorFloor");
    REQUIRE(plane.has_value());
    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<FixtureDrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const FixtureSceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});
    constexpr uint32_t kStepFrame = 24;
    constexpr uint32_t kFrames = kStepFrame + 7;
    const PerFrame animate = [](uint32_t frame, FixtureSceneView& view, Camera&) {
        view.exposureEv = frame >= kStepFrame ? 2.0f : 0.0f;
    };
    const auto sequence = [&](ReconstructionMode mode) {
        auto renderer = Renderer::create(*gpu, kScenarioWidth, kScenarioHeight, true);
        REQUIRE(renderer.has_value());
        return renderSequence(*gpu, **renderer, kFrames, mode, base, camera, TemporalDebugView::Off,
                              animate);
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
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    if (!gpu->upscalerSupport().available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto renderer = Renderer::create(*gpu, kScenarioWidth, kScenarioHeight, true);
    REQUIRE(renderer.has_value());
    const Camera camera;
    FixtureSceneView view = scenarioSceneView({}, {0.0f, 0.0f, -1.0f});
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = ReconstructionMode::VendorTemporal;
    renderFrame(*gpu, **renderer, camera, view);
    const auto first = (*renderer)->temporalStatus();
    REQUIRE(first.vendorScalerGeneration > 0);
    REQUIRE(first.vendorReset);
    view.temporal.renderScale = 0.5f;
    renderFrame(*gpu, **renderer, camera, view);
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
    renderFrame(*gpu, **renderer, camera, view);
    const auto afterResize = (*renderer)->temporalStatus();
    CHECK(afterResize.lastReset == HistoryResetReason::ExtentChanged);
    CHECK(afterResize.vendorReset);
    CHECK(afterResize.vendorScalerGeneration == first.vendorScalerGeneration + 1);
    CHECK(afterResize.extents.renderWidth == 128);
    CHECK(afterResize.extents.renderHeight == 72);
    const auto output =
        readHalf4(*gpu, *(*renderer)->historyTexture(), (*renderer)->textureStates(),
                  lmx::render::TextureUse::ExternalWrite);
    CHECK(output.size() == 256 * 144);

    view.temporal.cameraCut = true;
    renderFrame(*gpu, **renderer, camera, view);
    CHECK((*renderer)->temporalStatus().lastReset == HistoryResetReason::CameraCut);
    CHECK((*renderer)->temporalStatus().vendorReset);
    CHECK((*renderer)->temporalStatus().vendorScalerGeneration ==
          afterResize.vendorScalerGeneration);
    view.temporal.cameraCut = false;
    view.temporal.enabled = false;
    renderFrame(*gpu, **renderer, camera, view);
    view.temporal.enabled = true;
    renderFrame(*gpu, **renderer, camera, view);
    CHECK((*renderer)->temporalStatus().vendorReset);
    CHECK((*renderer)->temporalStatus().vendorScalerGeneration ==
          afterResize.vendorScalerGeneration);
}

//======================================================================================================================
TEST_CASE("vendor frames overlap while scale and diagnostics change", "[gpu][temporal][vendor]") {
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    if (!gpu->upscalerSupport().available) {
        SKIP("The device reports no vendor temporal scaler capability");
    }
    auto plane = lmx::test::fixtureMesh(gpu->device(), lmx::engine::makePlane(32.0f),
                                        "lmx.test.vendorOverlap");
    REQUIRE(plane.has_value());
    const std::array<FixtureDrawItem, 1> items = {
        staticItem(*plane, facingPlaneModel(-2.0f), {0.6f, 0.6f, 0.6f, 1.0f})};
    FixtureSceneView view = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = true;
    view.temporal.reconstruction = ReconstructionMode::VendorTemporal;
    auto renderer = Renderer::create(*gpu, kScenarioWidth, kScenarioHeight, true);
    REQUIRE(renderer.has_value());
    for (uint32_t frame = 0; frame < 5; ++frame) {
        view.temporal.renderScale = frame % 2 == 0 ? 0.5f : 1.0f;
        view.temporal.debugView =
            frame % 2 == 0 ? TemporalDebugView::Off : TemporalDebugView::MotionVectors;
        renderFrameInFlight(*gpu, **renderer, Camera{}, view);
        CHECK((*renderer)->temporalStatus().vendorScalerGeneration == 1);
        if (frame > 0) {
            CHECK((*renderer)->temporalStatus().lastReset == HistoryResetReason::None);
            CHECK_FALSE((*renderer)->temporalStatus().vendorReset);
        }
    }
    gpu->waitIdle();
    const float mean = meanOver(
        luminances(readHalf4(*gpu, *(*renderer)->historyTexture(), (*renderer)->textureStates(),
                             lmx::render::TextureUse::ExternalWrite)),
        allPixels());
    CHECK(std::isfinite(mean));
    CHECK(mean > 0.01f);
}
