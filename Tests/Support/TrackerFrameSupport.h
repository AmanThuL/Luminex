#pragma once

#include "Support/OracleRoots.h"
#include "Support/TestGpu.h"

#include "Engine/Scene/Scene.h"
#include "Render/Graph/FrameDeclaration.h"
#include "Render/Renderer/Renderer.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Scenes/SceneLibrary.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace lmx::test {

struct TrackedFrameOptions {
    uint32_t width = 64;
    uint32_t height = 64;
    uint32_t frames = 4;
    std::function<void(render::SceneView&, uint32_t frame)> perFrame;
    std::function<bool(uint32_t frame)> readDisplay;
};

//======================================================================================================================
inline void runTrackedFrames(std::string_view sceneId, const TrackedFrameOptions& options) {
#ifdef NDEBUG
    (void)sceneId;
    (void)options;
    SKIP("the validation tracker runs only in Debug builds");
#else
    auto gpu = createTestGpu({.validation = true});
    scenes::SceneLibrary library(*gpu);
    engine::Scene* scene = nullptr;
    engine::Camera camera;
    if (sceneId != "empty") {
        const auto id = scenes::parseSceneId(sceneId);
        REQUIRE(id.has_value());
        auto loaded = library.get(*id);
        INFO(errorOf(loaded));
        REQUIRE(loaded.has_value());
        scene = *loaded;
        camera = engine::cameraFromScene(scene->initialCamera);
    }

    render::TransientPool pool(*gpu);
    auto rendererResult = render::Renderer::create(*gpu, options.width, options.height);
    INFO(errorOf(rendererResult));
    REQUIRE(rendererResult.has_value());
    auto renderer = std::move(*rendererResult);
    auto offscreenResult =
        engine::GpuTexture::create(gpu->device(), {.format = rojoRHI::Format::BGRA8Unorm,
                                                   .extent = {options.width, options.height, 1},
                                                   .usage = rojoRHI::TextureUsage::RenderTarget |
                                                            rojoRHI::TextureUsage::Sampled,
                                                   .label = "lmx.test.uiTarget"});
    INFO(errorOf(offscreenResult));
    REQUIRE(offscreenResult.has_value());
    auto offscreen = std::move(*offscreenResult);
    auto sampleLibrary = gpu->loadShaderLibrary("FullscreenSample");
    INFO(errorOf(sampleLibrary));
    REQUIRE(sampleLibrary.has_value());
    const std::array<rojoRHI::ColorTarget, 1> colors{{{.format = rojoRHI::Format::BGRA8Unorm}}};
    auto samplePipeline = engine::adoptGpu(
        gpu->device(),
        gpu->device().createPipeline({.vertex = {sampleLibrary->get(), "vertexMain"},
                                      .fragment = {sampleLibrary->get(), "fragmentMain"},
                                      .raster = {.cull = rojoRHI::CullMode::None},
                                      .depthStencil = {.depthTest = false,
                                                       .depthWrite = false,
                                                       .depthCompare = rojoRHI::CompareOp::Always},
                                      .colors = colors,
                                      .label = "lmx.test.uiPipeline"}));
    INFO(errorOf(samplePipeline));
    REQUIRE(samplePipeline.has_value());

    std::vector<engine::DrawItem> items;
    uint64_t lastSubmitted = 0;
    const auto displayTexture = renderer->colorTarget().rhi();
    auto& states = renderer->textureStates();
    for (uint32_t ordinal = 0; ordinal < options.frames; ++ordinal) {
        CAPTURE(ordinal);
        auto frame = gpu->beginFrame();
        if (scene) {
            auto prepared = scene->prepareFrame(frame.number);
            if (!prepared)
                gpu->submitAndEndFrame(frame);
            INFO(errorOf(prepared));
            REQUIRE(prepared.has_value());
        }
        render::SceneView view =
            scene ? render::buildSceneView(*scene, items, false) : render::SceneView{};
        if (options.perFrame)
            options.perFrame(view, ordinal);
        const bool readDisplay = !options.readDisplay || options.readDisplay(ordinal);
        const auto previousLogical = states.logicalUse(displayTexture);
        const auto previousPhysical = states.find(displayTexture, {0, 1, 0, 1});

        auto& commands = frame.commands;
        render::FrameDeclaration declaration(pool, *renderer, commands, camera, view, true);
        auto& graph = declaration.graph();
        const auto display = declaration.displayColor();
        const auto target = graph.importPersistentTexture(offscreen, "lmx.test.uiTarget");
        render::PassDesc ui{.color = render::ColorAttachment{.handle = target}};
        if (readDisplay)
            ui.textureReads.push_back(display);
        graph.addPass("lmx.test.ui", std::move(ui),
                      [&, display, readDisplay](const render::PassResources& resources) {
                          if (!readDisplay)
                              return;
                          const auto source = resources.texture(display);
                          commands.setPipeline(samplePipeline->get());
                          commands.pushRoot(
                              FullscreenSampleRoot{.source = static_cast<uint32_t>(source.view)});
                          const rojoRHI::ResourceAccess access{
                              .texture = source.texture,
                              .range = {0, 1, 0, 1},
                              .stage = rojoRHI::Stage::FragmentShader,
                              .access = rojoRHI::Access::ShaderRead};
                          commands.declareAccess(std::span(&access, 1));
                          commands.draw(3);
                      });
        graph.exportTexture(render::nextVersion(target));
        const auto record = declaration.execute();
        gpu->submitAndEndFrame(frame);
        lastSubmitted = frame.number;

        REQUIRE(record.frameId == frame.number);
        REQUIRE(frame.number == uint64_t{ordinal} + 1);
        const auto displayPass =
            std::ranges::find(record.debug.passes, "lmx.pass.display", &render::DebugPass::label);
        REQUIRE(displayPass != record.debug.passes.end());
        const auto passIndex = static_cast<uint32_t>(displayPass - record.debug.passes.begin());
        const bool scheduled = std::ranges::find(record.debug.schedule.passes, passIndex) !=
                               record.debug.schedule.passes.end();
        REQUIRE(scheduled == readDisplay);
        REQUIRE(displayPass->cullReason.has_value() == !readDisplay);
        if (!readDisplay) {
            REQUIRE(previousLogical.has_value());
            REQUIRE(previousPhysical.has_value());
            REQUIRE(displayPass->cullReason == render::CullReason::NoSinkReachesIt);
            REQUIRE(states.logicalUse(displayTexture) == previousLogical);
            REQUIRE(states.find(displayTexture, {0, 1, 0, 1}) == previousPhysical);
        }
    }
    gpu->waitIdle();
    REQUIRE(lastSubmitted == options.frames);
    REQUIRE(gpu->frameNumber() == lastSubmitted);
    REQUIRE(gpu->completedFrame() == lastSubmitted);
#endif
}

} // namespace lmx::test
