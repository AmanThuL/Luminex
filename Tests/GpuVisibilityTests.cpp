#include "GpuTestSupport.h"
#include "Scene/Scene.h"

namespace {
namespace scene = lmx::scene;
namespace render = lmx::render;
namespace rhi = rojoRHI;

//======================================================================================================================
render::MeshData visibilityQuad() {
    return {.vertices = {{-0.2f, -0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                         {0.2f, -0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                         {0.2f, 0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                         {-0.2f, 0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
            .indices = {0, 1, 2, 0, 2, 3}};
}

//======================================================================================================================
scene::Scene visibilityScene() {
    scene::Scene result;
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
std::unique_ptr<render::Renderer> visibilityRenderer(rojoRHI::Device& device) {
    auto renderer = render::Renderer::create(device, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());
    for (uint32_t i = 0; i < 3; ++i) {
        (*renderer)->clearColor[i] = 0;
    }
    return std::move(*renderer);
}

//======================================================================================================================
std::unique_ptr<rojoRHI::Buffer> submitVisibility(rojoRHI::Device& device, scene::Scene& scene,
                                              render::Renderer& renderer,
                                              render::SubmissionMode mode, bool cull,
                                              bool wait = true) {
    auto snapshot = device.createBuffer({.size = uint64_t{kSize} * kSize * 4,
                                         .cpuReadback = true,
                                         .label = "lmx.test.sceneTables.frame"},
                                        nullptr);
    REQUIRE(snapshot.has_value());
    auto& commands = device.beginFrame();
    auto prepared = scene.prepareFrame(device.frameNumber());
    INFO(errorOf(prepared));
    REQUIRE(prepared.has_value());
    std::vector<render::DrawItem> items;
    auto view = scene.view(items, render::ShadowFilter::PCF, false);
    view.bloomEnabled = false;
    view.submission = mode;
    view.visibilityEnabled = cull;
    view.temporal.enabled = true;
    view.temporal.jitterEnabled = false;
    view.temporal.reconstruction = render::ReconstructionMode::Raw;
    view.temporal.sceneGeneration = scene.objects.front().id.store;
    renderer.render(commands, render::Camera{}, view, false);
    commands.textureBarrier(renderer.colorTarget(), rojoRHI::TextureUse::RenderTarget,
                            rojoRHI::TextureUse::CopySource);
    commands.beginCopyPass("lmx.test.sceneTables.preserveFrame");
    commands.copyTextureToBuffer(renderer.colorTarget(), {.width = kSize, .height = kSize},
                                 **snapshot, {.bytesPerRow = kSize * 4});
    commands.endCopyPass();
    commands.textureBarrier(renderer.colorTarget(), rojoRHI::TextureUse::CopySource,
                            rojoRHI::TextureUse::RenderTarget);
    commands.beginRenderPass({.colorTarget = &renderer.colorTarget(),
                              .clear = false,
                              .label = "lmx.test.sceneTables.restoreTarget"});
    commands.endRenderPass();
    device.endFrame(nullptr);
    scene.commitFrame();
    if (wait) {
        device.waitIdle();
    }
    return std::move(*snapshot);
}

//======================================================================================================================
std::vector<uint8_t> visibilityPixels(rojoRHI::Buffer& buffer) {
    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    buffer.readback(pixels.data(), pixels.size());
    return pixels;
}

} // namespace

//======================================================================================================================
TEST_CASE("production visibility and submission modes preserve raster output",
          "[gpu][visibility]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto scene = visibilityScene();
    const auto mesh = scene.objects.front().mesh;
    const auto material = scene.objects.front().material;
    // Shared material still has per-instance emissive strength: flat row transport must survive
    // batching all vertices and fragments, including the temporal reactive attachment.
    scene.objects[1].emissiveStrength = 0.4f;
    for (const glm::vec3 p : {glm::vec3{-100, 0, -3}, glm::vec3{100, 0, -3}, glm::vec3{0, -100, -3},
                              glm::vec3{0, 100, -3}, glm::vec3{0, 0, 3}})
        scene.addObject({.position = p, .mesh = mesh, .material = material});
    const float edge = 3.0f * std::tan(render::Camera{}.fovY * 0.5f);
    scene.addObject({.position = {edge, 0, -3}, .mesh = mesh, .material = material});
    REQUIRE(scene.finalize(**device));
    auto renderer = visibilityRenderer(**device);
    auto baseline =
        submitVisibility(**device, scene, *renderer, render::SubmissionMode::Direct, false);
    const auto reference = visibilityPixels(*baseline);
    for (const auto mode : {render::SubmissionMode::Direct, render::SubmissionMode::Indirect,
                            render::SubmissionMode::Batched}) {
        for (bool cull : {false, true}) {
            auto snapshot = submitVisibility(**device, scene, *renderer, mode, cull);
            REQUIRE(visibilityPixels(*snapshot) == reference);
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
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto scene = visibilityScene();
    REQUIRE(scene.finalize(**device));
    auto renderer = visibilityRenderer(**device);
    std::array<std::unique_ptr<rojoRHI::Buffer>, 4> snapshots;
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
        snapshots[frame] = submitVisibility(**device, scene, *renderer,
                                            render::SubmissionMode::Indirect, true, false);
        REQUIRE(renderer->visibilityStatus().scene.rejected == (frame < 2 ? 1 : 11));
        if (frame == 2)
            REQUIRE(renderer->visibilityStatus().submission.pendingReleaseBuffers == 6);
    }
    (*device)->waitIdle();
    for (uint32_t frame = 0; frame < 4; ++frame) {
        const auto pixels = visibilityPixels(*snapshots[frame]);
        for (uint32_t side = 0; side < 2; ++side) {
            const float x = side == 0 ? -0.6f : 0.6f;
            const auto clip = render::Camera{}.projectionMatrix(1.0f) * glm::vec4(x, 0, -3, 1);
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
    REQUIRE(visibilityPixels(*snapshots[0]) == visibilityPixels(*snapshots[2]));
    REQUIRE(visibilityPixels(*snapshots[1]) == visibilityPixels(*snapshots[3]));
    REQUIRE(visibilityPixels(*snapshots[0]) != visibilityPixels(*snapshots[1]));
}

//======================================================================================================================
TEST_CASE("draw submission slots grow and retire with paced scene capacity",
          "[gpu][visibility][submission]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device);
    auto scene = visibilityScene();
    REQUIRE(scene.finalize(**device));
    render::DrawSubmission submission(**device);
    std::array<rojoRHI::Buffer*, 3> rows{};
    std::array<std::vector<uint32_t>, 3> expected;
    uint64_t grownFrame = 0;
    for (uint32_t frame = 0; frame < 7; ++frame) {
        auto& commands = (*device)->beginFrame();
        (void)commands;
        if (frame == 3) {
            const auto mesh = scene.objects[0].mesh;
            const auto material = scene.objects[0].material;
            for (uint32_t i = 0; i < 10; ++i)
                scene.addObject({.position = {0, 0, -3}, .mesh = mesh, .material = material});
            grownFrame = (*device)->frameNumber();
        }
        REQUIRE(scene.prepareFrame((*device)->frameNumber()));
        std::vector<render::DrawItem> items;
        auto view = scene.view(items, render::ShadowFilter::PCF, false);
        render::VisibilityResult camera;
        camera.visibleItems = {frame % 2};
        render::VisibilityResult shadow;
        shadow.visibleItems = {0, 1};
        REQUIRE(submission.prepare((*device)->frameNumber(), view, camera, shadow));
        const auto slot = (*device)->frameNumber() % 3;
        rows[slot] = submission.scene().rows;
        expected[slot] = {items[frame % 2].instanceRow, items[0].instanceRow, items[1].instanceRow};
        if (frame == 3)
            REQUIRE(submission.stats().pendingReleaseBuffers == 6);
        if (grownFrame && (*device)->frameNumber() >= grownFrame + 2)
            REQUIRE(submission.stats().pendingReleaseBuffers == 0);
        (*device)->endFrame(nullptr);
        scene.commitFrame();
    }
    (*device)->waitIdle();
    for (uint32_t slot = 0; slot < 3; ++slot) {
        std::vector<uint32_t> actual(3);
        rows[slot]->readback(actual.data(), actual.size() * sizeof(uint32_t));
        REQUIRE(actual == expected[slot]);
    }
}
