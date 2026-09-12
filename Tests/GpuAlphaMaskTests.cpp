#include "GpuTestSupport.h"

#include <glm/gtc/packing.hpp>

#include <array>
#include <cmath>

namespace {
using namespace lmx::render;
using namespace lmx::rhi;

//======================================================================================================================
MeshData cutoutQuad() {
    return {.vertices = {{-1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                         {1, -1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1},
                         {1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0},
                         {-1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0}},
            .indices = {0, 1, 2, 0, 2, 3}};
}

//======================================================================================================================
std::unique_ptr<Texture> cutoutTexture(Device& device) {
    const std::array<uint8_t, 16> texels{255, 255, 255, 128, 255, 255, 255, 128,
                                         255, 255, 255, 255, 255, 255, 255, 255};
    const TextureMip mip{.data = texels.data(), .bytesPerRow = 16};
    auto texture = device.createTexture({.width = 4,
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
std::vector<uint16_t> cutoutFrame(Device& device, Renderer& renderer, SceneView view) {
    Camera camera;
    camera.position = {0, 0, 5};
    auto& commands = device.beginFrame();
    renderer.render(commands, camera, view, false);
    device.endFrame(nullptr);
    device.waitIdle();
    std::vector<uint16_t> pixels(kSize * kSize * 4);
    renderer.hdrColorTarget().readback(pixels.data(), pixels.size() * sizeof(uint16_t));
    return pixels;
}

//======================================================================================================================
float cutoutChannel(const std::vector<uint16_t>& pixels, uint32_t x, uint32_t channel) {
    return glm::unpackHalf1x16(pixels[(kSize / 2 * kSize + x) * 4 + channel]);
}

//======================================================================================================================
SceneView cutoutView(std::span<const DrawItem> items) {
    SceneView view;
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
    using namespace lmx::rhi;
    auto device = createDevice();
    REQUIRE(device);
    auto renderer = Renderer::create(**device, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer);
    auto mesh = createMesh(**device, cutoutQuad(), "lmx.test.cutout.quad");
    REQUIRE(mesh);
    auto texture = cutoutTexture(**device);
    std::array<DrawItem, 2> items;
    items[0].mesh = &*mesh;
    items[0].material.diffuse = texture.get();
    items[0].material.alphaMode = AlphaMode::Mask;
    items[0].material.alphaCutoff = 0.3f;
    items[0].material.albedo = {0, 0, 0, 0.5f};
    items[0].material.emissive = {2, 0, 0};
    items[0].motionClass = MotionClass::Invalid;
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
    auto pixels = cutoutFrame(**device, **renderer, view);
    REQUIRE(cutoutChannel(pixels, 25, 0) < 0.01f);
    REQUIRE(cutoutChannel(pixels, 25, 1) > 1.0f);
    REQUIRE(cutoutChannel(pixels, 39, 0) > 1.0f);
    REQUIRE(cutoutChannel(pixels, 39, 1) < 0.01f);
    REQUIRE(cutoutChannel(pixels, 39, 3) == 1.0f);
    if (view.temporal.enabled) {
        std::vector<uint16_t> history(kSize * kSize * 4);
        (*renderer)->historyTarget()->readback(history.data(), history.size() * sizeof(uint16_t));
        REQUIRE(cutoutChannel(history, 39, 3) == 1.0f);
        std::vector<uint16_t> motion(kSize * kSize * 2);
        (*renderer)->motionTarget()->readback(motion.data(), motion.size() * 2);
        const size_t hole = (kSize / 2 * kSize + 25) * 2;
        const size_t solid = (kSize / 2 * kSize + 39) * 2;
        REQUIRE(glm::unpackHalf1x16(motion[hole]) == 0.0f);
        REQUIRE(std::isinf(glm::unpackHalf1x16(motion[solid])));
        items[0].motionClass = MotionClass::Rigid;
        items[0].previousModel =
            glm::translate(glm::mat4{1}, glm::vec3{-0.1f, 0, 0}) * items[0].model;
        cutoutFrame(**device, **renderer, view);
        (*renderer)->motionTarget()->readback(motion.data(), motion.size() * 2);
        REQUIRE(glm::unpackHalf1x16(motion[hole]) == 0.0f);
        REQUIRE(glm::unpackHalf1x16(motion[solid]) > 0.005f);
    }
    items[0].material.alphaMode = AlphaMode::Opaque;
    items[0].model = glm::mat4{1};
    pixels = cutoutFrame(**device, **renderer, view);
    REQUIRE(cutoutChannel(pixels, 25, 0) > 1.0f);
    REQUIRE(cutoutChannel(pixels, 25, 1) < 0.01f);
}

//======================================================================================================================
TEST_CASE("alpha mask shadows preserve holes and transformed UV coverage", "[gpu][alpha-mask]") {
    using namespace lmx::render;
    using namespace lmx::rhi;
    auto device = createDevice();
    REQUIRE(device);
    auto renderer = Renderer::create(**device, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer);
    auto mesh = createMesh(**device, cutoutQuad(), "lmx.test.cutout.shadowQuad");
    REQUIRE(mesh);
    auto texture = cutoutTexture(**device);
    std::array<DrawItem, 2> items;
    items[0].mesh = &*mesh;
    items[0].material.diffuse = texture.get();
    items[0].material.alphaMode = AlphaMode::Mask;
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
    const auto masked = cutoutFrame(**device, **renderer, view);
    items[0].material.alphaMode = AlphaMode::Opaque;
    const auto opaque = cutoutFrame(**device, **renderer, view);
    INFO("masked=" << cutoutChannel(masked, 18, 0) << " opaque=" << cutoutChannel(opaque, 18, 0));
    REQUIRE(cutoutChannel(masked, 18, 0) > cutoutChannel(opaque, 18, 0) + 0.05f);
    items[0].material.alphaMode = AlphaMode::Mask;
    items[0].material.uvTransform[0][0] = -1;
    items[0].material.uvTransform[3][0] = 1;
    const auto flipped = cutoutFrame(**device, **renderer, view);
    REQUIRE(cutoutChannel(flipped, 18, 0) < cutoutChannel(masked, 18, 0) - 0.05f);
}

//======================================================================================================================
TEST_CASE("double sided alpha masks reverse back-face lighting normals", "[gpu][alpha-mask]") {
    using namespace lmx::render;
    using namespace lmx::rhi;
    auto device = createDevice();
    REQUIRE(device);
    auto renderer = Renderer::create(**device, kSize, kSize, true);
    INFO(errorOf(renderer));
    REQUIRE(renderer);
    auto mesh = createMesh(**device, cutoutQuad(), "lmx.test.cutout.backFace");
    REQUIRE(mesh);
    DrawItem item;
    item.mesh = &*mesh;
    item.material.alphaMode = AlphaMode::Mask;
    item.material.alphaCutoff = 0.5f;
    item.material.albedo.a = 0.5f;
    item.material.doubleSided = true;
    auto view = cutoutView(std::span{&item, 1});
    view.temporal.enabled = false;
    const auto front = cutoutFrame(**device, **renderer, view);
    item.model = glm::rotate(glm::mat4{1}, glm::pi<float>(), glm::vec3{0, 1, 0});
    const auto back = cutoutFrame(**device, **renderer, view);
    REQUIRE(cutoutChannel(front, 32, 0) > 0.15f);
    REQUIRE(std::abs(cutoutChannel(front, 32, 0) - cutoutChannel(back, 32, 0)) < 0.002f);
    item.material.doubleSided = false;
    const auto culled = cutoutFrame(**device, **renderer, view);
    REQUIRE(cutoutChannel(culled, 32, 0) < 0.1f);
}
