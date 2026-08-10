//----------------------------------------------------------------------------------------------------------------------
/// @file TransientPool.cpp
/// @brief Implements per-frame-slot placement heaps and their generation lifetime.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/TransientPool.h"

#include "Core/Assert.h"

#include <algorithm>
#include <format>
#include <utility>

namespace lmx::render {

//======================================================================================================================
void TransientPool::beginFrame() {
    const uint64_t frame = m_device.frameNumber();
    LMX_ASSERT(frame > m_frame,
               "TransientPool::beginFrame: the device is still on the frame this pool last opened "
               "-- call it once per Device::beginFrame(), after that call");
    m_frame = frame;

    // The slot's previous occupant retired during Device::beginFrame()'s pacing wait, so its
    // resources are unreferenced and the heap under them is free to be re-placed into.
    Slot& slot = openSlot();
    slot.textures.clear();
    slot.buffers.clear();

    std::erase_if(m_retiring,
                  [frame](const Retiring& retiring) { return frame >= retiring.releaseAtFrame; });
}

//======================================================================================================================
rhi::Result<void> TransientPool::reserve(uint64_t bytes) {
    LMX_ASSERT(m_frame > 0, "TransientPool::reserve: no frame is open -- call beginFrame() first");
    Slot& slot = openSlot();
    if (slot.bytes == bytes) {
        slot.lastFrame = m_frame;
        return {};
    }

    LMX_ASSERT(slot.textures.empty() && slot.buffers.empty(),
               "TransientPool::reserve: this frame has already placed resources in the slot's "
               "heap -- one frame reserves its transient memory once, before placing anything");

    // The outgoing generation may still be under an in-flight frame's placed resources, so it is
    // dated rather than dropped. A generation whose slot has simply come round again is already
    // retired by the pacing, and this bound agrees with that; it is stated so the rule holds
    // whatever a future caller does with the slots.
    if (slot.heap) {
        m_retiring.push_back({.heap = std::move(slot.heap),
                              .releaseAtFrame = slot.lastFrame + kTransientFrameSlots});
    }
    slot.bytes = 0;
    slot.lastFrame = m_frame;
    if (bytes == 0) {
        return {};
    }

    const uint32_t index = static_cast<uint32_t>(m_frame % kTransientFrameSlots);
    const std::string label = std::format("lmx.render.transientHeap{}", index);
    auto heap = m_device.createHeap({.size = bytes, .label = label});
    if (!heap) {
        return std::unexpected(heap.error());
    }
    slot.heap = std::move(*heap);
    slot.bytes = bytes;
    return {};
}

//======================================================================================================================
rhi::Result<rhi::Texture*> TransientPool::placeTexture(const rhi::TextureDesc& desc,
                                                       uint64_t offset) {
    LMX_ASSERT(m_frame > 0,
               "TransientPool::placeTexture: no frame is open -- call beginFrame() first");
    Slot& slot = openSlot();
    LMX_ASSERT(slot.heap != nullptr,
               "TransientPool::placeTexture: the open slot holds no heap -- reserve() the frame's "
               "transient bytes first");

    auto texture = m_device.createPlacedTexture(*slot.heap, offset, desc);
    if (!texture) {
        return std::unexpected(texture.error());
    }
    slot.textures.push_back(std::move(*texture));
    return slot.textures.back().get();
}

//======================================================================================================================
rhi::Result<rhi::Buffer*> TransientPool::placeBuffer(const rhi::BufferDesc& desc, uint64_t offset) {
    LMX_ASSERT(m_frame > 0,
               "TransientPool::placeBuffer: no frame is open -- call beginFrame() first");
    Slot& slot = openSlot();
    LMX_ASSERT(slot.heap != nullptr,
               "TransientPool::placeBuffer: the open slot holds no heap -- reserve() the frame's "
               "transient bytes first");

    auto buffer = m_device.createPlacedBuffer(*slot.heap, offset, desc);
    if (!buffer) {
        return std::unexpected(buffer.error());
    }
    slot.buffers.push_back(std::move(*buffer));
    return slot.buffers.back().get();
}

//======================================================================================================================
size_t TransientPool::liveGenerationCount() const {
    size_t live = m_retiring.size();
    for (const Slot& slot : m_slots) {
        live += slot.heap ? 1 : 0;
    }
    return live;
}

//======================================================================================================================
uint64_t TransientPool::heapBytes() const {
    return m_frame > 0 ? openSlot().bytes : 0;
}

//======================================================================================================================
TransientPool::Slot& TransientPool::openSlot() {
    return m_slots[m_frame % kTransientFrameSlots];
}

//======================================================================================================================
const TransientPool::Slot& TransientPool::openSlot() const {
    return m_slots[m_frame % kTransientFrameSlots];
}

} // namespace lmx::render
