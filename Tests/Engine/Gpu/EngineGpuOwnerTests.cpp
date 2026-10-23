#include <catch2/catch_test_macros.hpp>

#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Engine/Gpu/GpuTexture.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

//======================================================================================================================
std::unique_ptr<lmx::engine::GpuContext> makeGpu() {
    lmx::engine::GpuContextDesc desc;
#ifndef NDEBUG
    desc.validation = true;
#endif
    auto gpu = lmx::engine::GpuContext::create(desc);
    REQUIRE(gpu);
    return std::move(*gpu);
}

//======================================================================================================================
template <typename Owner>
bool viewsStorageOf(std::string_view text, const Owner& owner) {
    const auto* begin = reinterpret_cast<const std::byte*>(&owner);
    const auto* data = reinterpret_cast<const std::byte*>(text.data());
    return data >= begin && data < begin + sizeof(Owner);
}

//======================================================================================================================
void recycle(lmx::engine::GpuContext& gpu, uint32_t frames) {
    for (uint32_t index = 0; index < frames; ++index) {
        auto frame = gpu.beginFrame();
        gpu.submitAndEndFrame(frame);
    }
    gpu.waitIdle();
}

} // namespace

//======================================================================================================================
TEST_CASE("GpuTexture keeps a descriptor copy whose label outlives the caller's string",
          "[engine][device]") {
    auto gpu = makeGpu();
    lmx::engine::GpuTexture texture;
    {
        std::string label = "luminex.test.texture-with-a-label-longer-than-inline-storage";
        auto created =
            lmx::engine::GpuTexture::create(gpu->device(), {.format = rojoRHI::Format::RGBA8Unorm,
                                                            .extent = {64, 32, 1},
                                                            .usage = rojoRHI::TextureUsage::Sampled,
                                                            .label = label});
        REQUIRE(created);
        texture = std::move(*created);
        label.assign(label.size(), 'x');
    }
    CHECK(texture.desc().label == "luminex.test.texture-with-a-label-longer-than-inline-storage");
    CHECK(texture.desc().extent.width == 64);
    CHECK(texture.desc().extent.height == 32);
}

//======================================================================================================================
TEST_CASE("GpuTexture moves without dangling its label", "[engine][device]") {
    auto gpu = makeGpu();
    auto texture = lmx::engine::GpuTexture::create(
        gpu->device(),
        {.format = rojoRHI::Format::RGBA8Unorm, .extent = {8, 8, 1}, .label = "short"});
    REQUIRE(texture);
    auto source = std::move(*texture);
    const auto object = source.rhi();
    lmx::engine::GpuTexture constructed{std::move(source)};
    CHECK(constructed.desc().label == "short");
    CHECK(viewsStorageOf(constructed.desc().label, constructed));
    CHECK(constructed.rhi() == object);
    CHECK_FALSE(source); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    lmx::engine::GpuTexture assigned;
    assigned = std::move(constructed);
    CHECK(assigned.desc().label == "short");
    CHECK(viewsStorageOf(assigned.desc().label, assigned));
    CHECK(assigned.rhi() == object);
    CHECK_FALSE(constructed); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    auto buffer = lmx::engine::GpuBuffer::create(gpu->device(), {.size = 64, .label = "short"});
    REQUIRE(buffer);
    auto bufferSource = std::move(*buffer);
    const auto bufferObject = bufferSource.rhi();
    lmx::engine::GpuBuffer bufferConstructed{std::move(bufferSource)};
    CHECK(bufferConstructed.desc().label == "short");
    CHECK(viewsStorageOf(bufferConstructed.desc().label, bufferConstructed));
    CHECK(bufferConstructed.rhi() == bufferObject);
    CHECK_FALSE(bufferSource); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    lmx::engine::GpuBuffer bufferAssigned;
    bufferAssigned = std::move(bufferConstructed);
    CHECK(bufferAssigned.desc().label == "short");
    CHECK(viewsStorageOf(bufferAssigned.desc().label, bufferAssigned));
    CHECK(bufferAssigned.rhi() == bufferObject);
    CHECK_FALSE(bufferConstructed); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}

//======================================================================================================================
TEST_CASE("GpuBuffer keeps size and usage in its descriptor", "[engine][device]") {
    auto gpu = makeGpu();
    auto buffer = lmx::engine::GpuBuffer::create(
        gpu->device(),
        {.size = 4096, .memory = rojoRHI::Memory::Readback, .label = "luminex.test.buffer"});
    REQUIRE(buffer);
    CHECK(buffer->desc().size == 4096);
    CHECK(buffer->desc().memory == rojoRHI::Memory::Readback);
    CHECK(buffer->desc().usage == rojoRHI::BufferUsage::None);
    CHECK(buffer->cpuPointer() != nullptr);
    CHECK(buffer->desc().label == "luminex.test.buffer");
}

//======================================================================================================================
TEST_CASE("GpuOwned reset releases the object", "[engine][device]") {
    auto gpu = makeGpu();
    auto result = lmx::engine::adoptGpu(
        gpu->device(), gpu->device().createBuffer({.size = 64, .label = "lmx.test.owned"}));
    REQUIRE(result);
    const auto before = gpu->device().counters().deferredDestructionsRetired;
    result->reset();
    CHECK_FALSE(*result);
    recycle(*gpu, 4);
    CHECK(gpu->device().counters().deferredDestructionsRetired == before + 1);
}

//======================================================================================================================
TEST_CASE("GpuOwned reset destroys once and moves without double destroy", "[engine][device]") {
    auto gpu = makeGpu();
    const auto before = gpu->device().counters().deferredDestructionsRetired;
    {
        auto created = lmx::engine::adoptGpu(
            gpu->device(), gpu->device().createBuffer({.size = 64, .label = "lmx.test.ownedMove"}));
        REQUIRE(created);
        lmx::engine::GpuOwned<rojoRHI::Buffer> moved{std::move(*created)};
        CHECK_FALSE(*created); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        lmx::engine::GpuOwned<rojoRHI::Buffer> assigned;
        assigned = std::move(moved);
        CHECK_FALSE(moved); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        assigned.reset();
        assigned.reset();
        gpu->waitIdle();
        CHECK(gpu->device().counters().deferredDestructionsRetired == before);
        recycle(*gpu, 4);
        CHECK(gpu->device().counters().deferredDestructionsRetired == before + 1);
    }
    recycle(*gpu, 4);
    CHECK(gpu->device().counters().deferredDestructionsRetired == before + 1);
}

//======================================================================================================================
TEST_CASE("GpuTexture retains immutable creation layout and underlying resource dimensions",
          "[engine][device]") {
    auto gpu = makeGpu();
    lmx::engine::TextureRef empty;
    CHECK(empty.mips == 1);
    CHECK(empty.layers == 1);
    CHECK(empty.initialLayout == rojoRHI::Layout::Undefined);
    auto layered = lmx::engine::GpuTexture::create(
        gpu->device(),
        {.type = rojoRHI::TextureType::Tex2DArray,
         .format = rojoRHI::Format::RGBA8Unorm,
         .extent = {16, 8, 1},
         .mips = 3,
         .layers = 2,
         .usage = rojoRHI::TextureUsage::Sampled | rojoRHI::TextureUsage::Storage,
         .label = "lmx.test.layered"},
        {}, {.storageMips = true});
    REQUIRE(layered);
    CHECK(layered->initialLayout() == rojoRHI::Layout::Undefined);
    CHECK(layered->ref().mips == 3);
    CHECK(layered->ref().layers == 2);
    CHECK(layered->storageViewCount() == 3);
    CHECK(layered->storageView(0) != lmx::engine::kNoView);
    CHECK(layered->storageView(2) != lmx::engine::kNoView);
    CHECK(layered->view() != lmx::engine::kNoView);
    auto moved = std::move(*layered);
    CHECK(moved.ref().mips == 3);
    CHECK(moved.ref().layers == 2);
    CHECK(moved.initialLayout() == rojoRHI::Layout::Undefined);
    auto sampledOnly = lmx::engine::GpuTexture::create(
        gpu->device(), {.format = rojoRHI::Format::RGBA8Unorm,
                        .usage = rojoRHI::TextureUsage::Sampled | rojoRHI::TextureUsage::Storage,
                        .label = "lmx.test.sampledOnly"});
    REQUIRE(sampledOnly);
    CHECK(sampledOnly->storageViewCount() == 0);
    const std::array color{std::byte{0x22}, std::byte{0x33}, std::byte{0x44}, std::byte{0xff}};
    const float depth = 0.75f;
    const std::array colorData{rojoRHI::TextureData{std::span(color), 4, 0}};
    const std::array depthData{rojoRHI::TextureData{std::as_bytes(std::span(&depth, 1)), 4, 0}};
    auto initialized = lmx::engine::GpuTexture::create(gpu->device(),
                                                       {.format = rojoRHI::Format::RGBA8Unorm,
                                                        .usage = rojoRHI::TextureUsage::Sampled,
                                                        .label = "lmx.test.initialized"},
                                                       colorData);
    REQUIRE(initialized);
    auto initializedDepth = lmx::engine::GpuTexture::create(
        gpu->device(),
        {.format = rojoRHI::Format::D32Float,
         .usage = rojoRHI::TextureUsage::DepthStencil | rojoRHI::TextureUsage::Sampled,
         .label = "lmx.test.initializedDepth"},
        depthData);
    REQUIRE(initializedDepth);
    CHECK(initialized->initialLayout() == rojoRHI::Layout::ShaderRead);
    CHECK(initializedDepth->initialLayout() == rojoRHI::Layout::ShaderRead);
    const auto depthIdentity = initializedDepth->rhi();
    auto depthMoved = std::move(*initializedDepth);
    CHECK(depthMoved.ref().texture == depthIdentity);
    CHECK(depthMoved.ref().initialLayout == rojoRHI::Layout::ShaderRead);
    auto frame = gpu->beginFrame();
    const std::array barrier{
        rojoRHI::TextureBarrier{.texture = depthMoved.rhi(),
                                .srcStage = rojoRHI::Stage::None,
                                .srcAccess = rojoRHI::Access::None,
                                .dstStage = rojoRHI::Stage::FragmentShader,
                                .dstAccess = rojoRHI::Access::ShaderRead,
                                .oldLayout = rojoRHI::Layout::ShaderRead,
                                .newLayout = rojoRHI::Layout::DepthStencilRead}};
    frame.commands.barrier({}, {}, barrier);
    gpu->submitAndEndFrame(frame);
    gpu->waitIdle();
    CHECK(depthMoved.initialLayout() == rojoRHI::Layout::ShaderRead);
    CHECK(depthMoved.ref().initialLayout == rojoRHI::Layout::ShaderRead);
}
