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
    const TextureMip mip{.data = texels.data(), .bytesPerRow = 16};
    auto texture = lmx::engine::GpuTexture::create(device,
                                                   {.width = 4,
                                                    .height = 1,
                                                    .format = Format::RGBA8Unorm_sRGB,
                                                    .sampled = true,
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
            lmx::render::readbackNow(gpu, renderer.hdrColorTexture(),
                                     view.temporal.enabled ? lmx::render::TextureUse::CopySource
                                                           : lmx::render::TextureUse::ShaderRead);
        std::memcpy(pixels.data(), readbackBytes.data(), pixels.size() * sizeof(uint16_t));
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
TEST_CASE("alpha mask shares color depth and motion coverage", "[gpu][alpha-mask]") {
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
    SECTION("manual exposure temporal") {
        view.temporal.enabled = true;
    }
    SECTION("auto exposure temporal") {
        view.temporal.enabled = true;
        view.autoExposureEnabled = true;
    }
    SECTION("temporal disabled") {
        view.temporal.enabled = false;
    }
    SECTION("double sided back faces") {
        items[0].model = glm::rotate(glm::mat4{1}, glm::pi<float>(), glm::vec3{0, 1, 0});
        items[0].material.uvTransform[0][0] = -1;
        items[0].material.uvTransform[3][0] = 1;
        items[0].material.doubleSided = true;
        view.temporal.enabled = true;
    }
    auto pixels = cutoutFrame(*gpu, **renderer, view);
    REQUIRE(cutoutChannel(pixels, 25, 0) < 0.01f);
    REQUIRE(cutoutChannel(pixels, 25, 1) > 1.0f);
    REQUIRE(cutoutChannel(pixels, 39, 0) > 1.0f);
    REQUIRE(cutoutChannel(pixels, 39, 1) < 0.01f);
    REQUIRE(cutoutChannel(pixels, 39, 3) == 1.0f);
    if (view.temporal.enabled) {
        std::vector<uint16_t> history(kSize * kSize * 4);
        {
            const auto readbackBytes = lmx::render::readbackNow(
                *gpu, *(*renderer)->historyTexture(), lmx::render::TextureUse::CopyDestination);
            std::memcpy(history.data(), readbackBytes.data(), history.size() * sizeof(uint16_t));
        }
        REQUIRE(cutoutChannel(history, 39, 3) == 1.0f);
        std::vector<uint16_t> motion(kSize * kSize * 2);
        {
            const auto readbackBytes = lmx::render::readbackNow(
                *gpu, *(*renderer)->motionTexture(), lmx::render::TextureUse::RenderTarget);
            std::memcpy(motion.data(), readbackBytes.data(), motion.size() * 2);
        }
        const size_t hole = (kSize / 2 * kSize + 25) * 2;
        const size_t solid = (kSize / 2 * kSize + 39) * 2;
        REQUIRE(glm::unpackHalf1x16(motion[hole]) == 0.0f);
        REQUIRE(std::isinf(glm::unpackHalf1x16(motion[solid])));
        items[0].motionClass = lmx::engine::MotionClass::Rigid;
        items[0].previousModel =
            glm::translate(glm::mat4{1}, glm::vec3{-0.1f, 0, 0}) * items[0].model;
        cutoutFrame(*gpu, **renderer, view);
        {
            const auto readbackBytes = lmx::render::readbackNow(
                *gpu, *(*renderer)->motionTexture(), lmx::render::TextureUse::RenderTarget);
            std::memcpy(motion.data(), readbackBytes.data(), motion.size() * 2);
        }
        REQUIRE(glm::unpackHalf1x16(motion[hole]) == 0.0f);
        REQUIRE(glm::unpackHalf1x16(motion[solid]) > 0.005f);
    }
    items[0].material.alphaMode = lmx::engine::AlphaMode::Opaque;
    items[0].model = glm::mat4{1};
    pixels = cutoutFrame(*gpu, **renderer, view);
    REQUIRE(cutoutChannel(pixels, 25, 0) > 1.0f);
    REQUIRE(cutoutChannel(pixels, 25, 1) < 0.01f);
}
