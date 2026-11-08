#pragma once

#include "Render/Graph/TransientBackend.h"
#include "Render/Graph/TransientResourceDesc.h"
#include "Support/GraphTestSupport.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <ranges>
#include <string>
#include <vector>

namespace {

// Deterministic allocation metadata, with no native resources or device behind it.
struct FakeTransientBackend final : lmx::render::TransientBackend {
    static constexpr uint64_t kTextureAlignment = 16384;
    static constexpr uint64_t kBufferAlignment = 256;
    static constexpr uint64_t kBytesPerTexel = 4;

    struct TexturePlacement {
        lmx::engine::TextureRef ref;
        std::vector<rojoRHI::ViewIndex> storageViews;
    };
    struct BufferPlacement {
        rojoRHI::Buffer buffer;
        rojoRHI::Addr address;
    };
    struct Heap {
        uint64_t bytes = 0;
        std::string label;
        std::vector<TexturePlacement> textures;
        std::vector<BufferPlacement> buffers;
    };

    uint64_t frame = 0;
    std::map<uint32_t, Heap> heaps;
    std::vector<uint32_t> destroyedHeaps;
    uint32_t nextHeap = 1;
    mutable uint32_t textureQueries = 0;
    mutable uint32_t bufferQueries = 0;
    mutable uint32_t indirectQueries = 0;
    bool permitFootprintQueries = true;

    //==================================================================================================================
    static uint64_t alignUp(uint64_t value, uint64_t alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }

    //==================================================================================================================
    size_t placementCount(lmx::render::TransientHeapId heap) const {
        const Heap& entry = heaps.at(heap.value);
        return entry.textures.size() + entry.buffers.size();
    }

    //==================================================================================================================
    void requireInHeap(lmx::render::TransientHeapId heap, uint64_t offset,
                       lmx::render::TransientFootprint footprint) const {
        if (offset + footprint.size > heaps.at(heap.value).bytes) {
            std::abort();
        }
    }

    //==================================================================================================================
    uint64_t frameNumber() const override { return frame; }

    //==================================================================================================================
    static lmx::render::TransientFootprint
    textureFootprint(const lmx::render::TransientTextureDesc& desc) {
        const uint32_t faces = desc.kind == rojoRHI::TextureType::Cube ? 6 : 1;
        uint64_t texels = 0;
        for (uint32_t level = 0; level < desc.mipLevels; ++level) {
            texels +=
                uint64_t{std::max(1u, desc.width >> level)} * std::max(1u, desc.height >> level);
        }
        return {.size = alignUp(texels * faces * kBytesPerTexel, kTextureAlignment),
                .alignment = kTextureAlignment};
    }

    //==================================================================================================================
    static lmx::render::TransientFootprint
    bufferFootprint(const lmx::render::TransientBufferDesc& desc) {
        return {.size = alignUp(desc.size, kBufferAlignment), .alignment = kBufferAlignment};
    }

    //==================================================================================================================
    lmx::render::TransientFootprint
    footprint(const lmx::render::TransientTextureDesc& desc) const override {
        if (!permitFootprintQueries)
            std::abort();
        ++textureQueries;
        return textureFootprint(desc);
    }

    //==================================================================================================================
    lmx::render::TransientFootprint footprint(const lmx::render::TransientBufferDesc& desc,
                                              bool indirectArgs = false) const override {
        if (!permitFootprintQueries)
            std::abort();
        ++bufferQueries;
        if (indirectArgs)
            ++indirectQueries;
        return bufferFootprint(desc);
    }

    //==================================================================================================================
    lmx::Result<lmx::render::TransientHeapId> createHeap(uint64_t bytes,
                                                         std::string_view label) override {
        const uint32_t id = nextHeap++;
        heaps[id] = Heap{.bytes = bytes, .label = std::string(label)};
        return lmx::render::TransientHeapId{id};
    }

    //==================================================================================================================
    void destroyHeap(lmx::render::TransientHeapId heap) override {
        const auto found = heaps.find(heap.value);
        if (found == heaps.end() || !found->second.textures.empty() ||
            !found->second.buffers.empty())
            std::abort();
        heaps.erase(found);
        destroyedHeaps.push_back(heap.value);
    }

    //==================================================================================================================
    lmx::Result<lmx::engine::TextureRef> placeTexture(lmx::render::TransientHeapId heap,
                                                      uint64_t offset,
                                                      const lmx::render::TransientTextureDesc& desc,
                                                      std::string_view) override {
        requireInHeap(heap, offset, textureFootprint(desc));
        auto ref = testTextureRef(nextSyntheticIdentity(), desc.format, desc.width, desc.height,
                                  desc.mipLevels, desc.kind == rojoRHI::TextureType::Cube ? 6 : 1);
        if (desc.sampled)
            ref.view = rojoRHI::ViewIndex{static_cast<uint32_t>(nextSyntheticIdentity())};
        TexturePlacement placed{ref, {}};
        if (desc.storageRead || desc.storageWrite) {
            for (uint32_t mip = 0; mip < desc.mipLevels; ++mip) {
                placed.storageViews.push_back(
                    rojoRHI::ViewIndex{static_cast<uint32_t>(nextSyntheticIdentity())});
            }
        }
        heaps.at(heap.value).textures.push_back(std::move(placed));
        return ref;
    }

    //==================================================================================================================
    lmx::Result<rojoRHI::Buffer> placeBuffer(lmx::render::TransientHeapId heap, uint64_t offset,
                                             const lmx::render::TransientBufferDesc& desc,
                                             std::string_view,
                                             [[maybe_unused]] bool indirectArgs = false) override {
        requireInHeap(heap, offset, bufferFootprint(desc));
        const auto buffer = testBuffer(nextSyntheticIdentity());
        heaps.at(heap.value).buffers.push_back({buffer, {0, static_cast<uint32_t>(buffer.bits)}});
        return buffer;
    }

    //==================================================================================================================
    rojoRHI::ViewIndex storageView(lmx::render::TransientHeapId heap, rojoRHI::Texture texture,
                                   uint32_t mip) const override {
        const auto& records = heaps.at(heap.value).textures;
        const auto found = std::ranges::find_if(
            records, [texture](const auto& record) { return record.ref.texture == texture; });
        if (found == records.end() || mip >= found->storageViews.size())
            std::abort();
        return found->storageViews[mip];
    }

    //==================================================================================================================
    rojoRHI::Addr address(lmx::render::TransientHeapId heap,
                          rojoRHI::Buffer buffer) const override {
        const auto& records = heaps.at(heap.value).buffers;
        const auto found = std::ranges::find_if(
            records, [buffer](const auto& record) { return record.buffer == buffer; });
        if (found == records.end())
            std::abort();
        return found->address;
    }

    //==================================================================================================================
    void releasePlaced(lmx::render::TransientHeapId heap) override {
        Heap& entry = heaps.at(heap.value);
        entry.textures.clear();
        entry.buffers.clear();
    }
};

} // namespace
