#pragma once

#include "RHI/RHI.h"

#include <cstdlib>
#include <span>
#include <string_view>

namespace {

// A device that creates nothing and answers only the two queries a render graph plans a heap
// layout from.
//
// The numbers are fixed and stated here rather than taken from a backend, because a compiled plan
// is deterministic *under a documented alignment policy* -- so a golden file can hold one only if
// the policy it was produced under is written down. A texture costs four bytes a texel over its
// whole chain, rounded up to a 16 KiB alignment; a buffer is rounded up to 256. Both are shaped
// like a real allocator's answers without being any particular one's.
//
// Everything else aborts: a unit test that reaches for a pipeline or a frame has left the layer
// these fakes stand in for, and answering it with a null would hide that.
struct FakeDevice final : lmx::rhi::Device {
    static constexpr uint64_t kTextureAlignment = 16384;
    static constexpr uint64_t kBufferAlignment = 256;
    static constexpr uint64_t kBytesPerTexel = 4;

    //==================================================================================================================
    static uint64_t alignUp(uint64_t value, uint64_t alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }

    //==================================================================================================================
    lmx::rhi::SizeAlign textureSizeAlign(const lmx::rhi::TextureDesc& desc) const override {
        const uint32_t faces = desc.kind == lmx::rhi::TextureKind::Cube ? 6 : 1;
        uint64_t texels = 0;
        for (uint32_t level = 0; level < desc.mipLevels; ++level) {
            const uint64_t width = desc.width >> level;
            const uint64_t height = desc.height >> level;
            texels += (width > 0 ? width : 1) * (height > 0 ? height : 1);
        }
        return {.size = alignUp(texels * faces * kBytesPerTexel, kTextureAlignment),
                .alignment = kTextureAlignment};
    }

    //==================================================================================================================
    lmx::rhi::SizeAlign bufferSizeAlign(const lmx::rhi::BufferDesc& desc) const override {
        return {.size = alignUp(desc.size, kBufferAlignment), .alignment = kBufferAlignment};
    }

    //==================================================================================================================
    lmx::rhi::Result<std::unique_ptr<lmx::rhi::Swapchain>>
    createSwapchain(const lmx::rhi::SwapchainDesc&) override {
        std::abort();
    }

    //==================================================================================================================
    lmx::rhi::Result<std::unique_ptr<lmx::rhi::Buffer>> createBuffer(const lmx::rhi::BufferDesc&,
                                                                     const void*) override {
        std::abort();
    }

    //==================================================================================================================
    lmx::rhi::Result<std::unique_ptr<lmx::rhi::Texture>>
    createTexture(const lmx::rhi::TextureDesc&, std::span<const lmx::rhi::TextureMip>) override {
        std::abort();
    }

    //==================================================================================================================
    lmx::rhi::Result<std::unique_ptr<lmx::rhi::Heap>> createHeap(const lmx::rhi::HeapDesc&) override {
        std::abort();
    }

    //==================================================================================================================
    lmx::rhi::Result<std::unique_ptr<lmx::rhi::Texture>>
    createPlacedTexture(lmx::rhi::Heap&, uint64_t, const lmx::rhi::TextureDesc&) override {
        std::abort();
    }

    //==================================================================================================================
    lmx::rhi::Result<std::unique_ptr<lmx::rhi::Buffer>>
    createPlacedBuffer(lmx::rhi::Heap&, uint64_t, const lmx::rhi::BufferDesc&) override {
        std::abort();
    }

    //==================================================================================================================
    lmx::rhi::Result<std::unique_ptr<lmx::rhi::Sampler>>
    createSampler(const lmx::rhi::SamplerDesc&) override {
        std::abort();
    }

    //==================================================================================================================
    lmx::rhi::Result<std::unique_ptr<lmx::rhi::ShaderLibrary>>
    loadShaderLibrary(std::string_view) override {
        std::abort();
    }

    //==================================================================================================================
    lmx::rhi::Result<std::unique_ptr<lmx::rhi::GraphicsPipeline>>
    createGraphicsPipeline(const lmx::rhi::GraphicsPipelineDesc&) override {
        std::abort();
    }

    //==================================================================================================================
    lmx::rhi::Result<std::unique_ptr<lmx::rhi::ComputePipeline>>
    createComputePipeline(const lmx::rhi::ComputePipelineDesc&) override {
        std::abort();
    }

    //==================================================================================================================
    lmx::rhi::CommandList& beginFrame() override { std::abort(); }

    //==================================================================================================================
    void endFrame(lmx::rhi::Swapchain*) override { std::abort(); }

    //==================================================================================================================
    void waitIdle() override { std::abort(); }

    //==================================================================================================================
    std::span<const lmx::rhi::PassTiming> passTimings() const override { return {}; }

    //==================================================================================================================
    uint64_t passTimingsFrame() const override { return 0; }

    //==================================================================================================================
    uint64_t frameNumber() const override { return 0; }

    //==================================================================================================================
    std::string_view deviceName() const override { return "lmx.test.fakeDevice"; }
};

} // namespace
