#pragma once

#include "Render/Graph/TransientBackend.h"
#include "Render/Graph/TransientResourceDesc.h"

#include <rojoRHI/RHI.h>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// A transient backend with no device behind it. Footprints follow one deterministic rule (four
// bytes a texel over the whole mip chain, rounded up to 16 KiB; buffers rounded up to 256 bytes),
// so a layout's offsets and totals are exact numbers a case can assert. Heaps are records by id;
// placed resources are FakeTexture and FakeBuffer objects named by their labels, so a recorded
// barrier on a transient names it, and they live until their heap is released.
struct FakeTransientBackend final : lmx::render::TransientBackend {
    static constexpr uint64_t kTextureAlignment = 16384;
    static constexpr uint64_t kBufferAlignment = 256;
    static constexpr uint64_t kBytesPerTexel = 4;

    // One live heap and what is placed in it now.
    struct Heap {
        uint64_t bytes = 0;
        std::string label;
        std::vector<std::unique_ptr<FakeTexture>> textures;
        std::vector<std::unique_ptr<FakeBuffer>> buffers;
    };

    // The frame the pool reads; a case advances it directly.
    uint64_t frame = 0;
    // Live heaps keyed by id value.
    std::map<uint32_t, Heap> heaps;
    // Every heap destroyed so far, in destruction order.
    std::vector<uint32_t> destroyedHeaps;
    uint32_t nextHeap = 1;

    //==================================================================================================================
    static uint64_t alignUp(uint64_t value, uint64_t alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }

    //==================================================================================================================
    // The resources currently placed in `heap`, which must be live.
    size_t placementCount(lmx::render::TransientHeapId heap) const {
        const Heap& entry = heaps.at(heap.value);
        return entry.textures.size() + entry.buffers.size();
    }

    //==================================================================================================================
    // A placement past the end of its heap breaks the interface's contract, so the fake aborts
    // rather than letting a case pass over it.
    void requireInHeap(lmx::render::TransientHeapId heap, uint64_t offset,
                       lmx::render::TransientFootprint footprint) const {
        if (offset + footprint.size > heaps.at(heap.value).bytes) {
            std::abort();
        }
    }

    //==================================================================================================================
    uint64_t frameNumber() const override { return frame; }

    //==================================================================================================================
    lmx::render::TransientFootprint
    footprint(const lmx::render::TransientTextureDesc& desc) const override {
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
    lmx::render::TransientFootprint
    footprint(const lmx::render::TransientBufferDesc& desc) const override {
        return {.size = alignUp(desc.size, kBufferAlignment), .alignment = kBufferAlignment};
    }

    //==================================================================================================================
    lmx::Result<lmx::render::TransientHeapId> createHeap(uint64_t bytes,
                                                         std::string_view label) override {
        const uint32_t id = nextHeap++;
        heaps[id] = Heap{.bytes = bytes, .label = std::string(label)};
        return lmx::render::TransientHeapId{id};
    }

    //==================================================================================================================
    // Destroying an unknown heap, or one that still holds placements, breaks the interface's
    // contract, so the fake aborts rather than letting a case pass over it.
    void destroyHeap(lmx::render::TransientHeapId heap) override {
        const auto found = heaps.find(heap.value);
        if (found == heaps.end() || !found->second.textures.empty() ||
            !found->second.buffers.empty()) {
            std::abort();
        }
        heaps.erase(found);
        destroyedHeaps.push_back(heap.value);
    }

    //==================================================================================================================
    lmx::Result<rojoRHI::Texture*> placeTexture(lmx::render::TransientHeapId heap, uint64_t offset,
                                                const lmx::render::TransientTextureDesc& desc,
                                                std::string_view label) override {
        requireInHeap(heap, offset, footprint(desc));
        const uint32_t layers = desc.kind == rojoRHI::TextureKind::Cube ? 6 : 1;
        auto& textures = heaps.at(heap.value).textures;
        textures.push_back(std::make_unique<FakeTexture>(
            desc.width, desc.height, std::string(label), desc.mipLevels, layers));
        return textures.back().get();
    }

    //==================================================================================================================
    lmx::Result<rojoRHI::Buffer*> placeBuffer(lmx::render::TransientHeapId heap, uint64_t offset,
                                              const lmx::render::TransientBufferDesc& desc,
                                              std::string_view label) override {
        requireInHeap(heap, offset, footprint(desc));
        auto& buffers = heaps.at(heap.value).buffers;
        buffers.push_back(std::make_unique<FakeBuffer>(desc.size, std::string(label)));
        return buffers.back().get();
    }

    //==================================================================================================================
    void releasePlaced(lmx::render::TransientHeapId heap) override {
        Heap& entry = heaps.at(heap.value);
        entry.textures.clear();
        entry.buffers.clear();
    }
};

} // namespace
