#include "Engine/Scene/Scene.h"
#include "Render/Renderer/Renderer.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Support/GpuTestSupport.h"
#include "Support/SceneDocumentTestSupport.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("light diagnostics reuse actual temporal depth without contaminating history",
          "[gpu][light-debug]") {
    auto gpu = createTestGpu();
    auto scene = lmx::test::loadCatalogScene(gpu->device(), "light-lab", 256, 0);
    REQUIRE(scene.has_value());
    const auto camera = lmx::engine::cameraFromScene((*scene)->initialCamera);
    for (const auto reconstruction :
         {ReconstructionMode::NativeTaa, ReconstructionMode::VendorTemporal}) {
        std::vector<uint8_t> reference;
        for (bool debug : {false, true}) {
            auto renderer = Renderer::create(*gpu, 67, 43, true);
            REQUIRE(renderer.has_value());
            for (uint32_t frame = 0; frame < 7; ++frame) {
                auto gpuFrame2 = gpu->beginFrame();
                auto& commands = gpuFrame2.commands;
                REQUIRE((*scene)->prepareFrame(gpu->frameNumber()).has_value());
                std::vector<lmx::engine::DrawItem> items;
                auto view = buildSceneView(**scene, items, false);
                view.localLightMode = lmx::engine::LocalLightMode::Clustered;
                view.temporal.enabled = true;
                view.temporal.jitterEnabled = true;
                view.temporal.reconstruction = reconstruction;
                view.temporal.renderScale = 0.75f;
                view.bloomEnabled = false;
                view.lightDebugView = debug && frame < 6 ? lmx::engine::LightDebugView::Missed
                                                         : lmx::engine::LightDebugView::Off;
                const auto rendered = (*renderer)->render(commands, camera, view);
                gpu->submitAndEndFrame(gpuFrame2);
                INFO(errorOf(rendered));
                REQUIRE(rendered.has_value());
                gpu->waitIdle();
                if (debug && frame < 6) {
                    std::vector<uint8_t> pixels(67 * 43 * 4);
                    {
                        const auto readbackBytes = lmx::render::readbackNow(
                            *gpu, (*renderer)->colorTarget(), (*renderer)->textureStates());
                        INFO(errorOf(readbackBytes));
                        REQUIRE(readbackBytes.has_value());
                        std::memcpy(pixels.data(), readbackBytes->data(), pixels.size());
                    }
                    for (size_t at = 0; at < pixels.size(); at += 4) {
                        REQUIRE(pixels[at] == 0);
                        REQUIRE(pixels[at + 1] == 0);
                        REQUIRE(pixels[at + 2] == 0);
                        REQUIRE(pixels[at + 3] == 255);
                    }
                }
            }
            const auto drained = (*renderer)->drainLightingAfterIdle();
            INFO(errorOf(drained));
            REQUIRE(drained.has_value());
            std::vector<uint8_t> image(67 * 43 * 4);
            {
                const auto readbackBytes = lmx::render::readbackNow(
                    *gpu, (*renderer)->colorTarget(), (*renderer)->textureStates());
                INFO(errorOf(readbackBytes));
                REQUIRE(readbackBytes.has_value());
                std::memcpy(image.data(), readbackBytes->data(), image.size());
            }
            if (!debug)
                reference = std::move(image);
            else
                REQUIRE(image == reference);
        }
    }
}
