//----------------------------------------------------------------------------------------------------------------------
/// @file RhiTransientBackend.h
/// @brief Declares the transient backend that places the graph's transients on an RHI device.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Graph/TransientBackend.h"

#include <rojoRHI/RHI.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace lmx::render {

/// The TransientBackend over a rojoRHI::Device: sizes through textureSizeAlign and
/// bufferSizeAlign, creates heaps with createHeap, and places through createPlacedTexture and
/// createPlacedBuffer, translating their errors with engine::gpuError. A transient's descriptor
/// becomes the RHI's with no initial contents, no CPU readback, and the label the pool passes,
/// which is the graph resource's name.
///
/// The backend must not outlive its device.
class RhiTransientBackend final : public TransientBackend {
public:
    /// Binds the backend to the device its heaps and placed resources are created on. Nothing is
    /// allocated here.
    explicit RhiTransientBackend(rojoRHI::Device& device) : m_device(device) {}
    /// Releases every placed resource still held, then every heap.
    ~RhiTransientBackend() override;

    /// Returns the device's open frame number.
    uint64_t frameNumber() const override;
    /// Returns the device's textureSizeAlign for the converted descriptor.
    TransientFootprint footprint(const TransientTextureDesc& desc) const override;
    /// Returns the device's bufferSizeAlign for the converted descriptor.
    TransientFootprint footprint(const TransientBufferDesc& desc) const override;
    /// Creates an RHI heap of `bytes` labelled `label`.
    lmx::Result<TransientHeapId> createHeap(uint64_t bytes, std::string_view label) override;
    /// Destroys the RHI heap; its placed resources must already be released.
    void destroyHeap(TransientHeapId heap) override;
    /// Places an RHI texture in the heap and keeps it until the heap's next release.
    lmx::Result<rojoRHI::Texture*> placeTexture(TransientHeapId heap, uint64_t offset,
                                                const TransientTextureDesc& desc,
                                                std::string_view label) override;
    /// Places an RHI buffer in the heap and keeps it until the heap's next release.
    lmx::Result<rojoRHI::Buffer*> placeBuffer(TransientHeapId heap, uint64_t offset,
                                              const TransientBufferDesc& desc,
                                              std::string_view label) override;
    /// Destroys the RHI resources placed in the heap since its last release.
    void releasePlaced(TransientHeapId heap) override;

private:
    // One heap and the resources placed in it. The resources are declared after the heap so they
    // are destroyed first: a placed resource must not outlive the memory it sits in.
    struct HeapEntry {
        TransientHeapId id;
        std::unique_ptr<rojoRHI::Heap> heap;
        std::vector<std::unique_ptr<rojoRHI::Texture>> textures;
        std::vector<std::unique_ptr<rojoRHI::Buffer>> buffers;
    };

    HeapEntry& entryOf(TransientHeapId heap);

    rojoRHI::Device& m_device;
    // A pool holds at most a heap per frame slot plus the generations retiring, so a linear search
    // over this list is all a lookup needs.
    std::vector<HeapEntry> m_heaps;
    uint32_t m_nextHeap = 1;
};

} // namespace lmx::render
