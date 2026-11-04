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

} // namespace

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

        if (frame == 3) {
            const auto mesh = scene.objects[0].mesh;
            const auto material = scene.objects[0].material;
            for (uint32_t i = 0; i < 10; ++i)
                scene.addObject({.position = {0, 0, -3}, .mesh = mesh, .material = material});
            grownFrame = gpu->frameNumber();
        }
        const auto scenePrepared = scene.prepareFrame(gpu->frameNumber());
        if (!scenePrepared)
            gpu->submitAndEndFrame(commandsFrame);
        REQUIRE(scenePrepared);
        std::vector<engine::DrawItem> items;
        auto view = render::buildSceneView(scene, items, false);
        render::VisibilityResult camera;
        camera.visibleItems = {frame % 2};
        render::VisibilityResult shadow;
        shadow.visibleItems = {0, 1};
        const auto prepared =
            submission.prepare(commands, gpu->frameNumber(), view, camera, shadow);
        if (!prepared)
            gpu->submitAndEndFrame(commandsFrame);
        REQUIRE(prepared);
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
                lmx::render::readbackNow(*gpu, *rows[slot],
                                         {rojoRHI::Stage::VertexShader, rojoRHI::Access::ShaderRead,
                                          rojoRHI::Layout::Undefined},
                                         {0, actual.size() * sizeof(uint32_t)});
            REQUIRE(readbackBytes);
            std::memcpy(actual.data(), readbackBytes->data(), actual.size() * sizeof(uint32_t));
        }
        REQUIRE(actual == expected[slot]);
    }
}
