#pragma once

#include <catch2/catch_test_macros.hpp>

#include "GraphTestSupport.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Graph/TransientPool.h"

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
struct FakeTexture final : rojoRHI::Texture {
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
    rojoRHI::Format format() const override { return rojoRHI::Format::Unknown; }

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
struct FakeBuffer final : rojoRHI::Buffer {
    std::string name;

    //==================================================================================================================
    explicit FakeBuffer(uint64_t size, std::string label = {})
        : name(std::move(label)), m_size(size) {}

    //==================================================================================================================
    uint64_t size() const override { return m_size; }

    //==================================================================================================================
    void readback(void*, uint64_t) override {}

    //==================================================================================================================
    void write(uint64_t, const void*, uint64_t) override {}

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
inline std::string useName(rojoRHI::TextureUse use) {
    switch (use) {
    case rojoRHI::TextureUse::RenderTarget:
        return "RenderTarget";
    case rojoRHI::TextureUse::ShaderRead:
        return "ShaderRead";
    case rojoRHI::TextureUse::StorageRead:
        return "StorageRead";
    case rojoRHI::TextureUse::StorageWrite:
        return "StorageWrite";
    case rojoRHI::TextureUse::CopySource:
        return "CopySource";
    case rojoRHI::TextureUse::CopyDestination:
        return "CopyDestination";
    case rojoRHI::TextureUse::ExternalRead:
        return "ExternalRead";
    case rojoRHI::TextureUse::ExternalWrite:
        return "ExternalWrite";
    }
    return "unknown";
}

//======================================================================================================================
inline std::string useName(rojoRHI::BufferUse use) {
    switch (use) {
    case rojoRHI::BufferUse::ShaderRead:
        return "ShaderRead";
    case rojoRHI::BufferUse::StorageRead:
        return "StorageRead";
    case rojoRHI::BufferUse::StorageWrite:
        return "StorageWrite";
    case rojoRHI::BufferUse::CopySource:
        return "CopySource";
    case rojoRHI::BufferUse::CopyDestination:
        return "CopyDestination";
    case rojoRHI::BufferUse::IndirectArgument:
        return "IndirectArgument";
    }
    return "unknown";
}

// What execute() produces is a sequence of RHI calls, so recording that sequence is what makes it
// observable without a device. Only the calls execute() itself makes are recorded; the draw-level
// binds a pass body might make are not this layer's output and are left as no-ops.
struct RecordingCommandList final : rojoRHI::CommandList {
    // The order of passes and of the barriers between them, as one flat log -- both are ordering,
    // and separate lists would not say which came first.
    std::vector<std::string> events;
    // Every begun pass, for the attachment assertions the log cannot carry.
    std::vector<rojoRHI::RenderPassDesc> passes;
    std::vector<rojoRHI::TemporalScaleParams> temporalScales;
    std::deque<std::string> temporalLabels;

    //==================================================================================================================
    void temporalScale(rojoRHI::TemporalScaler&,
                       const rojoRHI::TemporalScaleParams& params) override {
        temporalLabels.emplace_back(params.label);
        temporalScales.push_back(params);
        temporalScales.back().label = temporalLabels.back();
        events.push_back("external " + std::string(params.label));
    }

    //==================================================================================================================
    void beginRenderPass(const rojoRHI::RenderPassDesc& desc) override {
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
    void bindComputePipeline(rojoRHI::ComputePipeline&) override {}

    //==================================================================================================================
    void bindStorageBuffer(uint32_t, rojoRHI::Buffer&, rojoRHI::StorageAccess) override {}

    //==================================================================================================================
    void bindStorageTexture(uint32_t, rojoRHI::Texture&, const rojoRHI::TextureViewDesc&,
                            rojoRHI::StorageAccess) override {}

    //==================================================================================================================
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        events.push_back("dispatch " + std::to_string(x) + "," + std::to_string(y) + "," +
                         std::to_string(z));
    }

    //==================================================================================================================
    void dispatchIndirect(rojoRHI::Buffer&, uint64_t) override {}

    // Copy pass boundaries log for the same reason the compute ones do; the copies a body records
    // are the body's own output and are left as no-ops.

    //==================================================================================================================
    void beginCopyPass(std::string_view label) override {
        events.push_back("begin copy " + std::string(label));
    }

    //==================================================================================================================
    void endCopyPass() override { events.push_back("end copy"); }

    //==================================================================================================================
    void copyBuffer(rojoRHI::Buffer&, uint64_t, rojoRHI::Buffer&, uint64_t, uint64_t) override {}

    //==================================================================================================================
    void copyBufferToTexture(rojoRHI::Buffer&, const rojoRHI::BufferTextureLayout&,
                             rojoRHI::Texture&, const rojoRHI::TextureCopyRegion&) override {}

    //==================================================================================================================
    void copyTextureToBuffer(rojoRHI::Texture&, const rojoRHI::TextureCopyRegion&, rojoRHI::Buffer&,
                             const rojoRHI::BufferTextureLayout&) override {}

    //==================================================================================================================
    void copyTexture(rojoRHI::Texture&, const rojoRHI::TextureCopyRegion&, rojoRHI::Texture&,
                     const rojoRHI::TextureCopyRegion&) override {}

    //==================================================================================================================
    void fillBuffer(rojoRHI::Buffer&, uint64_t, uint64_t, uint8_t) override {}

    //==================================================================================================================
    void bufferBarrier(rojoRHI::Buffer& buffer, const rojoRHI::BufferRange&,
                       rojoRHI::BufferUse from, rojoRHI::BufferUse to,
                       rojoRHI::BarrierOptions) override {
        events.push_back("barrier " + static_cast<FakeBuffer&>(buffer).name + " " + useName(from) +
                         "->" + useName(to));
    }

    //==================================================================================================================
    void textureBarrier(rojoRHI::Texture& texture, const rojoRHI::TextureSubresourceRange& range,
                        rojoRHI::TextureUse from, rojoRHI::TextureUse to,
                        rojoRHI::BarrierOptions) override {
        // A whole-resource range is what a pass with no subresource detail declares and is the
        // common case, so it is left out of the log; a narrowed one is spelled out, because a
        // barrier covering the wrong subresources is exactly what these cases are looking for.
        const bool wholeResource =
            range.baseMipLevel == 0 && range.mipLevelCount == rojoRHI::kAllMipLevels &&
            range.baseArrayLayer == 0 && range.arrayLayerCount == rojoRHI::kAllArrayLayers;
        events.push_back("barrier " + static_cast<FakeTexture&>(texture).name +
                         (wholeResource ? "" : " " + describeRange(range)) + " " + useName(from) +
                         "->" + useName(to));
    }

    //==================================================================================================================
    void bindPipeline(rojoRHI::GraphicsPipeline&) override {}

    //==================================================================================================================
    void bindBuffer(uint32_t, rojoRHI::Buffer&) override {}

    //==================================================================================================================
    void bindTexture(uint32_t, rojoRHI::Texture&, const rojoRHI::TextureViewDesc&) override {}

    //==================================================================================================================
    void bindSampler(uint32_t, rojoRHI::Sampler&) override {}

    //==================================================================================================================
    rojoRHI::GpuAddress bindFrameData(uint32_t, const void*, uint64_t, uint64_t) override {
        return {};
    }

    //==================================================================================================================
    void draw(uint32_t, uint32_t) override {}

    //==================================================================================================================
    void drawIndexed(rojoRHI::Buffer&, uint32_t, uint32_t) override {}

    //==================================================================================================================
    void drawIndirect(rojoRHI::Buffer&, uint64_t) override {}

    //==================================================================================================================
    void drawIndexedIndirect(rojoRHI::Buffer&, rojoRHI::Buffer&, uint64_t) override {}
};
} // namespace

namespace {

// One transient the fake device sizes to exactly one alignment unit: 64 * 64 texels at four bytes
// each is 16 KiB, which is FakeDevice::kTextureAlignment. Every offset below is therefore a
// multiple of the size, which keeps the packing readable in the assertions.
[[maybe_unused]] constexpr TransientTextureDesc kTransientColor{.width = 64,
                                                                .height = 64,
                                                                .format =
                                                                    rojoRHI::Format::RGBA16Float,
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
        const GraphTexture mid = graph.importTexture(midTarget, rojoRHI::Format::BGRA8Unorm, "mid");
        const GraphTexture out = graph.importTexture(outTarget, rojoRHI::Format::BGRA8Unorm, "out");

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
