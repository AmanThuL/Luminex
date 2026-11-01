#include "Engine/Gpu/PassTiming.h"
#include "Support/GpuRendererTestSupport.h"
#include "Support/SceneTableTestSupport.h"

#include <cstring>
#include <vector>
using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
TEST_CASE("pass timings name every pass the graph ran", "[gpu]") {
    using namespace rojoRHI;

    constexpr uint32_t kFrameSlots = 3;
    auto gpu = createTestGpu({.framesInFlight = kFrameSlots});

    auto cube =
        lmx::test::fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.timedCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(*gpu, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    const std::array<FixtureDrawItem, 2> items = twoCubeScene(*cube);

    auto gpuFrame6 = gpu->beginFrame();
    const uint64_t measuredFrame = gpuFrame6.number;
    auto& commands = gpuFrame6.commands;
    std::vector<lmx::engine::PassTiming> timings;
    bool measuredFramePublished = false;
    const auto collectTimings = [&] {
        const auto publication = gpu->passTimings();
        if (publication.frame == measuredFrame) {
            timings.assign(publication.passes.begin(), publication.passes.end());
            measuredFramePublished = true;
        }
    };
    const auto rendered = (*renderer)->render(
        commands, sceneCamera(), lmx::test::prepareSceneView(litSceneView(items), *gpu));
    if (!rendered) {
        gpu->submitAndEndFrame(gpuFrame6);
        collectTimings();
    }
    INFO(errorOf(rendered));
    REQUIRE(rendered.has_value());
    gpu->submitAndEndFrame(gpuFrame6);
    collectTimings();
    gpu->waitIdle();

    // A retired frame publishes only when a real frame slot is acquired and ended.
    for (uint32_t drained = 0; !measuredFramePublished && drained < kFrameSlots; ++drained) {
        auto drainFrame = gpu->beginFrame();
        gpu->submitAndEndFrame(drainFrame);
        collectTimings();
    }
    REQUIRE(measuredFramePublished);

    // Bloom is enabled by default (spec 10) and its passes always schedule with it -- kSize == 64
    // gives a 32-wide bloom base, which clamps to Renderer.cpp's full kMaxBloomDownsampleLevels ==
    // 4: threshold, 4 downsample passes, 4 upsample passes. Auto exposure is off by default (spec
    // 9), so the histogram/resolve/seed passes are culled and do not appear here -- see the
    // culling test in RenderGraphTests.cpp for that half of the picture.
    REQUIRE(timings.size() == 12);
    REQUIRE(timings[0].label == "lmx.pass.shadow");
    REQUIRE(timings[1].label == "lmx.pass.scene");
    REQUIRE(timings[2].label == "lmx.pass.bloom.threshold");
    REQUIRE(timings[3].label == "lmx.pass.bloom.downsample0");
    REQUIRE(timings[4].label == "lmx.pass.bloom.downsample1");
    REQUIRE(timings[5].label == "lmx.pass.bloom.downsample2");
    REQUIRE(timings[6].label == "lmx.pass.bloom.downsample3");
    REQUIRE(timings[7].label == "lmx.pass.bloom.upsample3");
    REQUIRE(timings[8].label == "lmx.pass.bloom.upsample2");
    REQUIRE(timings[9].label == "lmx.pass.bloom.upsample1");
    REQUIRE(timings[10].label == "lmx.pass.bloom.upsample0");
    REQUIRE(timings[11].label == "lmx.pass.display");
    for (const lmx::engine::PassTiming& timing : timings) {
        INFO(timing.label + ": " + std::to_string(timing.gpuMilliseconds) + " ms");
        REQUIRE(timing.gpuMilliseconds > 0.0);
    }
}
