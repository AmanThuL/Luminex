//----------------------------------------------------------------------------------------------------------------------
/// @file RhiTransientBackend.h
/// @brief Declares placement and cached views over a GPU context.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Graph/TransientBackend.h"
#include <vector>

namespace lmx::engine {
/// Owns the device and pacing counter borrowed by real transient placement.
class GpuContext;
} // namespace lmx::engine
namespace lmx::render {
/// Owns native placed objects while borrowing the longer-lived GPU context.
class RhiTransientBackend final : public TransientBackend {
public:
    /// Allocates nothing; gpu must outlive this backend.
    explicit RhiTransientBackend(engine::GpuContext& gpu) : m_gpu(gpu) {}
    /// Releases views/resources before heaps.
    ~RhiTransientBackend() override;
    /// @inheritdoc
    uint64_t frameNumber() const override;
    /// @inheritdoc
    TransientFootprint footprint(const TransientTextureDesc&) const override;
    /// @inheritdoc
    TransientFootprint footprint(const TransientBufferDesc&,
                                 bool indirectArgs = false) const override;
    /// @inheritdoc
    lmx::Result<TransientHeapId> createHeap(uint64_t bytes, std::string_view label) override;
    /// @inheritdoc
    void destroyHeap(TransientHeapId heap) override;
    /// @inheritdoc
    lmx::Result<engine::TextureRef> placeTexture(TransientHeapId heap, uint64_t offset,
                                                 const TransientTextureDesc&,
                                                 std::string_view label) override;
    /// @inheritdoc
    lmx::Result<rojoRHI::Buffer> placeBuffer(TransientHeapId heap, uint64_t offset,
                                             const TransientBufferDesc&, std::string_view label,
                                             bool indirectArgs = false) override;
    /// @inheritdoc
    rojoRHI::ViewIndex storageView(TransientHeapId heap, rojoRHI::Texture texture,
                                   uint32_t mip) const override;
    /// @inheritdoc
    rojoRHI::Addr address(TransientHeapId heap, rojoRHI::Buffer buffer) const override;
    /// @inheritdoc
    void releasePlaced(TransientHeapId heap) override;

private:
    struct TextureEntry {
        engine::TextureRef ref;
        std::vector<rojoRHI::ViewIndex> storage;
    };
    struct BufferEntry {
        rojoRHI::Buffer buffer;
        rojoRHI::Addr address;
    };
    struct HeapEntry {
        TransientHeapId id;
        rojoRHI::Heap heap;
        std::vector<TextureEntry> textures;
        std::vector<BufferEntry> buffers;
    };
    HeapEntry& entryOf(TransientHeapId heap);
    const HeapEntry& entryOf(TransientHeapId heap) const;
    engine::GpuContext& m_gpu;
    std::vector<HeapEntry> m_heaps;
    uint32_t m_nextHeap = 1;
};
} // namespace lmx::render
