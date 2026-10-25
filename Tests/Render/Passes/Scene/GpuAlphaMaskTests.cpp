#include "Support/GpuTestSupport.h"

#include <cstring>
#include <glm/gtc/packing.hpp>

#include "Support/SceneTableTestSupport.h"
#include <array>
#include <cmath>

using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

namespace {
using namespace lmx::render;
using namespace rojoRHI;

//======================================================================================================================
lmx::engine::MeshData cutoutQuad() {
    return {.vertices = {{-1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                         {1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                         {1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                         {-1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
            .indices = {0, 1, 2, 0, 2, 3}};
}

//======================================================================================================================
lmx::engine::GpuTexture cutoutTexture(Device& device) {
    const std::array<uint8_t, 16> texels{255, 255, 255, 128, 255, 255, 255, 128,
                                         255, 255, 255, 255, 255, 255, 255, 255};
    const TextureData mip{.bytes = std::as_bytes(std::span(texels)), .bytesPerRow = 16};
    auto texture = lmx::engine::GpuTexture::create(device,
                                                   {.format = Format::RGBA8Unorm_sRGB,
                                                    .extent = {4, 1, 1},
                                                    .usage = rojoRHI::TextureUsage::Sampled,
                                                    .label = "lmx.test.cutout.texture"},
                                                   std::span{&mip, 1});
    INFO(errorOf(texture));
    REQUIRE(texture);
    return std::move(*texture);
}

//======================================================================================================================
std::vector<uint16_t> cutoutFrame(lmx::engine::GpuContext& gpu, Renderer& renderer,
                                  FixtureSceneView view) {
    lmx::engine::Camera camera;
    camera.position = {0, 0, 5};
    auto gpuFrame1 = gpu.beginFrame();
    auto& commands = gpuFrame1.commands;
    renderer.render(commands, camera, lmx::test::prepareSceneView(view, gpu));
    gpu.submitAndEndFrame(gpuFrame1);
    gpu.waitIdle();
    std::vector<uint16_t> pixels(kSize * kSize * 4);
    {
        const auto readbackBytes =
            lmx::render::readbackNow(gpu, renderer.hdrColorTexture(), renderer.textureStates());
        INFO(errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(pixels.data(), readbackBytes->data(), pixels.size() * sizeof(uint16_t));
    }
    return pixels;
}

//======================================================================================================================
float cutoutChannel(const std::vector<uint16_t>& pixels, uint32_t x, uint32_t channel) {
    return glm::unpackHalf1x16(pixels[(kSize / 2 * kSize + x) * 4 + channel]);
}

//======================================================================================================================
FixtureSceneView cutoutView(std::span<const FixtureDrawItem> items) {
    FixtureSceneView view;
    view.items = items;
    view.boundingSphere = {0, 0, -1, 5};
    view.lights[0] = {.strength = {1, 1, 1}, .direction = {0, 0, -1}};
    view.lights[1].strength = {0, 0, 0};
    view.lights[2].strength = {0, 0, 0};
    view.bloomEnabled = false;
    view.temporal.jitterEnabled = false;
    view.temporal.reconstruction = ReconstructionMode::Raw;
    return view;
}
} // namespace

//======================================================================================================================
TEST_CASE("alpha mask shadows preserve holes and transformed UV coverage", "[gpu][alpha-mask]") {
    using namespace lmx::render;
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer);
    auto mesh = fixtureMesh(gpu->device(), cutoutQuad(), "lmx.test.cutout.shadowQuad");
    REQUIRE(mesh);
    auto texture = cutoutTexture(gpu->device());
    std::array<FixtureDrawItem, 2> items;
    items[0].mesh = &*mesh;
    items[0].material.diffuse = &texture;
    items[0].material.alphaMode = lmx::engine::AlphaMode::Mask;
    items[0].material.alphaCutoff = 0.3f;
    items[0].material.albedo.a = 0.5f;
    items[0].material.doubleSided = true;
    items[1].mesh = &*mesh;
    items[1].model = glm::translate(glm::mat4{1}, glm::vec3{0, 0, -1}) *
                     glm::scale(glm::mat4{1}, glm::vec3{4, 4, 1});
    items[1].previousModel = items[1].model;
    auto view = cutoutView(items);
    view.temporal.enabled = false;
    view.lights[0].direction = glm::normalize(glm::vec3{-1, 0, -1});
    const auto masked = cutoutFrame(*gpu, **renderer, view);
    items[0].material.alphaMode = lmx::engine::AlphaMode::Opaque;
    const auto opaque = cutoutFrame(*gpu, **renderer, view);
    INFO("masked=" << cutoutChannel(masked, 18, 0) << " opaque=" << cutoutChannel(opaque, 18, 0));
    REQUIRE(cutoutChannel(masked, 18, 0) > cutoutChannel(opaque, 18, 0) + 0.05f);
    items[0].material.alphaMode = lmx::engine::AlphaMode::Mask;
    items[0].material.uvTransform[0][0] = -1;
    items[0].material.uvTransform[3][0] = 1;
    const auto flipped = cutoutFrame(*gpu, **renderer, view);
    REQUIRE(cutoutChannel(flipped, 18, 0) < cutoutChannel(masked, 18, 0) - 0.05f);
}

//======================================================================================================================
TEST_CASE("double sided alpha masks reverse back-face lighting normals", "[gpu][alpha-mask]") {
    using namespace lmx::render;
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer);
    auto mesh = fixtureMesh(gpu->device(), cutoutQuad(), "lmx.test.cutout.backFace");
    REQUIRE(mesh);
    FixtureDrawItem item;
    item.mesh = &*mesh;
    item.material.alphaMode = lmx::engine::AlphaMode::Mask;
    item.material.alphaCutoff = 0.5f;
    item.material.albedo.a = 0.5f;
    item.material.doubleSided = true;
    auto view = cutoutView(std::span{&item, 1});
    view.temporal.enabled = false;
    const auto front = cutoutFrame(*gpu, **renderer, view);
    item.model = glm::rotate(glm::mat4{1}, glm::pi<float>(), glm::vec3{0, 1, 0});
    const auto back = cutoutFrame(*gpu, **renderer, view);
    REQUIRE(cutoutChannel(front, 32, 0) > 0.15f);
    REQUIRE(std::abs(cutoutChannel(front, 32, 0) - cutoutChannel(back, 32, 0)) < 0.002f);
    item.material.doubleSided = false;
    const auto culled = cutoutFrame(*gpu, **renderer, view);
    REQUIRE(cutoutChannel(culled, 32, 0) < 0.1f);
}

//======================================================================================================================
TEST_CASE("alpha mask shares color and depth coverage with temporal Off", "[gpu][alpha-mask]") {
    using namespace lmx::render;
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    auto renderer = Renderer::create(*gpu, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer);
    auto mesh = fixtureMesh(gpu->device(), cutoutQuad(), "lmx.test.cutout.quad");
    REQUIRE(mesh);
    auto texture = cutoutTexture(gpu->device());
    std::array<FixtureDrawItem, 2> items;
    items[0].mesh = &*mesh;
    items[0].material.diffuse = &texture;
    items[0].material.alphaMode = lmx::engine::AlphaMode::Mask;
    items[0].material.alphaCutoff = 0.3f;
    items[0].material.albedo = {0, 0, 0, 0.5f};
    items[0].material.emissive = {2, 0, 0};
    items[0].motionClass = lmx::engine::MotionClass::Invalid;
    items[1].mesh = &*mesh;
    items[1].model = glm::translate(glm::mat4{1}, glm::vec3{0, 0, -1}) *
                     glm::scale(glm::mat4{1}, glm::vec3{4, 4, 1});
    items[1].previousModel = items[1].model;
    items[1].material.albedo = {0, 0, 0, 1};
    items[1].material.emissive = {0, 2, 0};
    auto view = cutoutView(items);
    view.lights[0].strength = {0, 0, 0};
    view.temporal.enabled = false;
    SECTION("temporal disabled") {}
    SECTION("double sided back faces with temporal Off") {
        items[0].model = glm::rotate(glm::mat4{1}, glm::pi<float>(), glm::vec3{0, 1, 0});
        items[0].material.uvTransform[0][0] = -1;
        items[0].material.uvTransform[3][0] = 1;
        items[0].material.doubleSided = true;
    }
    auto pixels = cutoutFrame(*gpu, **renderer, view);
    REQUIRE(cutoutChannel(pixels, 25, 0) < 0.01f);
    REQUIRE(cutoutChannel(pixels, 25, 1) > 1.0f);
    REQUIRE(cutoutChannel(pixels, 39, 0) > 1.0f);
    REQUIRE(cutoutChannel(pixels, 39, 1) < 0.01f);
    REQUIRE(cutoutChannel(pixels, 39, 3) == 1.0f);
    items[0].material.alphaMode = lmx::engine::AlphaMode::Opaque;
    items[0].model = glm::mat4{1};
    pixels = cutoutFrame(*gpu, **renderer, view);
    REQUIRE(cutoutChannel(pixels, 25, 0) > 1.0f);
    REQUIRE(cutoutChannel(pixels, 25, 1) < 0.01f);
}
