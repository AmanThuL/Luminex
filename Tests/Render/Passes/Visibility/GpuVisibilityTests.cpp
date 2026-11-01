#include "Engine/Scene/Scene.h"

#include "Render/Renderer/SceneViewBuilder.h"
#include "Support/GpuTestSupport.h"
#include <cstring>

namespace {
namespace engine = lmx::engine;
namespace render = lmx::render;

//======================================================================================================================
engine::MeshData visibilityQuad() {
    return {.vertices = {{-0.2f, -0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                         {0.2f, -0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                         {0.2f, 0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                         {-0.2f, 0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
            .indices = {0, 1, 2, 0, 2, 3}};
}

//======================================================================================================================
engine::Scene visibilityScene() {
    engine::Scene result;
    result.name = "lmx.test.sceneTables";
    result.boundingSphere = {0.0f, 0.0f, -3.0f, 3.0f};
    for (auto& light : result.lights) {
        light.strength = {};
    }
    const auto mesh = result.addMesh(visibilityQuad(), "lmx.test.sceneTables.quad");
    const auto material = result.addMaterial({.albedo = {0, 0, 0, 1}, .emissive = {0.5f, 0, 0}});
    result.addObject({.position = {-0.6f, 0, -3}, .mesh = mesh, .material = material});
    result.addObject({.position = {0.6f, 0, -3}, .mesh = mesh, .material = material});
    return result;
}

//======================================================================================================================
std::unique_ptr<render::Renderer> visibilityRenderer(engine::GpuContext& gpu) {
    auto renderer = render::Renderer::create(gpu, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());
    for (uint32_t i = 0; i < 3; ++i) {
        (*renderer)->clearColor[i] = 0;
    }
    return std::move(*renderer);
}

//======================================================================================================================
engine::GpuBuffer submitVisibility(engine::GpuContext& gpu, engine::Scene& scene,
                                   render::Renderer& renderer, render::SubmissionMode mode,
                                   bool cull, bool wait = true) {
    auto snapshot = engine::GpuBuffer::create(gpu.device(),
                                              {.size = uint64_t{kSize} * kSize * 4,
                                               .cpuReadback = true,
                                               .label = "lmx.test.sceneTables.frame"},
                                              nullptr);
    REQUIRE(snapshot.has_value());
    auto frame = gpu.beginFrame();
    auto& commands = frame.commands;
    auto prepared = scene.prepareFrame(gpu.frameNumber());
    INFO(errorOf(prepared));
    REQUIRE(prepared.has_value());
    std::vector<engine::DrawItem> items;
    auto view = render::buildSceneView(scene, items, false);
    view.bloomEnabled = false;
    view.submission = mode;
    view.visibilityEnabled = cull;
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = false;
    view.temporal.reconstruction = render::ReconstructionMode::Raw;
    view.temporal.sceneGeneration = scene.objects.front().id.store;
    renderer.render(commands, engine::Camera{}, view);
    commands.textureBarrier(renderer.colorTarget().rhi(), rojoRHI::TextureUse::RenderTarget,
                            rojoRHI::TextureUse::CopySource);
    commands.beginCopyPass("lmx.test.sceneTables.preserveFrame");
    commands.copyTextureToBuffer(renderer.colorTarget().rhi(), {.width = kSize, .height = kSize},
                                 snapshot->rhi(), {.bytesPerRow = kSize * 4});
    commands.endCopyPass();
    commands.textureBarrier(renderer.colorTarget().rhi(), rojoRHI::TextureUse::CopySource,
                            rojoRHI::TextureUse::RenderTarget);
    commands.beginRenderPass({.colorTarget = &renderer.colorTarget().rhi(),
                              .clear = false,
                              .label = "lmx.test.sceneTables.restoreTarget"});
    commands.endRenderPass();
    gpu.submitAndEndFrame(frame);
    scene.commitFrame();
    if (wait) {
        gpu.waitIdle();
    }
    return std::move(*snapshot);
}

//======================================================================================================================
std::vector<uint8_t> visibilityPixels(engine::GpuContext& gpu, const engine::GpuBuffer& buffer) {
    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    {
        const auto readbackBytes = lmx::render::readbackNow(
            gpu, buffer, lmx::render::BufferUse::CopyDestination, {0, pixels.size()});
        std::memcpy(pixels.data(), readbackBytes.data(), pixels.size());
    }
    return pixels;
}

} // namespace

//======================================================================================================================

//======================================================================================================================

//======================================================================================================================
TEST_CASE("draw submission slots grow and retire with paced scene capacity",
          "[gpu][visibility][submission]") {
    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);
    auto scene = visibilityScene();
    REQUIRE(scene.finalize(gpu->device()));
    render::DrawSubmission submission(gpu->device());
    std::array<const lmx::engine::GpuBuffer*, 3> rows{};
    std::array<std::vector<uint32_t>, 3> expected;
    uint64_t grownFrame = 0;
    for (uint32_t frame = 0; frame < 7; ++frame) {
        auto commandsFrame = gpu->beginFrame();
        auto& commands = commandsFrame.commands;
        (void)commands;
        if (frame == 3) {
            const auto mesh = scene.objects[0].mesh;
            const auto material = scene.objects[0].material;
            for (uint32_t i = 0; i < 10; ++i)
                scene.addObject({.position = {0, 0, -3}, .mesh = mesh, .material = material});
            grownFrame = gpu->frameNumber();
        }
        REQUIRE(scene.prepareFrame(gpu->frameNumber()));
        std::vector<engine::DrawItem> items;
        auto view = render::buildSceneView(scene, items, false);
        render::VisibilityResult camera;
        camera.visibleItems = {frame % 2};
        render::VisibilityResult shadow;
        shadow.visibleItems = {0, 1};
        REQUIRE(submission.prepare(gpu->frameNumber(), view, camera, shadow));
        const auto slot = gpu->frameNumber() % 3;
        rows[slot] = submission.scene().rows;
        expected[slot] = {items[frame % 2].instanceRow, items[0].instanceRow, items[1].instanceRow};
        if (frame == 3)
            REQUIRE(submission.stats().pendingReleaseBuffers == 6);
        if (grownFrame && gpu->frameNumber() >= grownFrame + 2)
            REQUIRE(submission.stats().pendingReleaseBuffers == 0);
        gpu->submitAndEndFrame(commandsFrame);
        scene.commitFrame();
    }
    gpu->waitIdle();
    for (uint32_t slot = 0; slot < 3; ++slot) {
        std::vector<uint32_t> actual(3);
        {
            const auto readbackBytes =
                lmx::render::readbackNow(*gpu, *rows[slot], lmx::render::BufferUse::ShaderRead,
                                         {0, actual.size() * sizeof(uint32_t)});
            std::memcpy(actual.data(), readbackBytes.data(), actual.size() * sizeof(uint32_t));
        }
        REQUIRE(actual == expected[slot]);
    }
}
