#include "Render/Gpu/Readback.h"
#include "Render/Passes/Occlusion/HzbStage.h"
#include "Support/TestGpu.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/vec2.hpp>

#include <algorithm>

//======================================================================================================================
TEST_CASE("HZB allocation holds every ceil-sized mip at every active scale", "[hzb]") {
    using namespace lmx::render;
    const auto ordinary = hzbLayout(1280, 720);
    REQUIRE(ordinary.width == 640);
    REQUIRE(ordinary.height == 384);
    REQUIRE(ordinary.levelCount == 7);
    REQUIRE(hzbLayout(1, 1).levelCount == 1);
    REQUIRE(hzbLevelExtent(65, 0) == 33);
    REQUIRE(hzbLevelExtent(65, 1) == 17);
    REQUIRE(hzbLevelExtent(65, 2) == 9);
    for (const auto extent : {glm::uvec2{1, 1}, glm::uvec2{31, 7}, glm::uvec2{1279, 719},
                              glm::uvec2{1280, 720}, glm::uvec2{16384, 1}}) {
        const auto layout = hzbLayout(extent.x, extent.y);
        uint64_t bytes = 0;
        for (uint32_t level = 0; level < layout.levelCount; ++level) {
            REQUIRE((layout.width >> level) >= hzbLevelExtent(extent.x, level));
            REQUIRE((layout.height >> level) >= hzbLevelExtent(extent.y, level));
            bytes += uint64_t{layout.width >> level} * (layout.height >> level) * 8;
        }
        REQUIRE(bytes == layout.bytes);
        REQUIRE(hzbLevelExtent(extent.x, layout.levelCount - 1) <= 16);
        REQUIRE(hzbLevelExtent(extent.y, layout.levelCount - 1) <= 16);
        if (layout.levelCount > 1) {
            REQUIRE(std::max(hzbLevelExtent(extent.x, layout.levelCount - 2),
                             hzbLevelExtent(extent.y, layout.levelCount - 2)) > 16);
        }
    }
}

//======================================================================================================================
TEST_CASE("R32Float sampled storage and reported attachment usage follow v2 capabilities",
          "[hzb][rhi][device]") {
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    REQUIRE(lmx::render::packedTexelBytes(Format::R32Float) == 4);
    const auto capabilities = gpu->device().formatCapabilities(Format::R32Float);
    REQUIRE(capabilities.sampled);
    REQUIRE(capabilities.storageRead);
    REQUIRE(capabilities.storageWrite);
    TextureDesc desc{.format = Format::R32Float,
                     .extent = {8, 8, 1},
                     .usage = TextureUsage::Sampled | TextureUsage::Storage,
                     .label = "lmx.test.r32"};
    auto texture = lmx::engine::GpuTexture::create(gpu->device(), desc, {}, {.storageMips = true});
    REQUIRE(texture);
    REQUIRE(texture->view() != lmx::engine::kNoView);
    REQUIRE(texture->storageViewCount() == 1);
    if (capabilities.colorAttachment) {
        desc.usage = desc.usage | TextureUsage::RenderTarget;
        auto attachment =
            lmx::engine::GpuTexture::create(gpu->device(), desc, {}, {.storageMips = true});
        REQUIRE(attachment);
    } else {
        REQUIRE_FALSE(capabilities.colorAttachment);
    }
}
