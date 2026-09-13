#pragma once

#include <catch2/catch_test_macros.hpp>

#include "GraphTestSupport.h"
#include "Render/RenderGraph.h"
#include "Render/TransientPool.h"

#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <ranges>
#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::render;

namespace {
// Texture is an interface, and the graph reads three things from it: the extent, which is what the
// attachment rules inspect since the format is declared at import, and the mip and layer counts,
// which a declared subresource range is resolved and bounds-checked against. readback() is never
// reached, so it is left empty rather than faked.
//
// `name` is the test's own label for the texture, so a recorded barrier says which resource it
// transitioned rather than printing a pointer.
struct FakeTexture final : rhi::Texture {
    std::string name;

    //==================================================================================================================
    FakeTexture(uint32_t width, uint32_t height, std::string label = {}, uint32_t mips = 1,
                uint32_t layers = 1)
        : name(std::move(label)), m_width(width), m_height(height), m_mipLevels(mips),
          m_arrayLayers(layers) {}

    //==================================================================================================================
    uint32_t width() const override { return m_width; }

    //==================================================================================================================
    uint32_t height() const override { return m_height; }

    // Unknown is the sentinel a test double uses when the graph case declares the format itself;
    // production textures always report their concrete creation format.
    //==================================================================================================================
    rhi::Format format() const override { return rhi::Format::Unknown; }

    //==================================================================================================================
    uint32_t mipLevels() const override { return m_mipLevels; }

    //==================================================================================================================
    uint32_t arrayLayers() const override { return m_arrayLayers; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_mipLevels = 1;
    uint32_t m_arrayLayers = 1;
};

// The graph never reads a buffer's size -- buffers carry no attachment or subresource rules at all
// -- so this exists to give importBuffer a real object to borrow and to name itself in a barrier.
struct FakeBuffer final : rhi::Buffer {
    std::string name;

    //==================================================================================================================
    explicit FakeBuffer(uint64_t size, std::string label = {})
        : name(std::move(label)), m_size(size) {}

    //==================================================================================================================
    uint64_t size() const override { return m_size; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

private:
    uint64_t m_size = 0;
};

// Passes here are declarations and nothing else: this layer stores the body without running it, so
// every pass gets the same empty one.
[[maybe_unused]] const ExecuteFn kNoWork = [](const PassResources&) {};

// REQUIRE(result.has_value()) on its own reports "false != true"; the graph's message is the only
// thing that says *why*, so it is pulled out for INFO before the assertion.
template <typename T>

//======================================================================================================================
inline std::string errorOf(const GraphResult<T>& result) {
    return result ? std::string{} : result.error().message;
}

//======================================================================================================================
inline std::string useName(rhi::TextureUse use) {
    switch (use) {
    case rhi::TextureUse::RenderTarget:
        return "RenderTarget";
    case rhi::TextureUse::ShaderRead:
        return "ShaderRead";
    case rhi::TextureUse::StorageRead:
        return "StorageRead";
    case rhi::TextureUse::StorageWrite:
        return "StorageWrite";
    case rhi::TextureUse::CopySource:
        return "CopySource";
    case rhi::TextureUse::CopyDestination:
        return "CopyDestination";
    case rhi::TextureUse::ExternalRead:
        return "ExternalRead";
    case rhi::TextureUse::ExternalWrite:
        return "ExternalWrite";
    }
    return "unknown";
}

//======================================================================================================================
inline std::string useName(rhi::BufferUse use) {
    switch (use) {
    case rhi::BufferUse::ShaderRead:
        return "ShaderRead";
    case rhi::BufferUse::StorageRead:
        return "StorageRead";
    case rhi::BufferUse::StorageWrite:
        return "StorageWrite";
    case rhi::BufferUse::CopySource:
        return "CopySource";
    case rhi::BufferUse::CopyDestination:
        return "CopyDestination";
    case rhi::BufferUse::IndirectArgument:
        return "IndirectArgument";
    }
    return "unknown";
}

// What execute() produces is a sequence of RHI calls, so recording that sequence is what makes it
// observable without a device. Only the calls execute() itself makes are recorded; the draw-level
// binds a pass body might make are not this layer's output and are left as no-ops.
struct RecordingCommandList final : rhi::CommandList {
    // The order of passes and of the barriers between them, as one flat log -- both are ordering,
    // and separate lists would not say which came first.
    std::vector<std::string> events;
    // Every begun pass, for the attachment assertions the log cannot carry.
    std::vector<rhi::RenderPassDesc> passes;
    std::vector<rhi::TemporalScaleParams> temporalScales;
    std::deque<std::string> temporalLabels;

    //==================================================================================================================
    void temporalScale(rhi::TemporalScaler&, const rhi::TemporalScaleParams& params) override {
        temporalLabels.emplace_back(params.label);
        temporalScales.push_back(params);
        temporalScales.back().label = temporalLabels.back();
        events.push_back("external " + std::string(params.label));
    }

    //==================================================================================================================
    void beginRenderPass(const rhi::RenderPassDesc& desc) override {
        passes.push_back(desc);
        events.push_back("begin " + std::string(desc.label));
    }

    //==================================================================================================================
    void endRenderPass() override { events.push_back("end"); }

    // Compute pass boundaries are part of the ordering this log exists to show, so they share the
    // event list with the render ones; the dispatches inside a body are the body's output, not
    // execute()'s, and are logged only so a stray one is visible.

    //==================================================================================================================
    void beginComputePass(std::string_view label) override {
        events.push_back("begin compute " + std::string(label));
    }

    //==================================================================================================================
    void endComputePass() override { events.push_back("end compute"); }

    //==================================================================================================================
    void bindComputePipeline(rhi::ComputePipeline&) override {}

    //==================================================================================================================
    void bindStorageBuffer(uint32_t, rhi::Buffer&, rhi::StorageAccess) override {}

    //==================================================================================================================
    void bindStorageTexture(uint32_t, rhi::Texture&, const rhi::TextureViewDesc&,
                            rhi::StorageAccess) override {}

    //==================================================================================================================
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        events.push_back("dispatch " + std::to_string(x) + "," + std::to_string(y) + "," +
                         std::to_string(z));
    }

    //==================================================================================================================
    void dispatchIndirect(rhi::Buffer&, uint64_t) override {}

    // Copy pass boundaries log for the same reason the compute ones do; the copies a body records
    // are the body's own output and are left as no-ops.

    //==================================================================================================================
    void beginCopyPass(std::string_view label) override {
        events.push_back("begin copy " + std::string(label));
    }

    //==================================================================================================================
    void endCopyPass() override { events.push_back("end copy"); }

    //==================================================================================================================
    void copyBuffer(rhi::Buffer&, uint64_t, rhi::Buffer&, uint64_t, uint64_t) override {}

    //==================================================================================================================
    void copyBufferToTexture(rhi::Buffer&, const rhi::BufferTextureLayout&, rhi::Texture&,
                             const rhi::TextureCopyRegion&) override {}

    //==================================================================================================================
    void copyTextureToBuffer(rhi::Texture&, const rhi::TextureCopyRegion&, rhi::Buffer&,
                             const rhi::BufferTextureLayout&) override {}

    //==================================================================================================================
    void copyTexture(rhi::Texture&, const rhi::TextureCopyRegion&, rhi::Texture&,
                     const rhi::TextureCopyRegion&) override {}

    //==================================================================================================================
    void fillBuffer(rhi::Buffer&, uint64_t, uint64_t, uint8_t) override {}

    //==================================================================================================================
    void bufferBarrier(rhi::Buffer& buffer, const rhi::BufferRange&, rhi::BufferUse from,
                       rhi::BufferUse to, rhi::BarrierOptions) override {
        events.push_back("barrier " + static_cast<FakeBuffer&>(buffer).name + " " + useName(from) +
                         "->" + useName(to));
    }

    //==================================================================================================================
    void textureBarrier(rhi::Texture& texture, const rhi::TextureSubresourceRange& range,
                        rhi::TextureUse from, rhi::TextureUse to, rhi::BarrierOptions) override {
        // A whole-resource range is what a pass with no subresource detail declares and is the
        // common case, so it is left out of the log; a narrowed one is spelled out, because a
        // barrier covering the wrong subresources is exactly what these cases are looking for.
        const bool wholeResource =
            range.baseMipLevel == 0 && range.mipLevelCount == rhi::kAllMipLevels &&
            range.baseArrayLayer == 0 && range.arrayLayerCount == rhi::kAllArrayLayers;
        events.push_back("barrier " + static_cast<FakeTexture&>(texture).name +
                         (wholeResource ? "" : " " + describeRange(range)) + " " + useName(from) +
                         "->" + useName(to));
    }

    //==================================================================================================================
    void bindPipeline(rhi::GraphicsPipeline&) override {}

    //==================================================================================================================
    void bindBuffer(uint32_t, rhi::Buffer&) override {}

    //==================================================================================================================
    void bindTexture(uint32_t, rhi::Texture&, const rhi::TextureViewDesc&) override {}

    //==================================================================================================================
    void bindSampler(uint32_t, rhi::Sampler&) override {}

    //==================================================================================================================
    rhi::GpuAddress bindFrameData(uint32_t, const void*, uint64_t, uint64_t) override { return {}; }

    //==================================================================================================================
    void draw(uint32_t, uint32_t) override {}

    //==================================================================================================================
    void drawIndexed(rhi::Buffer&, uint32_t, uint32_t) override {}

    //==================================================================================================================
    void drawIndirect(rhi::Buffer&, uint64_t) override {}

    //==================================================================================================================
    void drawIndexedIndirect(rhi::Buffer&, rhi::Buffer&, uint64_t) override {}
};
} // namespace

namespace {

// One transient the fake device sizes to exactly one alignment unit: 64 * 64 texels at four bytes
// each is 16 KiB, which is FakeDevice::kTextureAlignment. Every offset below is therefore a
// multiple of the size, which keeps the packing readable in the assertions.
[[maybe_unused]] constexpr TransientTextureDesc kTransientColor{.width = 64,
                                                                .height = 64,
                                                                .format = rhi::Format::RGBA16Float,
                                                                .renderTarget = true,
                                                                .sampled = true};

// Two transients used one after the other, with an imported target between them so the first one's
// lifetime closes before the second one's opens. It is the shape aliasing exists for.
struct DisjointFrame {
    FakeDevice device;
    TransientPool pool{device};
    FakeTexture midTarget{64, 64, "mid"};
    FakeTexture outTarget{64, 64, "out"};
    RenderGraph graph{pool};

    //==================================================================================================================
    // `secondDesc` is a parameter so a test can make the second transient incompatible with the
    // first without restating the frame.
    void declare(const TransientTextureDesc& secondDesc = kTransientColor) {
        const GraphTexture first = graph.createTexture(kTransientColor, "lmx.transient.first");
        const GraphTexture second = graph.createTexture(secondDesc, "lmx.transient.second");
        const GraphTexture mid = graph.importTexture(midTarget, rhi::Format::BGRA8Unorm, "mid");
        const GraphTexture out = graph.importTexture(outTarget, rhi::Format::BGRA8Unorm, "out");

        PassDesc writeFirst;
        writeFirst.color = ColorAttachment{.handle = first};
        graph.addPass("lmx.pass.writeFirst", writeFirst, kNoWork);

        PassDesc readFirst;
        readFirst.textureReads.push_back(nextVersion(first));
        readFirst.color = ColorAttachment{.handle = mid};
        graph.addPass("lmx.pass.readFirst", readFirst, kNoWork);

        PassDesc writeSecond;
        writeSecond.color = ColorAttachment{.handle = second};
        graph.addPass("lmx.pass.writeSecond", writeSecond, kNoWork);

        PassDesc readSecond;
        readSecond.textureReads.push_back(nextVersion(second));
        readSecond.color = ColorAttachment{.handle = out};
        graph.addPass("lmx.pass.readSecond", readSecond, kNoWork);

        graph.exportTexture(nextVersion(mid));
        graph.exportTexture(nextVersion(out));
    }
};

} // namespace
