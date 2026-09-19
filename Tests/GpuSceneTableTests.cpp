#include "GpuTestSupport.h"
#include "Scene/Scene.h"

#include <catch2/catch_approx.hpp>

#include <glm/gtc/packing.hpp>

#include <cstring>

namespace {
namespace scene = lmx::scene;
namespace render = lmx::render;

//======================================================================================================================
render::MeshData tableQuad() {
    return {.vertices = {{-0.2f, -0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                         {0.2f, -0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                         {0.2f, 0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                         {-0.2f, 0.2f, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
            .indices = {0, 1, 2, 0, 2, 3}};
}

//======================================================================================================================
scene::Scene tableScene() {
    scene::Scene result;
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
std::unique_ptr<render::Renderer> tableRenderer(rojoRHI::Device& device) {
    auto renderer = render::Renderer::create(device, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());
    for (uint32_t i = 0; i < 3; ++i) {
        (*renderer)->clearColor[i] = 0;
    }
    return std::move(*renderer);
}

//======================================================================================================================
std::unique_ptr<rojoRHI::Buffer> submitTables(rojoRHI::Device& device, scene::Scene& scene,
                                              render::Renderer& renderer, bool wait = true) {
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
std::vector<uint8_t> tablePixels(rojoRHI::Buffer& buffer) {
    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    buffer.readback(pixels.data(), pixels.size());
    return pixels;
}

//======================================================================================================================
uint32_t tablePixelX(float x) {
    const render::Camera camera;
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
std::vector<render::InstanceRow> instanceRows(scene::Scene& scene) {
    const auto tables = scene.tables();
    std::vector<render::InstanceRow> rows(tables.instanceCount);
    tables.instances->readback(rows.data(), rows.size() * sizeof(render::InstanceRow));
    return rows;
}

//======================================================================================================================
glm::vec2 tableMotion(render::Renderer& renderer, float x) {
    std::vector<uint32_t> motion(size_t{kSize} * kSize);
    renderer.motionTarget()->readback(motion.data(), motion.size() * sizeof(uint32_t));
    return glm::unpackHalf2x16(motion[size_t{kSize / 2} * kSize + tablePixelX(x)]);
}

} // namespace

//======================================================================================================================
TEST_CASE("Scene tables share geometry and material without sharing motion",
          "[gpu][scene-tables]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = tableScene();
    REQUIRE(scene.finalize(**device).has_value());
    auto renderer = tableRenderer(**device);
    const auto first = scene.objects[0].id;
    const auto second = scene.objects[1].id;
    REQUIRE(first != second);
    REQUIRE(scene.objects[0].mesh == scene.objects[1].mesh);
    REQUIRE(scene.objects[0].material == scene.objects[1].material);
    auto frame = submitTables(**device, scene, *renderer);
    requireRed(tablePixels(*frame), -0.6f);
    requireRed(tablePixels(*frame), 0.6f);
    scene.tryObject(first)->position.x = -0.2f;
    frame = submitTables(**device, scene, *renderer);
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::None);
    requireBlack(tablePixels(*frame), -0.6f);
    requireRed(tablePixels(*frame), -0.2f);
    requireRed(tablePixels(*frame), 0.6f);
    const auto rows = instanceRows(scene);
    REQUIRE(rows[first.slot].previousModel[3].x == -0.6f);
    REQUIRE(rows[first.slot].model[3].x == -0.2f);
    REQUIRE(rows[second.slot].previousModel == rows[second.slot].model);
    REQUIRE(tableMotion(*renderer, 0.6f) == glm::vec2(0));
    REQUIRE(tableMotion(*renderer, -0.2f).x > 0.0f);
}

//======================================================================================================================
TEST_CASE("Scene removal preserves row identity and the image of remaining objects",
          "[gpu][scene-tables]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = tableScene();
    const auto removed = scene.objects[0].id;
    const auto retained = scene.objects[1].id;
    REQUIRE(scene.finalize(**device).has_value());
    auto renderer = tableRenderer(**device);
    auto before = submitTables(**device, scene, *renderer);
    scene.removeObject(removed);
    scene.tryObject(retained)->position.x = 0.0f;
    auto after = submitTables(**device, scene, *renderer);
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::None);
    REQUIRE(scene.objects[0].id == retained);
    REQUIRE_FALSE(scene.tryObject(removed));
    requireRed(tablePixels(*after), 0.0f);
    requireBlack(tablePixels(*after), -0.6f);
    auto authored = tableScene();
    const auto mesh = authored.objects[0].mesh;
    const auto material = authored.objects[0].material;
    authored.removeObject(authored.objects[0].id);
    authored.removeObject(authored.objects[0].id);
    authored.addObject({.position = {0, 0, -3}, .mesh = mesh, .material = material});
    REQUIRE(authored.finalize(**device).has_value());
    auto referenceRenderer = tableRenderer(**device);
    auto reference = submitTables(**device, authored, *referenceRenderer);
    REQUIRE(tablePixels(*after) == tablePixels(*reference));
}

//======================================================================================================================
TEST_CASE("A reused scene row seeds its own pose and renders zero first-frame motion",
          "[gpu][scene-tables]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = tableScene();
    REQUIRE(scene.finalize(**device).has_value());
    auto renderer = tableRenderer(**device);
    auto before = submitTables(**device, scene, *renderer);
    const auto old = scene.objects[0];
    scene.removeObject(old.id);
    const auto replacement =
        scene.addObject({.position = {0, 0, -3}, .mesh = old.mesh, .material = old.material});
    REQUIRE(replacement.slot == old.id.slot);
    REQUIRE(replacement.generation != old.id.generation);
    REQUIRE_FALSE(scene.tryObject(old.id));
    auto after = submitTables(**device, scene, *renderer);
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::None);
    requireRed(tablePixels(*after), 0.0f);
    const auto row = instanceRows(scene)[replacement.slot];
    REQUIRE(row.model == row.previousModel);
    REQUIRE(row.model[3].x == 0.0f);
    REQUIRE(tableMotion(*renderer, 0.0f) == glm::vec2(0));
}

//======================================================================================================================
TEST_CASE("Scene replacement rejects foreign identities while submitted scenes overlap",
          "[gpu][scene-tables]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto first = std::make_unique<scene::Scene>(tableScene());
    auto second = tableScene();
    const auto foreign = first->objects[0].id;
    REQUIRE_FALSE(second.tryObject(foreign));
    REQUIRE(first->finalize(**device).has_value());
    REQUIRE(second.finalize(**device).has_value());
    second.objects[0].position.x = 0.0f;
    auto renderer = tableRenderer(**device);
    std::vector<std::unique_ptr<rojoRHI::Buffer>> frames;
    frames.push_back(submitTables(**device, *first, *renderer, false));
    frames.push_back(submitTables(**device, second, *renderer, false));
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::SceneChanged);
    frames.push_back(submitTables(**device, *first, *renderer, false));
    frames.push_back(submitTables(**device, second, *renderer, false));
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::SceneChanged);
    (*device)->waitIdle();
    requireRed(tablePixels(*frames[0]), -0.6f);
    requireRed(tablePixels(*frames[1]), 0.0f);
    requireRed(tablePixels(*frames[2]), -0.6f);
    requireRed(tablePixels(*frames[3]), 0.0f);
    first.reset();
    auto final = submitTables(**device, second, *renderer);
    requireRed(tablePixels(*final), 0.0f);
}

//======================================================================================================================
TEST_CASE("Scene texture fallbacks preserve factors and stale texture handles fail lookup",
          "[gpu][scene-tables]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = tableScene();
    const std::array<uint8_t, 4> white = {255, 255, 255, 255};
    const rojoRHI::TextureMip mip{.data = white.data(), .bytesPerRow = 4};
    auto texture = (*device)->createTexture({.width = 1,
                                             .height = 1,
                                             .format = rojoRHI::Format::RGBA8Unorm,
                                             .sampled = true,
                                             .label = "lmx.test.sceneTables.white"},
                                            std::span(&mip, 1));
    REQUIRE(texture.has_value());
    const auto stale = scene.addTexture(std::move(*texture));
    REQUIRE(scene.tryTexture(stale));
    scene.removeTexture(stale);
    REQUIRE_FALSE(scene.tryTexture(stale));
    auto replacement = (*device)->createTexture({.width = 1,
                                                 .height = 1,
                                                 .format = rojoRHI::Format::RGBA8Unorm,
                                                 .sampled = true,
                                                 .label = "lmx.test.sceneTables.white2"},
                                                std::span(&mip, 1));
    REQUIRE(replacement.has_value());
    const auto whiteId = scene.addTexture(std::move(*replacement));
    REQUIRE(whiteId.slot == stale.slot);
    REQUIRE(whiteId.generation != stale.generation);
    REQUIRE_FALSE(scene.tryTexture(stale));
    const auto materialId = scene.objects[0].material;
    REQUIRE(scene.finalize(**device).has_value());
    auto renderer = tableRenderer(**device);
    auto fallback = submitTables(**device, scene, *renderer);
    auto& material = scene.material(materialId);
    material.diffuse = whiteId;
    material.metallicRoughness = whiteId;
    material.occlusion = whiteId;
    material.emissiveMap = whiteId;
    auto explicitWhite = submitTables(**device, scene, *renderer);
    REQUIRE(tablePixels(*fallback) == tablePixels(*explicitWhite));
    requireRed(tablePixels(*fallback), -0.6f);
    render::MaterialRow row{};
    scene.tables().materials->readback(&row, sizeof(row));
    REQUIRE((row.flags & render::kMaterialHasNormalMap) == 0);
}

//======================================================================================================================
TEST_CASE("Scene table growth keeps stable rows and retires old buffers after three frames",
          "[gpu][scene-tables]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = tableScene();
    REQUIRE(scene.finalize(**device).has_value());
    auto renderer = tableRenderer(**device);
    auto initial = submitTables(**device, scene, *renderer, false);
    const auto old = scene.tables();
    const auto retained = scene.objects[0].id;
    const auto mesh = scene.objects[0].mesh;
    const auto material = scene.objects[0].material;
    const uint64_t lastFrame = (*device)->frameNumber();
    const uint32_t capacity = scene.tableStats().instanceCapacity;
    while (scene.objects.size() <= capacity) {
        scene.addObject({.position = {0, 0, -3}, .mesh = mesh, .material = material});
    }
    std::vector<std::unique_ptr<rojoRHI::Buffer>> frames;
    frames.push_back(submitTables(**device, scene, *renderer, false));
    REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::None);
    REQUIRE(scene.tableStats().growthEvents == 1);
    REQUIRE(scene.tableStats().instanceCapacity == capacity * 2);
    REQUIRE(scene.tableStats().pendingReleaseBuffers == 3);
    REQUIRE(scene.tables().instances != old.instances);
    REQUIRE(scene.tryObject(retained));
    frames.push_back(submitTables(**device, scene, *renderer, false));
    REQUIRE((*device)->frameNumber() == lastFrame + 2);
    REQUIRE(scene.tableStats().pendingReleaseBuffers == 3);
    frames.push_back(submitTables(**device, scene, *renderer, false));
    REQUIRE((*device)->frameNumber() == lastFrame + 3);
    REQUIRE(scene.tableStats().pendingReleaseBuffers == 0);
    (*device)->waitIdle();
    requireRed(tablePixels(*initial), -0.6f);
    for (const auto& frame : frames) {
        requireRed(tablePixels(*frame), -0.6f);
        requireRed(tablePixels(*frame), 0.0f);
    }
    REQUIRE(instanceRows(scene)[retained.slot].model[3].x == -0.6f);
    auto settled = submitTables(**device, scene, *renderer);
    REQUIRE(scene.tableStats().rowsWritten == 0);
    REQUIRE(scene.tableStats().bytesWritten == 0);
}

//======================================================================================================================
TEST_CASE("Scene table slots preserve each submitted transform across three-frame overlap",
          "[gpu][scene-tables]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = tableScene();
    scene.removeObject(scene.objects[1].id);
    REQUIRE(scene.finalize(**device).has_value());
    auto renderer = tableRenderer(**device);
    const std::array<float, 6> positions = {-0.7f, 0.0f, 0.7f, -0.7f, 0.0f, 0.7f};
    std::array<rojoRHI::Buffer*, 3> slotBuffers{};
    std::array<float, 3> slotPositions{};
    std::array<float, 3> slotPrevious{};
    std::vector<std::unique_ptr<rojoRHI::Buffer>> frames;
    for (size_t i = 0; i < positions.size(); ++i) {
        scene.objects[0].position.x = positions[i];
        const float previous = scene.objects[0].previousModel[3].x;
        frames.push_back(submitTables(**device, scene, *renderer, false));
        if (i > 0) {
            REQUIRE(renderer->temporalStatus().lastReset == render::HistoryResetReason::None);
        }
        const uint32_t slot = scene.tableStats().slot;
        slotBuffers[slot] = scene.tables().instances;
        slotPositions[slot] = positions[i];
        slotPrevious[slot] = previous;
    }
    (*device)->waitIdle();
    for (size_t i = 0; i < positions.size(); ++i) {
        const auto pixels = tablePixels(*frames[i]);
        requireRed(pixels, positions[i]);
        for (float other : {-0.7f, 0.0f, 0.7f}) {
            if (other != positions[i]) {
                requireBlack(pixels, other);
            }
        }
    }
    for (uint32_t slot = 0; slot < 3; ++slot) {
        render::InstanceRow row{};
        REQUIRE(slotBuffers[slot]);
        slotBuffers[slot]->readback(&row, sizeof(row));
        REQUIRE(row.model[3].x == slotPositions[slot]);
        REQUIRE(row.previousModel[3].x == slotPrevious[slot]);
    }
}

//======================================================================================================================
TEST_CASE("Local light table slots preserve each submitted row across three-frame overlap",
          "[gpu][scene-tables]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = tableScene();
    const auto light = scene.addLight({.type = render::LocalLightType::Point,
                                       .position = {0.0f, 0.0f, 0.0f},
                                       .colour = {1.0f, 1.0f, 1.0f},
                                       .intensity = 1.0f,
                                       .range = 5.0f});
    REQUIRE(light.has_value());
    REQUIRE(scene.finalize(**device).has_value());
    auto renderer = tableRenderer(**device);
    // Six distinct nonzero values: a repeated or zero-valued position would be indistinguishable
    // from an unwritten row's default-constructed LightRow{} (position (0,0,0), range 0).
    const std::array<float, 6> positions = {-2.1f, -1.4f, -0.7f, 0.7f, 1.4f, 2.1f};
    std::array<rojoRHI::Buffer*, 3> slotBuffers{};
    std::array<float, 3> slotPositions{};
    std::vector<std::unique_ptr<rojoRHI::Buffer>> frames;
    for (size_t i = 0; i < positions.size(); ++i) {
        REQUIRE(scene
                    .updateLight(*light, {.type = render::LocalLightType::Point,
                                          .position = {positions[i], 0.0f, 0.0f},
                                          .colour = {1.0f, 1.0f, 1.0f},
                                          .intensity = 1.0f,
                                          .range = 5.0f})
                    .has_value());
        frames.push_back(submitTables(**device, scene, *renderer, false));
        const uint32_t slot = scene.tableStats().slot;
        slotBuffers[slot] = scene.tables().lights;
        slotPositions[slot] = positions[i];
    }
    (*device)->waitIdle();
    for (uint32_t slot = 0; slot < 3; ++slot) {
        render::LightRow row{};
        REQUIRE(slotBuffers[slot]);
        slotBuffers[slot]->readback(&row, sizeof(row));
        REQUIRE(row.position.x == slotPositions[slot]);
        REQUIRE(row.range == 5.0f);
    }
}

//======================================================================================================================
TEST_CASE(
    "Removing a no-longer-authored texture retains submitted bindings through paced retirement",
    "[gpu][scene-tables]") {
    auto device = rojoRHI::createDevice();
    REQUIRE(device.has_value());
    auto scene = tableScene();
    const std::array<uint8_t, 4> black = {0, 0, 0, 255};
    const rojoRHI::TextureMip mip{.data = black.data(), .bytesPerRow = 4};
    auto texture = (*device)->createTexture({.width = 1,
                                             .height = 1,
                                             .format = rojoRHI::Format::RGBA8Unorm,
                                             .sampled = true,
                                             .label = "lmx.test.sceneTables.retiringTexture"},
                                            std::span(&mip, 1));
    REQUIRE(texture.has_value());
    const auto id = scene.addTexture(std::move(*texture));
    const auto material = scene.objects[0].material;
    scene.material(material).emissiveMap = id;
    REQUIRE(scene.finalize(**device).has_value());
    auto renderer = tableRenderer(**device);
    auto textured = submitTables(**device, scene, *renderer, false);
    scene.material(material).emissiveMap.reset();
    scene.removeTexture(id);
    REQUIRE_FALSE(scene.tryTexture(id));
    std::vector<std::unique_ptr<rojoRHI::Buffer>> fallback;
    for (uint32_t frame = 0; frame < 3; ++frame) {
        fallback.push_back(submitTables(**device, scene, *renderer, false));
    }
    (*device)->waitIdle();
    requireBlack(tablePixels(*textured), -0.6f);
    for (const auto& frame : fallback) {
        requireRed(tablePixels(*frame), -0.6f);
    }
}
