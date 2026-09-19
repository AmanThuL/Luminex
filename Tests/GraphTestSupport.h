#pragma once

#include <rojoRHI/RHI.h>

#include <cstdlib>
#include <cstring>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace rojoRHI;

struct FakeCommandList : rojoRHI::CommandList {
    struct Request {
        uint32_t slot = 0;
        const void* data = nullptr;
        uint64_t size = 0;
        uint64_t alignment = 0;
    };
    std::vector<Request> requests;
    uint64_t nextAddress = 4096;
    std::vector<TemporalScaleParams> temporalScales;
    std::deque<std::string> temporalLabels;

    //==================================================================================================================
    GpuAddress bindFrameData(uint32_t slot, const void* data, uint64_t size,
                             uint64_t alignment) override {
        requests.push_back({slot, data, size, alignment});
        nextAddress += alignment;
        return GpuAddress{nextAddress};
    }
    using CommandList::bindFrameData;

    //==================================================================================================================
    void temporalScale(TemporalScaler&, const TemporalScaleParams& params) override {
        temporalLabels.emplace_back(params.label);
        temporalScales.push_back(params);
        temporalScales.back().label = temporalLabels.back();
    }

    //==================================================================================================================
    void beginRenderPass(const RenderPassDesc&) override {}

    //==================================================================================================================
    void endRenderPass() override {}

    //==================================================================================================================
    void beginComputePass(std::string_view) override {}

    //==================================================================================================================
    void endComputePass() override {}

    //==================================================================================================================
    void bindComputePipeline(ComputePipeline&) override {}

    //==================================================================================================================
    void bindStorageBuffer(uint32_t, Buffer&, StorageAccess) override {}

    //==================================================================================================================
    void bindStorageTexture(uint32_t, Texture&, const TextureViewDesc&, StorageAccess) override {}

    //==================================================================================================================
    void dispatch(uint32_t, uint32_t, uint32_t) override {}

    //==================================================================================================================
    void dispatchIndirect(Buffer&, uint64_t) override {}

    //==================================================================================================================
    void beginCopyPass(std::string_view) override {}

    //==================================================================================================================
    void endCopyPass() override {}

    //==================================================================================================================
    void copyBuffer(Buffer&, uint64_t, Buffer&, uint64_t, uint64_t) override {}

    //==================================================================================================================
    void copyBufferToTexture(Buffer&, const BufferTextureLayout&, Texture&,
                             const TextureCopyRegion&) override {}

    //==================================================================================================================
    void copyTextureToBuffer(Texture&, const TextureCopyRegion&, Buffer&,
                             const BufferTextureLayout&) override {}

    //==================================================================================================================
    void copyTexture(Texture&, const TextureCopyRegion&, Texture&,
                     const TextureCopyRegion&) override {}

    //==================================================================================================================
    void fillBuffer(Buffer&, uint64_t, uint64_t, uint8_t) override {}

    //==================================================================================================================
    void bindPipeline(GraphicsPipeline&) override {}

    //==================================================================================================================
    void bindBuffer(uint32_t, Buffer&) override {}

    //==================================================================================================================
    void bindTexture(uint32_t, Texture&, const TextureViewDesc&) override {}

    //==================================================================================================================
    void bindSampler(uint32_t, Sampler&) override {}

    //==================================================================================================================
    void draw(uint32_t, uint32_t) override {}

    //==================================================================================================================
    void drawIndexed(Buffer&, uint32_t, uint32_t) override {}

    //==================================================================================================================
    void drawIndirect(Buffer&, uint64_t) override {}

    //==================================================================================================================
    void drawIndexedIndirect(Buffer&, Buffer&, uint64_t) override {}

    //==================================================================================================================
    void textureBarrier(Texture&, const TextureSubresourceRange&, TextureUse, TextureUse,
                        BarrierOptions) override {}

    //==================================================================================================================
    void bufferBarrier(Buffer&, const BufferRange&, BufferUse, BufferUse, BarrierOptions) override {
    }
};

struct FakeGraphicsPipeline final : rojoRHI::GraphicsPipeline {
    std::string label;
};

struct FakeComputePipeline final : rojoRHI::ComputePipeline {
    std::string label;
};

// Deterministic allocation policy and no-op GPU objects for graph declarations.
struct FakeDevice final : rojoRHI::Device {
    static constexpr uint64_t kTextureAlignment = 16384;
    static constexpr uint64_t kBufferAlignment = 256;
    static constexpr uint64_t kBytesPerTexel = 4;

    // The heap retains its requested size; placed objects own descriptors and no GPU storage.
    struct FakeHeap final : rojoRHI::Heap {

        //==============================================================================================================
        explicit FakeHeap(uint64_t bytes) : m_size(bytes) {}

        //==============================================================================================================
        uint64_t size() const override { return m_size; }

    private:
        uint64_t m_size = 0;
    };

    // Tests may advance this directly or use endFrame to rotate the pool's frame slots.
    uint64_t frame = 0;
    FakeCommandList commands;
    struct TextureObject final : rojoRHI::Texture {
        rojoRHI::TextureDesc desc;
        std::string label;

        //==============================================================================================================
        explicit TextureObject(const rojoRHI::TextureDesc& value)
            : desc(value), label(value.label) {
            desc.label = label;
        }

        //==============================================================================================================
        uint32_t width() const override { return desc.width; }

        //==============================================================================================================
        uint32_t height() const override { return desc.height; }

        //==============================================================================================================
        rojoRHI::Format format() const override { return desc.format; }

        //==============================================================================================================
        uint32_t mipLevels() const override { return desc.mipLevels; }

        //==============================================================================================================
        uint32_t arrayLayers() const override {
            return desc.kind == rojoRHI::TextureKind::Cube ? 6 : 1;
        }

        //==============================================================================================================
        void readback(void*, uint64_t) override { std::abort(); }
    };
    struct BufferObject final : rojoRHI::Buffer {
        std::vector<std::byte> storage;

        //==============================================================================================================
        explicit BufferObject(uint64_t size, const void* initialData = nullptr) : storage(size) {
            if (initialData) {
                std::memcpy(storage.data(), initialData, size);
            }
        }

        //==============================================================================================================
        uint64_t size() const override { return storage.size(); }

        //==============================================================================================================
        void readback(void* destination, uint64_t size) override {
            if (size > storage.size()) {
                std::abort();
            }
            std::memcpy(destination, storage.data(), size);
        }

        //==============================================================================================================
        void write(uint64_t offset, const void* source, uint64_t size) override {
            if (offset > storage.size()) {
                std::abort();
            }
            if (size > storage.size() - offset) {
                std::abort();
            }
            std::memcpy(storage.data() + offset, source, size);
        }
    };
    rojoRHI::DeviceCapabilities deviceCaps;
    bool failTemporalScalerCreation = false;
    std::vector<rojoRHI::TemporalScalerDesc> temporalScalerCreations;
    std::deque<std::string> temporalScalerLabels;
    struct FakeTemporalScaler final : rojoRHI::TemporalScaler {};

    //==================================================================================================================
    const rojoRHI::DeviceCapabilities& capabilities() const override { return deviceCaps; }

    //==================================================================================================================
    rojoRHI::Result<std::unique_ptr<rojoRHI::TemporalScaler>>
    createTemporalScaler(const rojoRHI::TemporalScalerDesc& desc) override {
        temporalScalerLabels.emplace_back(desc.label);
        temporalScalerCreations.push_back(desc);
        temporalScalerCreations.back().label = temporalScalerLabels.back();
        if (!deviceCaps.temporalScaler.available || failTemporalScalerCreation) {
            return std::unexpected(rojoRHI::Error{rojoRHI::ErrorCode::ResourceCreationFailed,
                                                   "fake temporal scaler unavailable"});
        }
        return std::make_unique<FakeTemporalScaler>();
    }

    //==================================================================================================================
    static uint64_t alignUp(uint64_t value, uint64_t alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }

    //==================================================================================================================
    rojoRHI::SizeAlign textureSizeAlign(const rojoRHI::TextureDesc& desc) const override {
        const uint32_t faces = desc.kind == rojoRHI::TextureKind::Cube ? 6 : 1;
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
    rojoRHI::SizeAlign bufferSizeAlign(const rojoRHI::BufferDesc& desc) const override {
        return {.size = alignUp(desc.size, kBufferAlignment), .alignment = kBufferAlignment};
    }

    //==================================================================================================================
    rojoRHI::Result<std::unique_ptr<rojoRHI::Swapchain>>
    createSwapchain(const rojoRHI::SwapchainDesc&) override {
        std::abort();
    }

    //==================================================================================================================
    rojoRHI::Result<std::unique_ptr<rojoRHI::Buffer>>
    createBuffer(const rojoRHI::BufferDesc& desc, const void* initialData) override {
        return std::make_unique<BufferObject>(desc.size, initialData);
    }

    //==================================================================================================================
    rojoRHI::Result<std::unique_ptr<rojoRHI::Texture>>
    createTexture(const rojoRHI::TextureDesc& desc,
                  std::span<const rojoRHI::TextureMip>) override {
        return std::make_unique<TextureObject>(desc);
    }

    //==================================================================================================================
    rojoRHI::Result<std::unique_ptr<rojoRHI::Heap>>
    createHeap(const rojoRHI::HeapDesc& desc) override {
        return std::make_unique<FakeHeap>(desc.size);
    }

    //==================================================================================================================
    rojoRHI::Result<std::unique_ptr<rojoRHI::Texture>>
    createPlacedTexture(rojoRHI::Heap&, uint64_t, const rojoRHI::TextureDesc& desc) override {
        return std::make_unique<TextureObject>(desc);
    }

    //==================================================================================================================
    rojoRHI::Result<std::unique_ptr<rojoRHI::Buffer>>
    createPlacedBuffer(rojoRHI::Heap&, uint64_t, const rojoRHI::BufferDesc& desc) override {
        return std::make_unique<BufferObject>(desc.size);
    }

    //==================================================================================================================
    rojoRHI::Result<std::unique_ptr<rojoRHI::Sampler>>
    createSampler(const rojoRHI::SamplerDesc&) override {
        return std::make_unique<rojoRHI::Sampler>();
    }

    //==================================================================================================================
    rojoRHI::Result<std::unique_ptr<rojoRHI::ShaderLibrary>>
    loadShaderLibrary(std::string_view) override {
        return std::make_unique<rojoRHI::ShaderLibrary>();
    }

    //==================================================================================================================
    rojoRHI::Result<std::unique_ptr<rojoRHI::GraphicsPipeline>>
    createGraphicsPipeline(const rojoRHI::GraphicsPipelineDesc& desc) override {
        auto result = std::make_unique<FakeGraphicsPipeline>();
        result->label = desc.label;
        return result;
    }

    //==================================================================================================================
    rojoRHI::Result<std::unique_ptr<rojoRHI::ComputePipeline>>
    createComputePipeline(const rojoRHI::ComputePipelineDesc& desc) override {
        auto result = std::make_unique<FakeComputePipeline>();
        result->label = desc.label;
        return result;
    }

    //==================================================================================================================
    rojoRHI::CommandList& beginFrame() override { return commands; }

    //==================================================================================================================
    void endFrame(rojoRHI::Swapchain*) override { ++frame; }

    //==================================================================================================================
    void waitIdle() override {}

    //==================================================================================================================
    std::span<const rojoRHI::PassTiming> passTimings() const override { return {}; }

    //==================================================================================================================
    uint64_t passTimingsFrame() const override { return 0; }

    //==================================================================================================================
    uint64_t frameNumber() const override { return frame; }

    //==================================================================================================================
    std::string_view deviceName() const override { return "lmx.test.fakeDevice"; }
};

} // namespace
