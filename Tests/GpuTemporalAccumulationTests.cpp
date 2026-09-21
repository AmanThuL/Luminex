#include "GpuTemporalTestSupport.h"
#include "SceneTableTestSupport.h"

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
// Static stability (spec 11): a checker floor and five poles, camera still, jitter on, 32 frames.
// Jitter moves where every frame is sampled, so the raw picture keeps changing at every edge the
// checker's perspective compresses toward the horizon; accumulation is what is supposed to settle
// that without the scene having moved at all.
TEST_CASE("accumulation settles a static jittered frame", "[gpu][temporal]") {
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.scenarioCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(64.0f), "lmx.test.scenarioFloor");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<FixtureDrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const float poleWidth = 2.0f / pixelsPerUnit(camera, 12.0f);
    for (int32_t i = -2; i <= 2; ++i) {
        items.push_back(staticItem(
            *cube,
            boxModel({static_cast<float>(i) * 1.5f, 1.5f, -8.0f}, {poleWidth, 3.0f, poleWidth}),
            {1.0f, 1.0f, 1.0f, 1.0f}));
    }
    const FixtureSceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});

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
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.scenarioCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(64.0f), "lmx.test.scenarioFloor");
    INFO(errorOf(plane));
    REQUIRE(plane.has_value());

    // Looking down at the floor, with the poles standing on it and entirely below the horizon:
    // "over the floor" is what puts real geometry behind every pole pixel, so an uncovered sample
    // reprojects onto the floor rather than onto the empty background a silhouette against the sky
    // would leave -- which the disocclusion test must reject, and rightly does.
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
    const FixtureSceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});

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
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(32.0f), "lmx.test.scenarioWall");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 0.0f, 4.0f}, 0.0f);
    // Camera z 4 to quad z -6; the backdrop sits six units further back, which is the depth gap
    // the disocclusion test has to see in the band the quad vacates.
    constexpr float kQuadDistance = 10.0f;
    constexpr float kQuadHalfWidth = 1.0f;
    constexpr float kStartX = -3.0f;
    const float stepX = 4.0f / pixelsPerUnit(camera, kQuadDistance);

    std::vector<FixtureDrawItem> items;
    items.push_back(staticItem(*plane, facingPlaneModel(-12.0f), {0.6f, 0.6f, 0.6f, 1.0f}));
    items.push_back(staticItem(*cube, boxModel({kStartX, 0.0f, -6.0f}, {2.0f, 2.0f, 0.1f}),
                               {0.05f, 0.05f, 0.05f, 1.0f}));

    const auto quadCenterX = [&](uint32_t frame) {
        return kStartX + stepX * static_cast<float>(frame - 1);
    };
    const PerFrame animate = [&](uint32_t frame, FixtureSceneView& view, Camera&) {
        auto* drawn = const_cast<FixtureDrawItem*>(view.items.data());
        drawn[1].model = boxModel({quadCenterX(frame), 0.0f, -6.0f}, {2.0f, 2.0f, 0.1f});
        drawn[1].previousModel =
            boxModel({quadCenterX(frame == 1 ? 1 : frame - 1), 0.0f, -6.0f}, {2.0f, 2.0f, 0.1f});
    };
    // Facing the camera, so both the quad and the backdrop take the light and the two differ by
    // albedo alone -- which is what makes the luminance contrast a number the tolerance can scale.
    const FixtureSceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});

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
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(32.0f), "lmx.test.scenarioWall");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 0.0f, 4.0f}, 0.0f);
    std::vector<FixtureDrawItem> items;
    items.push_back(staticItem(*plane, facingPlaneModel(-12.0f), {0.4f, 0.4f, 0.4f, 1.0f}));
    items.push_back(staticItem(*cube, boxModel({0.0f, 0.0f, -6.0f}, {2.0f, 2.0f, 0.1f}),
                               {0.05f, 0.05f, 0.05f, 1.0f}));

    constexpr uint32_t kStepFrame = 24;
    const PerFrame animate = [](uint32_t frame, FixtureSceneView& view, Camera&) {
        auto* drawn = const_cast<FixtureDrawItem*>(view.items.data());
        drawn[1].material.emissive = frame >= kStepFrame ? glm::vec3{4.0f} : glm::vec3{0.0f};
    };
    const FixtureSceneView base = scenarioSceneView(items, {0.0f, 0.0f, -1.0f});

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
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<FixtureDrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const FixtureSceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});

    constexpr uint32_t kStepFrame = 24;
    const PerFrame animate = [](uint32_t frame, FixtureSceneView& view, Camera&) {
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
    using namespace rojoRHI;
    using lmx::render::HistoryResetReason;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<FixtureDrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const FixtureSceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});

    constexpr uint32_t kCutFrame = 20;
    constexpr uint32_t kFrames = kCutFrame + lmx::render::kTemporalWarmupFrames;
    const PerFrame animate = [](uint32_t frame, FixtureSceneView& view, Camera&) {
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
    using namespace rojoRHI;
    using lmx::render::HistoryResetReason;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<FixtureDrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    FixtureSceneView view = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});
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
    using namespace rojoRHI;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = lmx::test::fixtureMesh(**device, lmx::engine::makeCube(), "lmx.test.scenarioCube");
    REQUIRE(cube.has_value());
    auto plane =
        lmx::test::fixtureMesh(**device, lmx::engine::makePlane(64.0f), "lmx.test.scenarioFloor");
    REQUIRE(plane.has_value());

    const Camera camera = scenarioCamera({0.0f, 1.5f, 4.0f}, -0.20f);
    std::vector<FixtureDrawItem> items;
    appendCheckerFloor(items, *cube, *plane);
    const FixtureSceneView base = scenarioSceneView(items, {0.0f, -1.0f, -0.4f});

    const auto capture = [&](bool temporal) {
        auto renderer =
            Renderer::create(**device, kScenarioWidth, kScenarioHeight, /*cpuReadback=*/true);
        REQUIRE(renderer.has_value());
        FixtureSceneView view = base;
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
