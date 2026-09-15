#include "GpuTestSupport.h"

#include "App/Model/FrameRecordRing.h"
#include "Render/FrameDeclaration.h"

#include "SceneTableTestSupport.h"
#include <algorithm>
#include <array>
#include <vector>

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
TEST_CASE("FrameDeclaration preserves headless rendering and retains its caller-selected sink",
          "[gpu][app][frame-declaration]") {
    using namespace lmx::rhi;
    using namespace lmx::render;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());
    TransientPool pool(**device);
    auto reference = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(reference));
    REQUIRE(reference.has_value());
    auto shared = Renderer::create(**device, kSize, kSize, /*cpuReadback=*/true);
    INFO(errorOf(shared));
    REQUIRE(shared.has_value());
    auto cube = fixtureMesh(**device, makeCube(), "lmx.test.frameDeclaration.cube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    const std::array<FixtureDrawItem, 1> items = {{
        {.mesh = &*cube, .material = {.albedo = {0.4f, 0.6f, 0.8f, 1.0f}}},
    }};
    FixtureSceneView view;
    view.items = items;
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 4.0f};
    view.lights[0] = {.strength = {2.0f, 2.0f, 2.0f}, .direction = {0.0f, 0.0f, -1.0f}};
    view.temporal.enabled = false;
    view.bloomEnabled = true;
    lmx::app::FrameRecordRing records;
    uint64_t firstSharedFrame = 0;

    // More than three frames reuses every transient slot; toggling pooling also replaces heaps.
    for (uint32_t index = 0; index < 5; ++index) {
        CommandList& referenceCommands = (*device)->beginFrame();
        (*reference)
            ->render(referenceCommands, camera, lmx::test::prepareSceneView(view, device),
                     /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();
        std::vector<uint8_t> expected(size_t{kSize} * kSize * 4);
        (*reference)->colorTarget().readback(expected.data(), expected.size());

        CommandList& commands = (*device)->beginFrame();
        const uint64_t frameId = (*device)->frameNumber();
        if (index == 0) {
            firstSharedFrame = frameId;
        }
        const bool poolingEnabled = index % 2 == 0;
        const auto prepared = view.prepare(**device);
        lmx::render::FrameDeclaration frame(pool, **shared, commands, camera, prepared,
                                            poolingEnabled);
        frame.graph().exportTexture(frame.displayColor());
        records.retain(frame.execute());
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();

        std::vector<uint8_t> actual(expected.size());
        (*shared)->colorTarget().readback(actual.data(), actual.size());
        REQUIRE(actual == expected);
        const lmx::app::RetainedFrame* retained = records.find(frameId);
        REQUIRE(retained != nullptr);
        const CompiledFrameDebug& debug = retained->record.debug;
        REQUIRE(debug.poolingEnabled == poolingEnabled);
        REQUIRE(debug.sinks.size() == 1);
        REQUIRE(debug.sinks[0].kind == SinkKind::Export);
        REQUIRE(debug.resources[debug.sinks[0].resource].name == "lmx.render.displayColor");
        REQUIRE_FALSE(debug.schedule.passes.empty());
        REQUIRE(debug.passes[debug.schedule.passes.back()].label == "lmx.pass.display");
        REQUIRE(std::ranges::none_of(
            debug.passes, [](const DebugPass& pass) { return pass.label == "lmx.pass.ui"; }));
        REQUIRE_FALSE(debug.transients.empty());
        if (!poolingEnabled) {
            REQUIRE(debug.memory.aliasSavings == 0);
        }
    }
    REQUIRE(records.size() == lmx::app::FrameRecordRing::kCapacity);
    REQUIRE(records.find(firstSharedFrame) == nullptr);
}
