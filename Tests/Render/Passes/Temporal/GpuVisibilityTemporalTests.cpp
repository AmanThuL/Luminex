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
// One frame of the scene, with a preserving copy of the display target recorded on the same command
// list after the frame's own passes, so each submitted frame keeps its own picture.
render::ReadbackTicket submitVisibility(engine::GpuContext& gpu, engine::Scene& scene,
                                        render::Renderer& renderer, render::SubmissionMode mode,
                                        bool cull, bool wait = true) {
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
    const auto rendered = renderer.render(commands, engine::Camera{}, view);
    if (!rendered)
        gpu.submitAndEndFrame(frame);
    INFO(errorOf(rendered));
    REQUIRE(rendered.has_value());
    auto snapshot =
        render::recordReadback(gpu, commands, renderer.colorTarget(), renderer.textureStates());
    gpu.submitAndEndFrame(frame);
    scene.commitFrame();
    if (wait) {
        gpu.waitIdle();
    }
    return snapshot;
}

//======================================================================================================================
std::vector<uint8_t> visibilityPixels(engine::GpuContext& gpu,
                                      const render::ReadbackTicket& ticket) {
    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    const auto resolved = render::resolve(gpu, ticket, pixels);
    INFO(errorOf(resolved));
    REQUIRE(resolved.has_value());
    return pixels;
}

} // namespace

//======================================================================================================================
TEST_CASE("production visibility and submission modes preserve raster output",
          "[gpu][visibility]") {
    auto gpu = createTestGpu();
    auto scene = visibilityScene();
    const auto mesh = scene.objects.front().mesh;
    const auto material = scene.objects.front().material;
    // Shared material still has per-instance emissive strength: flat row transport must survive
    // batching all vertices and fragments, including the temporal reactive attachment.
    scene.objects[1].emissiveStrength = 0.4f;
    for (const glm::vec3 p : {glm::vec3{-100, 0, -3}, glm::vec3{100, 0, -3}, glm::vec3{0, -100, -3},
                              glm::vec3{0, 100, -3}, glm::vec3{0, 0, 3}})
        scene.addObject({.position = p, .mesh = mesh, .material = material});
    const float edge = 3.0f * std::tan(engine::Camera{}.fovY * 0.5f);
    scene.addObject({.position = {edge, 0, -3}, .mesh = mesh, .material = material});
    REQUIRE(scene.finalize(gpu->device()));
    auto renderer = visibilityRenderer(*gpu);
    auto baseline = submitVisibility(*gpu, scene, *renderer, render::SubmissionMode::Direct, false);
    const auto reference = visibilityPixels(*gpu, baseline);
    for (const auto mode : {render::SubmissionMode::Direct, render::SubmissionMode::Indirect,
                            render::SubmissionMode::Batched}) {
        for (bool cull : {false, true}) {
            auto snapshot = submitVisibility(*gpu, scene, *renderer, mode, cull);
            REQUIRE(visibilityPixels(*gpu, snapshot) == reference);
            const auto& status = renderer->visibilityStatus();
            REQUIRE(status.scene.candidates.size() == 8);
            REQUIRE(status.scene.rejected == (cull ? 5 : 0));
            REQUIRE(status.shadow.rejected == 0);
            REQUIRE(status.scene.candidates.back().state ==
                    (cull ? render::VisibilityState::Visible : render::VisibilityState::Bypassed));
            REQUIRE(status.submission.sceneCommands ==
                    (mode == render::SubmissionMode::Batched ? 1 : (cull ? 3 : 8)));
        }
    }
}

//======================================================================================================================
TEST_CASE("visibility buffers retain four overlapping frame lists and images",
          "[gpu][visibility]") {
    auto gpu = createTestGpu();
    auto scene = visibilityScene();
    REQUIRE(scene.finalize(gpu->device()));
    auto renderer = visibilityRenderer(*gpu);
    std::array<render::ReadbackTicket, 4> snapshots;
    for (uint32_t frame = 0; frame < 4; ++frame) {
        if (frame == 2) {
            const auto mesh = scene.objects[0].mesh;
            const auto material = scene.objects[0].material;
            // Grow while both earlier frames still own the old submission allocations.
            for (uint32_t i = 0; i < 10; ++i)
                scene.addObject({.position = {100, 100, -3}, .mesh = mesh, .material = material});
        }
        scene.objects[0].position.x = frame % 2 == 0 ? -0.6f : -100;
        scene.objects[1].position.x = frame % 2 == 0 ? 100 : 0.6f;
        snapshots[frame] =
            submitVisibility(*gpu, scene, *renderer, render::SubmissionMode::Indirect, true, false);
        REQUIRE(renderer->visibilityStatus().scene.rejected == (frame < 2 ? 1 : 11));
        if (frame == 2)
            REQUIRE(renderer->visibilityStatus().submission.pendingReleaseBuffers == 6);
    }
    gpu->waitIdle();
    for (uint32_t frame = 0; frame < 4; ++frame) {
        const auto pixels = visibilityPixels(*gpu, snapshots[frame]);
        for (uint32_t side = 0; side < 2; ++side) {
            const float x = side == 0 ? -0.6f : 0.6f;
            const auto clip = engine::Camera{}.projectionMatrix(1.0f) * glm::vec4(x, 0, -3, 1);
            const uint32_t pixelX = static_cast<uint32_t>((clip.x / clip.w * 0.5f + 0.5f) * kSize);
            const auto pixel = pixelAt(pixels, pixelX, kSize / 2);
            INFO("frame " << frame << " side " << side);
            if (side == frame % 2)
                REQUIRE(pixel.r > 100);
            else
                REQUIRE(pixel.r < 5);
            REQUIRE(pixel.g < 5);
            REQUIRE(pixel.b < 5);
        }
    }
    REQUIRE(visibilityPixels(*gpu, snapshots[0]) == visibilityPixels(*gpu, snapshots[2]));
    REQUIRE(visibilityPixels(*gpu, snapshots[1]) == visibilityPixels(*gpu, snapshots[3]));
    REQUIRE(visibilityPixels(*gpu, snapshots[0]) != visibilityPixels(*gpu, snapshots[1]));
}
