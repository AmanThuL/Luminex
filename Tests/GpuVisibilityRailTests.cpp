#include "Engine/Catalog/CatalogScenes.h"
#include "Engine/Catalog/SceneLibrary.h"
#include "GpuTestSupport.h"
#include "Render/SceneViewBuilder.h"

namespace {
namespace render = lmx::render;
namespace engine = lmx::engine;
} // namespace

//======================================================================================================================
TEST_CASE("GPU VisibilityLab states and canonical output equal the CPU at three rail times",
          "[gpu][visibility]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto loaded = engine::loadVisibilityLabScene(**device, 4096);
    REQUIRE(loaded);
    auto& world = **loaded;
    auto renderer = render::Renderer::create(**device, 160, 90, true);
    REQUIRE(renderer);
    auto camera = engine::cameraFromScene(world.initialCamera);
    for (auto mode : {render::SubmissionMode::Indirect, render::SubmissionMode::Batched}) {
        for (double seconds : {0.0, 3.0, 6.0}) {
            world.animationTime = seconds;
            world.followCameraTrack(camera);
            auto& commands = (*device)->beginFrame();
            REQUIRE(world.prepareFrame((*device)->frameNumber()));
            std::vector<engine::DrawItem> items;
            auto view = render::buildSceneView(world, items, render::ShadowFilter::PCF, false);
            view.classifyMode = render::ClassifyMode::Gpu;
            view.classifyCheck = true;
            view.submission = mode;
            view.temporal.enabled = false;
            view.bloomEnabled = false;
            const auto planes = render::extractFrustumPlanes(camera.projectionMatrix(16.0f / 9.0f) *
                                                             camera.viewMatrix());
            const auto expected = render::classifyView(planes, items, view.tables);
            (*renderer)->render(commands, camera, view, false);
            const auto declared = (*device)->frameNumber();
            (*device)->endFrame(nullptr);
            world.commitFrame();
            (*device)->waitIdle();
            (*renderer)->drainVisibilityAfterIdle();
            const auto results = (*renderer)->takeRetiredVisibility();
            REQUIRE(results.size() == 1);
            const auto& result = results[0];
            CAPTURE(seconds, result.stateMismatches, result.rowMismatches,
                    result.argumentMismatches, result.counterMismatches);
            REQUIRE(result.frameNumber == declared);
            REQUIRE(result.checkPassed());
            REQUIRE_FALSE(result.overflow);
            REQUIRE(result.scene.visible == expected.visible);
            REQUIRE(result.scene.rejected == expected.rejected);
            REQUIRE(result.scene.visibleItems == expected.visibleItems);
            REQUIRE(result.shadowCounters.emittedRows == 4096);
            if (seconds == 0)
                for (uint32_t i = 0; i < 5; ++i)
                    REQUIRE(result.scene.candidates[i].state == render::VisibilityState::Visible);
        }
    }
}

//======================================================================================================================
TEST_CASE("Empty GPU view and classifier switches preserve final frame diagnostics",
          "[gpu][visibility]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto renderer = render::Renderer::create(**device, 32, 32, true);
    REQUIRE(renderer);
    std::vector<uint64_t> gpuFrames;
    for (auto classifier : {render::ClassifyMode::Gpu, render::ClassifyMode::Cpu,
                            render::ClassifyMode::Gpu, render::ClassifyMode::Gpu}) {
        auto& commands = (*device)->beginFrame();
        render::SceneView view;
        view.temporal.enabled = false;
        view.classifyMode = classifier;
        view.classifyCheck = classifier == render::ClassifyMode::Gpu;
        view.submission = render::SubmissionMode::Batched;
        (*renderer)->render(commands, engine::Camera{}, view, false);
        if (classifier == render::ClassifyMode::Gpu)
            gpuFrames.push_back((*device)->frameNumber());
        (*device)->endFrame(nullptr);
    }
    (*device)->waitIdle();
    (*renderer)->drainVisibilityAfterIdle();
    const auto results = (*renderer)->takeRetiredVisibility();
    REQUIRE(results.size() == gpuFrames.size());
    for (size_t i = 0; i < results.size(); ++i) {
        REQUIRE(results[i].frameNumber == gpuFrames[i]);
        REQUIRE(results[i].checkPassed());
        REQUIRE(results[i].sceneCounters.candidates == 0);
        REQUIRE_FALSE(results[i].overflow);
    }
}
