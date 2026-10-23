//----------------------------------------------------------------------------------------------------------------------
/// @file TransientPool.cpp
/// @brief Implements per-frame-slot placement heaps and their generation lifetime.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/TransientPool.h"

#include "Core/Diagnostics/Assert.h"
#include "Render/Graph/RhiTransientBackend.h"

#include <format>
#include <memory>
#include <utility>
#include <vector>

namespace lmx::render {

//======================================================================================================================
TransientPool::TransientPool(rojoRHI::Device& device)
    : m_ownedBackend(std::make_unique<RhiTransientBackend>(device)), m_backend(*m_ownedBackend) {}

//======================================================================================================================
TransientPool::~TransientPool() {
    for (Slot& slot : m_slots) {
        if (slot.heap) {
            m_backend.releasePlaced(*slot.heap);
            m_backend.destroyHeap(*slot.heap);
        }
    }
    for (const Retiring& retiring : m_retiring) {
        m_backend.destroyHeap(retiring.heap);
    }
}

//======================================================================================================================
void TransientPool::beginFrame() {
    const uint64_t frame = m_backend.frameNumber();
    LMX_ASSERT(frame > m_frame,
               "TransientPool::beginFrame: the device is still on the frame this pool last opened "
               "-- call it once per Device::beginFrame(), after that call");
    m_frame = frame;

    // The slot's previous occupant retired during Device::beginFrame()'s pacing wait, so its
    // resources are unreferenced and the heap under them is free to be re-placed into.
    Slot& slot = openSlot();
    slot.reserved = false;
    if (slot.heap) {
        m_backend.releasePlaced(*slot.heap);
    }

    std::vector<Retiring> stillRetiring;
    for (const Retiring& retiring : m_retiring) {
        if (frame >= retiring.releaseAtFrame) {
            m_backend.destroyHeap(retiring.heap);
        } else {
            stillRetiring.push_back(retiring);
        }
    }
    m_retiring = std::move(stillRetiring);
}

//======================================================================================================================
lmx::Result<void> TransientPool::reserve(uint64_t bytes) {
    LMX_ASSERT(m_frame > 0, "TransientPool::reserve: no frame is open -- call beginFrame() first");
    Slot& slot = openSlot();
    // Refused before the footprint is even looked at. A second reservation asking for the *same*
    // bytes is the dangerous one: nothing about the heap changes, so it would quietly succeed and
    // the two callers would place at the same offsets in memory the first one's resources are
    // already live in -- aliasing across two frames' worth of work with no barrier between them.
    if (slot.reserved) {
        return std::unexpected(
            lmx::Error{lmx::ErrorCode::InvalidArgument,
                       "TransientPool::reserve: this frame has already reserved its transient "
                       "memory -- one frame reserves once, so a second graph declaring transients "
                       "in the same frame needs a frame of its own"});
    }
    slot.reserved = true;

    if (slot.bytes == bytes) {
        slot.lastFrame = m_frame;
        return {};
    }

    // The outgoing generation may still be under an in-flight frame's placed resources, so it is
    // dated rather than dropped. A generation whose slot has simply come round again is already
    // retired by the pacing, and this bound agrees with that; it is stated so the rule holds
    // whatever a future caller does with the slots.
    if (slot.heap) {
        m_retiring.push_back(
            {.heap = *slot.heap, .releaseAtFrame = slot.lastFrame + kTransientFrameSlots});
        slot.heap.reset();
    }
    slot.bytes = 0;
    slot.lastFrame = m_frame;
    if (bytes == 0) {
        return {};
    }

    const uint32_t index = static_cast<uint32_t>(m_frame % kTransientFrameSlots);
    const std::string label = std::format("lmx.render.transientHeap{}", index);
    const lmx::Result<TransientHeapId> heap = m_backend.createHeap(bytes, label);
    if (!heap) {
        return std::unexpected(heap.error());
    }
    slot.heap = *heap;
    slot.bytes = bytes;
    return {};
}

//======================================================================================================================
lmx::Result<rojoRHI::Texture*> TransientPool::placeTexture(const TransientTextureDesc& desc,
                                                           uint64_t offset,
                                                           std::string_view label) {
    LMX_ASSERT(m_frame > 0,
               "TransientPool::placeTexture: no frame is open -- call beginFrame() first");
    Slot& slot = openSlot();
    LMX_ASSERT(slot.heap.has_value(),
               "TransientPool::placeTexture: the open slot holds no heap -- reserve() the frame's "
               "transient bytes first");
    return m_backend.placeTexture(*slot.heap, offset, desc, label);
}

//======================================================================================================================
lmx::Result<rojoRHI::Buffer*> TransientPool::placeBuffer(const TransientBufferDesc& desc,
                                                         uint64_t offset, std::string_view label) {
    LMX_ASSERT(m_frame > 0,
               "TransientPool::placeBuffer: no frame is open -- call beginFrame() first");
    Slot& slot = openSlot();
    LMX_ASSERT(slot.heap.has_value(),
               "TransientPool::placeBuffer: the open slot holds no heap -- reserve() the frame's "
               "transient bytes first");
    return m_backend.placeBuffer(*slot.heap, offset, desc, label);
}

//======================================================================================================================
size_t TransientPool::liveGenerationCount() const {
    size_t live = m_retiring.size();
    for (const Slot& slot : m_slots) {
        live += slot.heap.has_value() ? 1 : 0;
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
