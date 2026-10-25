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
render::ReadbackTicket submitOffTables(engine::GpuContext& gpu, engine::Scene& scene,
                                       render::Renderer& renderer, bool wait = true) {
    auto gpuFrame = gpu.beginFrame();
    auto& commands = gpuFrame.commands;
    auto prepared = scene.prepareFrame(gpuFrame.number);
    INFO(errorOf(prepared));
    REQUIRE(prepared.has_value());
    std::vector<engine::DrawItem> items;
    auto view = render::buildSceneView(scene, items, false);
    view.bloomEnabled = false;
    view.temporal.enabled = false;
    view.localLightMode = engine::LocalLightMode::Off;
    renderer.render(commands, engine::Camera{}, view);
    auto snapshot =
        render::recordReadback(gpu, commands, renderer.colorTarget(), renderer.textureStates());
    gpu.submitAndEndFrame(gpuFrame);
    scene.commitFrame();
    if (wait)
        gpu.waitIdle();
    return snapshot;
}

//======================================================================================================================
std::vector<uint8_t> tablePixels(engine::GpuContext& gpu, const render::ReadbackTicket& ticket) {
    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    const auto resolved = render::resolve(gpu, ticket, pixels);
    INFO(errorOf(resolved));
    REQUIRE(resolved.has_value());
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

} // namespace

//======================================================================================================================
TEST_CASE("Scene texture fallbacks preserve factors and stale texture handles fail lookup",
          "[gpu][scene-tables]") {
    auto gpu = createTestGpu();
    auto scene = tableScene();
    const std::array<uint8_t, 4> white = {255, 255, 255, 255};
    const rojoRHI::TextureData mip{.bytes = std::as_bytes(std::span(white)), .bytesPerRow = 4};
    auto texture = lmx::engine::GpuTexture::create(gpu->device(),
                                                   {.format = rojoRHI::Format::RGBA8Unorm,
                                                    .extent = {1, 1, 1},
                                                    .usage = rojoRHI::TextureUsage::Sampled,
                                                    .label = "lmx.test.sceneTables.white"},
                                                   std::span(&mip, 1));
    REQUIRE(texture.has_value());
    const auto stale = scene.addTexture(std::move(*texture));
    REQUIRE(scene.tryTexture(stale));
    scene.removeTexture(stale);
    REQUIRE_FALSE(scene.tryTexture(stale));
    auto replacement = lmx::engine::GpuTexture::create(gpu->device(),
                                                       {.format = rojoRHI::Format::RGBA8Unorm,
                                                        .extent = {1, 1, 1},
                                                        .usage = rojoRHI::TextureUsage::Sampled,
                                                        .label = "lmx.test.sceneTables.white2"},
                                                       std::span(&mip, 1));
    REQUIRE(replacement.has_value());
    const auto whiteId = scene.addTexture(std::move(*replacement));
    REQUIRE(whiteId.slot == stale.slot);
    REQUIRE(whiteId.generation != stale.generation);
    REQUIRE_FALSE(scene.tryTexture(stale));
    const auto materialId = scene.objects[0].material;
    REQUIRE(scene.finalize(gpu->device()).has_value());
    auto renderer = tableRenderer(*gpu);
    auto fallback = submitOffTables(*gpu, scene, *renderer);
    auto& material = scene.material(materialId);
    material.diffuse = whiteId;
    material.metallicRoughness = whiteId;
    material.occlusion = whiteId;
    material.emissiveMap = whiteId;
    auto explicitWhite = submitOffTables(*gpu, scene, *renderer);
    REQUIRE(tablePixels(*gpu, fallback) == tablePixels(*gpu, explicitWhite));
    requireRed(tablePixels(*gpu, fallback), -0.6f);
    engine::MaterialRow row{};
    {
        const auto bytes =
            lmx::render::readbackNow(*gpu, *scene.tables().materials,
                                     render::SyncPoint{rojoRHI::Stage::None, rojoRHI::Access::None,
                                                       rojoRHI::Layout::Undefined},
                                     {0, sizeof(row)});
        INFO(errorOf(bytes));
        REQUIRE(bytes.has_value());
        std::memcpy(&row, bytes->data(), sizeof(row));
    }
    REQUIRE((row.flags & engine::kMaterialHasNormalMap) == 0);
}

//======================================================================================================================
TEST_CASE("Local light table slots preserve each submitted row across three-frame overlap",
          "[gpu][scene-tables]") {
    auto gpu = createTestGpu();
    auto scene = tableScene();
    const auto light = scene.addLight({.type = engine::LocalLightType::Point,
                                       .position = {0.0f, 0.0f, 0.0f},
                                       .colour = {1.0f, 1.0f, 1.0f},
                                       .intensity = 1.0f,
                                       .range = 5.0f});
    REQUIRE(light.has_value());
    REQUIRE(scene.finalize(gpu->device()).has_value());
    auto renderer = tableRenderer(*gpu);
    // Six distinct nonzero values: a repeated or zero-valued position would be indistinguishable
    // from an unwritten row's default-constructed LightRow{} (position (0,0,0), range 0).
    const std::array<float, 6> positions = {-2.1f, -1.4f, -0.7f, 0.7f, 1.4f, 2.1f};
    std::array<const lmx::engine::GpuBuffer*, 3> slotBuffers{};
    std::array<float, 3> slotPositions{};
    std::vector<render::ReadbackTicket> frames;
    for (size_t i = 0; i < positions.size(); ++i) {
        REQUIRE(scene
                    .updateLight(*light, {.type = engine::LocalLightType::Point,
                                          .position = {positions[i], 0.0f, 0.0f},
                                          .colour = {1.0f, 1.0f, 1.0f},
                                          .intensity = 1.0f,
                                          .range = 5.0f})
                    .has_value());
        frames.push_back(submitOffTables(*gpu, scene, *renderer, false));
        const uint32_t slot = scene.tableStats().slot;
        slotBuffers[slot] = scene.tables().lights;
        slotPositions[slot] = positions[i];
    }
    gpu->waitIdle();
    for (uint32_t slot = 0; slot < 3; ++slot) {
        engine::LightRow row{};
        REQUIRE(slotBuffers[slot]);
        {
            const auto bytes = lmx::render::readbackNow(
                *gpu, *slotBuffers[slot],
                render::SyncPoint{rojoRHI::Stage::None, rojoRHI::Access::None,
                                  rojoRHI::Layout::Undefined},
                {0, sizeof(row)});
            INFO(errorOf(bytes));
            REQUIRE(bytes.has_value());
            std::memcpy(&row, bytes->data(), sizeof(row));
        }
        REQUIRE(row.position.x == slotPositions[slot]);
        REQUIRE(row.range == 5.0f);
    }
}

//======================================================================================================================
TEST_CASE(
    "Removing a no-longer-authored texture retains submitted bindings through paced retirement",
    "[gpu][scene-tables]") {
    auto gpu = createTestGpu();
    auto scene = tableScene();
    const std::array<uint8_t, 4> black = {0, 0, 0, 255};
    const rojoRHI::TextureData mip{.bytes = std::as_bytes(std::span(black)), .bytesPerRow = 4};
    auto texture =
        lmx::engine::GpuTexture::create(gpu->device(),
                                        {.format = rojoRHI::Format::RGBA8Unorm,
                                         .extent = {1, 1, 1},
                                         .usage = rojoRHI::TextureUsage::Sampled,
                                         .label = "lmx.test.sceneTables.retiringTexture"},
                                        std::span(&mip, 1));
    REQUIRE(texture.has_value());
    const auto id = scene.addTexture(std::move(*texture));
    const auto material = scene.objects[0].material;
    scene.material(material).emissiveMap = id;
    REQUIRE(scene.finalize(gpu->device()).has_value());
    auto renderer = tableRenderer(*gpu);
    auto textured = submitOffTables(*gpu, scene, *renderer, false);
    scene.material(material).emissiveMap.reset();
    scene.removeTexture(id);
    REQUIRE_FALSE(scene.tryTexture(id));
    std::vector<render::ReadbackTicket> fallback;
    for (uint32_t frame = 0; frame < 3; ++frame) {
        fallback.push_back(submitOffTables(*gpu, scene, *renderer, false));
    }
    gpu->waitIdle();
    requireBlack(tablePixels(*gpu, textured), -0.6f);
    for (const auto& frame : fallback) {
        requireRed(tablePixels(*gpu, frame), -0.6f);
    }
}
