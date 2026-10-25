#include "Engine/Scene/Scene.h"
#include "Render/Renderer/SceneViewBuilder.h"
#include "Support/GpuTestSupport.h"

#include <catch2/catch_approx.hpp>

#include <glm/gtc/packing.hpp>

#include <cstring>

namespace {
namespace engine = lmx::engine;
namespace render = lmx::render;

//======================================================================================================================
engine::MeshData tableQuad() {
    return {.vertices = {{-0.2f, -0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                         {0.2f, -0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                         {0.2f, 0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                         {-0.2f, 0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
            .indices = {0, 1, 2, 0, 2, 3}};
}

//======================================================================================================================
engine::Scene tableScene() {
    engine::Scene result;
    result.name = "lmx.test.sceneTables";
    result.boundingSphere = {0.0f, 0.0f, -3.0f, 3.0f};
    for (auto& light : result.lights) {
        light.strength = {};
    }
    const auto mesh = result.addMesh(tableQuad(), "lmx.test.sceneTables.quad");
    const auto material = result.addMaterial({.albedo = {0, 0, 0, 1}, .emissive = {0.5f, 0, 0}});
    result.addObject({.position = {-0.6f, 0, -3}, .mesh = mesh, .material = material});
    result.addObject({.position = {0.6f, 0, -3}, .mesh = mesh, .material = material});
    return result;
}

//======================================================================================================================
std::unique_ptr<render::Renderer> tableRenderer(engine::GpuContext& gpu) {
    auto renderer = render::Renderer::create(gpu, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());
    for (uint32_t i = 0; i < 3; ++i) {
        (*renderer)->clearColor[i] = 0;
    }
    return std::move(*renderer);
}

//======================================================================================================================
engine::GpuBuffer submitTables(engine::GpuContext& gpu, engine::Scene& scene,
                               render::Renderer& renderer, bool wait = true) {
    auto snapshot = engine::GpuBuffer::create(gpu.device(),
                                              {.size = uint64_t{kSize} * kSize * 4,
                                               .cpuReadback = true,
                                               .label = "lmx.test.sceneTables.frame"},
                                              nullptr);
    REQUIRE(snapshot.has_value());
    auto gpuFrame = gpu.beginFrame();
    auto& commands = gpuFrame.commands;
    auto prepared = scene.prepareFrame(gpu.frameNumber());
    INFO(errorOf(prepared));
    REQUIRE(prepared.has_value());
    std::vector<engine::DrawItem> items;
    auto view = render::buildSceneView(scene, items, false);
    view.bloomEnabled = false;
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
    gpu.submitAndEndFrame(gpuFrame);
    scene.commitFrame();
    if (wait) {
        gpu.waitIdle();
    }
    return std::move(*snapshot);
}

//======================================================================================================================
std::vector<uint8_t> tablePixels(engine::GpuContext& gpu, const engine::GpuBuffer& buffer) {
    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    {
        const auto bytes = lmx::render::readbackNow(
            gpu, buffer, lmx::render::BufferUse::CopyDestination, {0, pixels.size()});
        std::memcpy(pixels.data(), bytes.data(), pixels.size());
    }
    return pixels;
}

//======================================================================================================================
uint32_t tablePixelX(float x) {
    const engine::Camera camera;
    const glm::vec4 clip = camera.projectionMatrix(1.0f) * glm::vec4(x, 0, -3, 1);
    return static_cast<uint32_t>((clip.x / clip.w * 0.5f + 0.5f) * kSize);
}

//======================================================================================================================
void requireRed(const std::vector<uint8_t>& pixels, float x) {
    const Pixel pixel = pixelAt(pixels, tablePixelX(x), kSize / 2);
    INFO(describe("scene table red", tablePixelX(x), kSize / 2, pixel));
    REQUIRE(pixel.r > 100);
    REQUIRE(pixel.g < 10);
    REQUIRE(pixel.b < 10);
}

//======================================================================================================================
void requireBlack(const std::vector<uint8_t>& pixels, float x) {
    const Pixel pixel = pixelAt(pixels, tablePixelX(x), kSize / 2);
    REQUIRE(pixel.r < 5);
    REQUIRE(pixel.g < 5);
    REQUIRE(pixel.b < 5);
}

//======================================================================================================================
std::vector<engine::InstanceRow> instanceRows(engine::GpuContext& gpu, engine::Scene& scene) {
    const auto tables = scene.tables();
    std::vector<engine::InstanceRow> rows(tables.instanceCount);
    {
        const auto bytes =
            lmx::render::readbackNow(gpu, *tables.instances, lmx::render::BufferUse::ShaderRead,
                                     {0, rows.size() * sizeof(engine::InstanceRow)});
        std::memcpy(rows.data(), bytes.data(), rows.size() * sizeof(engine::InstanceRow));
    }
    return rows;
}

//======================================================================================================================
glm::vec2 tableMotion(engine::GpuContext& gpu, render::Renderer& renderer, float x) {
    std::vector<uint32_t> motion(size_t{kSize} * kSize);
    {
        const auto bytes = lmx::render::readbackNow(gpu, *renderer.motionTexture(),
                                                    lmx::render::TextureUse::RenderTarget);
        std::memcpy(motion.data(), bytes.data(), motion.size() * sizeof(uint32_t));
    }
    return glm::unpackHalf2x16(motion[size_t{kSize / 2} * kSize + tablePixelX(x)]);
}

} // namespace

//======================================================================================================================
TEST_CASE("Scene tables share geometry and material without sharing motion",
          "[gpu][scene-tables]") {
    auto gpu = createTestGpu();
    auto scene = tableScene();
    REQUIRE(scene.finalize(gpu->device()).has_value());
    auto renderer = tableRenderer(*gpu);
    const auto first = scene.objects[0].id;
    const auto second = scene.objects[1].id;
    REQUIRE(first != second);
    REQUIRE(scene.objects[0].mesh == scene.objects[1].mesh);
    REQUIRE(scene.objects[0].material == scene.objects[1].material);
    auto frame = submitTables(*gpu, scene, *renderer);
    requireRed(tablePixels(*gpu, frame), -0.6f);
    requireRed(tablePixels(*gpu, frame), 0.6f);
    scene.tryObject(first)->position.x = -0.2f;
    frame = submitTables(*gpu, scene, *renderer);
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::None);
    requireBlack(tablePixels(*gpu, frame), -0.6f);
    requireRed(tablePixels(*gpu, frame), -0.2f);
    requireRed(tablePixels(*gpu, frame), 0.6f);
    const auto rows = instanceRows(*gpu, scene);
    REQUIRE(rows[first.slot].previousModel[3].x == -0.6f);
    REQUIRE(rows[first.slot].model[3].x == -0.2f);
    REQUIRE(rows[second.slot].previousModel == rows[second.slot].model);
    REQUIRE(tableMotion(*gpu, *renderer, 0.6f) == glm::vec2(0));
    REQUIRE(tableMotion(*gpu, *renderer, -0.2f).x > 0.0f);
}

//======================================================================================================================
TEST_CASE("Scene removal preserves row identity and the image of remaining objects",
          "[gpu][scene-tables]") {
    auto gpu = createTestGpu();
    auto scene = tableScene();
    const auto removed = scene.objects[0].id;
    const auto retained = scene.objects[1].id;
    REQUIRE(scene.finalize(gpu->device()).has_value());
    auto renderer = tableRenderer(*gpu);
    auto before = submitTables(*gpu, scene, *renderer);
    scene.removeObject(removed);
    scene.tryObject(retained)->position.x = 0.0f;
    auto after = submitTables(*gpu, scene, *renderer);
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::None);
    REQUIRE(scene.objects[0].id == retained);
    REQUIRE_FALSE(scene.tryObject(removed));
    requireRed(tablePixels(*gpu, after), 0.0f);
    requireBlack(tablePixels(*gpu, after), -0.6f);
    auto authored = tableScene();
    const auto mesh = authored.objects[0].mesh;
    const auto material = authored.objects[0].material;
    authored.removeObject(authored.objects[0].id);
    authored.removeObject(authored.objects[0].id);
    authored.addObject({.position = {0, 0, -3}, .mesh = mesh, .material = material});
    REQUIRE(authored.finalize(gpu->device()).has_value());
    auto referenceRenderer = tableRenderer(*gpu);
    auto reference = submitTables(*gpu, authored, *referenceRenderer);
    REQUIRE(tablePixels(*gpu, after) == tablePixels(*gpu, reference));
}

//======================================================================================================================
TEST_CASE("A reused scene row seeds its own pose and renders zero first-frame motion",
          "[gpu][scene-tables]") {
    auto gpu = createTestGpu();
    auto scene = tableScene();
    REQUIRE(scene.finalize(gpu->device()).has_value());
    auto renderer = tableRenderer(*gpu);
    auto before = submitTables(*gpu, scene, *renderer);
    const auto old = scene.objects[0];
    scene.removeObject(old.id);
    const auto replacement =
        scene.addObject({.position = {0, 0, -3}, .mesh = old.mesh, .material = old.material});
    REQUIRE(replacement.slot == old.id.slot);
    REQUIRE(replacement.generation != old.id.generation);
    REQUIRE_FALSE(scene.tryObject(old.id));
    auto after = submitTables(*gpu, scene, *renderer);
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::None);
    requireRed(tablePixels(*gpu, after), 0.0f);
    const auto row = instanceRows(*gpu, scene)[replacement.slot];
    REQUIRE(row.model == row.previousModel);
    REQUIRE(row.model[3].x == 0.0f);
    REQUIRE(tableMotion(*gpu, *renderer, 0.0f) == glm::vec2(0));
}

//======================================================================================================================
TEST_CASE("Scene replacement rejects foreign identities while submitted scenes overlap",
          "[gpu][scene-tables]") {
    auto gpu = createTestGpu();
    auto first = std::make_unique<engine::Scene>(tableScene());
    auto second = tableScene();
    const auto foreign = first->objects[0].id;
    REQUIRE_FALSE(second.tryObject(foreign));
    REQUIRE(first->finalize(gpu->device()).has_value());
    REQUIRE(second.finalize(gpu->device()).has_value());
    second.objects[0].position.x = 0.0f;
    auto renderer = tableRenderer(*gpu);
    std::vector<engine::GpuBuffer> frames;
    frames.push_back(submitTables(*gpu, *first, *renderer, false));
    frames.push_back(submitTables(*gpu, second, *renderer, false));
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::SceneChanged);
    frames.push_back(submitTables(*gpu, *first, *renderer, false));
    frames.push_back(submitTables(*gpu, second, *renderer, false));
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::SceneChanged);
    gpu->waitIdle();
    requireRed(tablePixels(*gpu, frames[0]), -0.6f);
    requireRed(tablePixels(*gpu, frames[1]), 0.0f);
    requireRed(tablePixels(*gpu, frames[2]), -0.6f);
    requireRed(tablePixels(*gpu, frames[3]), 0.0f);
    first.reset();
    auto final = submitTables(*gpu, second, *renderer);
    requireRed(tablePixels(*gpu, final), 0.0f);
}

//======================================================================================================================
TEST_CASE("Scene table growth keeps stable rows and retires old buffers after three frames",
          "[gpu][scene-tables]") {
    auto gpu = createTestGpu();
    auto scene = tableScene();
    REQUIRE(scene.finalize(gpu->device()).has_value());
    auto renderer = tableRenderer(*gpu);
    auto initial = submitTables(*gpu, scene, *renderer, false);
    const auto old = scene.tables();
    const auto retained = scene.objects[0].id;
    const auto mesh = scene.objects[0].mesh;
    const auto material = scene.objects[0].material;
    const uint64_t lastFrame = gpu->frameNumber();
    const uint32_t capacity = scene.tableStats().instanceCapacity;
    while (scene.objects.size() <= capacity) {
        scene.addObject({.position = {0, 0, -3}, .mesh = mesh, .material = material});
    }
    std::vector<engine::GpuBuffer> frames;
    frames.push_back(submitTables(*gpu, scene, *renderer, false));
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::None);
    REQUIRE(scene.tableStats().growthEvents == 1);
    REQUIRE(scene.tableStats().instanceCapacity == capacity * 2);
    REQUIRE(scene.tableStats().pendingReleaseBuffers == 3);
    REQUIRE(scene.tables().instances != old.instances);
    REQUIRE(scene.tryObject(retained));
    frames.push_back(submitTables(*gpu, scene, *renderer, false));
    REQUIRE(gpu->frameNumber() == lastFrame + 2);
    REQUIRE(scene.tableStats().pendingReleaseBuffers == 3);
    frames.push_back(submitTables(*gpu, scene, *renderer, false));
    REQUIRE(gpu->frameNumber() == lastFrame + 3);
    REQUIRE(scene.tableStats().pendingReleaseBuffers == 0);
    gpu->waitIdle();
    requireRed(tablePixels(*gpu, initial), -0.6f);
    for (const auto& frame : frames) {
        requireRed(tablePixels(*gpu, frame), -0.6f);
        requireRed(tablePixels(*gpu, frame), 0.0f);
    }
    REQUIRE(instanceRows(*gpu, scene)[retained.slot].model[3].x == -0.6f);
    auto settled = submitTables(*gpu, scene, *renderer);
    REQUIRE(scene.tableStats().rowsWritten == 0);
    REQUIRE(scene.tableStats().bytesWritten == 0);
}

//======================================================================================================================
TEST_CASE("Scene table slots preserve each submitted transform across three-frame overlap",
          "[gpu][scene-tables]") {
    auto gpu = createTestGpu();
    auto scene = tableScene();
    scene.removeObject(scene.objects[1].id);
    REQUIRE(scene.finalize(gpu->device()).has_value());
    auto renderer = tableRenderer(*gpu);
    const std::array<float, 6> positions = {-0.7f, 0.0f, 0.7f, -0.7f, 0.0f, 0.7f};
    std::array<const lmx::engine::GpuBuffer*, 3> slotBuffers{};
    std::array<float, 3> slotPositions{};
    std::array<float, 3> slotPrevious{};
    std::vector<engine::GpuBuffer> frames;
    for (size_t i = 0; i < positions.size(); ++i) {
        scene.objects[0].position.x = positions[i];
        const float previous = scene.objects[0].previousModel[3].x;
        frames.push_back(submitTables(*gpu, scene, *renderer, false));
        if (i > 0) {
            REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::None);
        }
        const uint32_t slot = scene.tableStats().slot;
        slotBuffers[slot] = scene.tables().instances;
        slotPositions[slot] = positions[i];
        slotPrevious[slot] = previous;
    }
    gpu->waitIdle();
    for (size_t i = 0; i < positions.size(); ++i) {
        const auto pixels = tablePixels(*gpu, frames[i]);
        requireRed(pixels, positions[i]);
        for (float other : {-0.7f, 0.0f, 0.7f}) {
            if (other != positions[i]) {
                requireBlack(pixels, other);
            }
        }
    }
    for (uint32_t slot = 0; slot < 3; ++slot) {
        engine::InstanceRow row{};
        REQUIRE(slotBuffers[slot]);
        {
            const auto bytes = lmx::render::readbackNow(
                *gpu, *slotBuffers[slot], lmx::render::BufferUse::ShaderRead, {0, sizeof(row)});
            std::memcpy(&row, bytes.data(), sizeof(row));
        }
        REQUIRE(row.model[3].x == slotPositions[slot]);
        REQUIRE(row.previousModel[3].x == slotPrevious[slot]);
    }
}
