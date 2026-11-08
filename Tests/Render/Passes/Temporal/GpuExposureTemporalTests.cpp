//----------------------------------------------------------------------------------------------------------------------
/// @file GpuExposureTemporalTests.cpp
/// @brief Preserves manual temporal exposure history conformance.
//----------------------------------------------------------------------------------------------------------------------

#include "Support/ExposureBloomTestSupport.h"
#include "Support/GpuTestSupport.h"
#include "Support/SceneTableTestSupport.h"

#include "Engine/Asset/Model/GeometryGenerator.h"
#include "Engine/Geometry/Mesh.h"
#include "Engine/View/Camera.h"
#include "Render/Renderer/Renderer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>

using lmx::test::FixtureDrawItem;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

//======================================================================================================================
// The whole frame this time, not one kernel: a manual-mode temporal frame has to leave the applied
// exposure and the one before it in the buffer, because that pair is what a temporal resolve
// corrects a reprojected history by (M6.2 spec 7). The renderer declares the seed on every such
// frame and exports what it wrote so dead-pass culling cannot drop it -- nothing in the frame
// itself reads it, its consumer is the next frame -- and the last case here is the other half of
// that rule: with temporal off, manual mode declares no seed at all, so the buffer is untouched
// and the pre-temporal frame's declaration stays exact.
TEST_CASE("a manual temporal frame records the EV edit and the value before it", "[gpu]") {
    using namespace lmx::render;

    constexpr uint32_t kExtent = 32;

    auto created = lmx::engine::GpuContext::create({});
    INFO(lmx::test::errorOf(created));
    REQUIRE(created);
    auto gpu = std::move(*created);

    auto cube = fixtureMesh(gpu->device(), lmx::engine::makeCube(), "lmx.test.exposurePairCube");
    INFO(lmx::test::errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(*gpu, kExtent, kExtent, /*cpuReadback=*/true);
    INFO(lmx::test::errorOf(renderer));
    REQUIRE(renderer.has_value());

    lmx::engine::Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};

    const std::array<FixtureDrawItem, 1> items = {FixtureDrawItem{.mesh = &*cube}};
    FixtureSceneView view;
    view.items = items;
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 4.0f};
    view.temporal.enabled = true;

    const auto renderOneFrame = [&](float exposureEv, bool temporalEnabled) {
        view.exposureEv = exposureEv;
        view.temporal.enabled = temporalEnabled;
        auto commandsFrame = gpu->beginFrame();
        auto& commands = commandsFrame.commands;
        const auto rendered =
            (*renderer)->render(commands, camera, lmx::test::prepareSceneView(view, *gpu));
        gpu->submitAndEndFrame(commandsFrame);
        gpu->waitIdle();
        INFO(lmx::test::errorOf(rendered));
        REQUIRE(rendered.has_value());

        std::array<float, lmx::render::kExposureBufferFloats> pair{};
        {
            const auto readbackBytes = lmx::render::readbackNow(
                *gpu, (*renderer)->exposureBufferOwner(),
                {rojoRHI::Stage::ComputeShader,
                 rojoRHI::Access::ShaderRead | rojoRHI::Access::ShaderWrite,
                 rojoRHI::Layout::Undefined},
                {0, sizeof(pair)});
            INFO(lmx::test::errorOf(readbackBytes));
            REQUIRE(readbackBytes.has_value());
            std::memcpy(pair.data(), readbackBytes->data(), sizeof(pair));
        }
        return pair;
    };

    // EV 0 first: the pair records unit exposure over whatever the buffer was created holding.
    const auto initial = renderOneFrame(0.0f, /*temporalEnabled=*/true);
    REQUIRE(initial[0] == Catch::Approx(1.0f));

    // The edit. The frame that applies exp2(2) is the frame that has to remember exp2(0).
    const auto edited = renderOneFrame(2.0f, /*temporalEnabled=*/true);
    REQUIRE(edited[0] == Catch::Approx(4.0f));
    REQUIRE(edited[1] == Catch::Approx(1.0f));

    // Temporal off declares no seed, so a further EV edit reaches shading through PassUniforms
    // alone and leaves the buffer exactly as the last temporal frame left it.
    const auto untouched = renderOneFrame(-1.0f, /*temporalEnabled=*/false);
    REQUIRE(untouched[0] == Catch::Approx(4.0f));
    REQUIRE(untouched[1] == Catch::Approx(1.0f));
}
