//----------------------------------------------------------------------------------------------------------------------
/// @file TransientPool.cpp
/// @brief Implements complete-layout placement reuse under context frame pacing.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/TransientPool.h"
#include "Core/Diagnostics/Assert.h"
#include "Render/Graph/RhiTransientBackend.h"
#include <format>
#include <utility>

namespace lmx::render {

//======================================================================================================================
TransientPool::TransientPool(engine::GpuContext& gpu)
    : m_ownedBackend(std::make_unique<RhiTransientBackend>(gpu)), m_backend(*m_ownedBackend) {}

//======================================================================================================================
TransientPool::~TransientPool() {
    for (auto& slot : m_slots) {
        if (slot.heap) {
            m_backend.releasePlaced(*slot.heap);
            m_backend.destroyHeap(*slot.heap);
        }
    }
    for (const auto& old : m_retiring) {
        m_backend.destroyHeap(old.heap);
    }
}

//======================================================================================================================
void TransientPool::beginFrame() {
    const uint64_t frame = m_backend.frameNumber();
    LMX_ASSERT(frame > m_frame,
               "TransientPool::beginFrame: the device is still on the frame this pool last opened "
               "-- call it once per Device::beginFrame(), after that call");
    m_frame = frame;
    openSlot().reserved = false;
    std::vector<Retiring> kept;
    for (const auto& old : m_retiring) {
        if (frame >= old.releaseAtFrame) {
            m_backend.destroyHeap(old.heap);
        } else {
            kept.push_back(old);
        }
    }
    m_retiring = std::move(kept);
}

//======================================================================================================================
lmx::Result<void> TransientPool::reserve(const TransientLayout& layout) {
    LMX_ASSERT(m_frame > 0, "TransientPool::reserve: no frame is open -- call beginFrame() first");
    Slot& slot = openSlot();
    if (slot.reserved) {
        return std::unexpected(
            lmx::Error{lmx::ErrorCode::InvalidArgument,
                       "TransientPool::reserve: this frame has already reserved its transient "
                       "memory -- one frame reserves once, so a second graph declaring transients "
                       "in the same frame needs a frame of its own"});
    }
    slot.reserved = true;
    slot.cursor = 0;
    const bool hit = slot.complete && slot.layout == layout;
    slot.complete = false;
    if (!hit) {
        if (slot.heap) {
            m_backend.releasePlaced(*slot.heap);
        }
        slot.placed.clear();
    }
    if (slot.bytes != layout.bytes) {
        if (slot.heap) {
            m_retiring.push_back({*slot.heap, slot.lastFrame + kTransientFrameSlots});
            slot.heap.reset();
        }
        slot.bytes = 0;
        if (layout.bytes != 0) {
            auto heap =
                m_backend.createHeap(layout.bytes, std::format("lmx.render.transientHeap{}",
                                                               m_frame % kTransientFrameSlots));
            if (!heap) {
                return std::unexpected(heap.error());
            }
            slot.heap = *heap;
            slot.bytes = layout.bytes;
        }
    }
    slot.layout = layout;
    slot.lastFrame = m_frame;
    return {};
}

//======================================================================================================================
const TransientPlacementDesc& TransientPool::nextPlacement(uint64_t offset,
                                                           std::string_view label) const {
    const Slot& slot = openSlot();
    LMX_ASSERT(slot.reserved && slot.heap && slot.cursor < slot.layout.placements.size(),
               "placed resource has no corresponding reserved layout entry");
    const auto& entry = slot.layout.placements[slot.cursor];
    LMX_ASSERT(entry.offset == offset && entry.label == label && offset <= slot.bytes &&
                   entry.footprint.size <= slot.bytes - offset,
               "placement does not match the reserved layout or exceeds its heap");
    return entry;
}

//======================================================================================================================
lmx::Result<engine::TextureRef> TransientPool::placeTexture(const TransientTextureDesc& desc,
                                                            uint64_t offset,
                                                            std::string_view label) {
    LMX_ASSERT(m_frame > 0,
               "TransientPool::placeTexture: no frame is open -- call beginFrame() first");
    Slot& slot = openSlot();
    LMX_ASSERT(slot.heap.has_value(),
               "TransientPool::placeTexture: the open slot holds no heap -- reserve() the frame's "
               "transient bytes first");
    const auto& planned = nextPlacement(offset, label);
    const auto* textureDesc = std::get_if<TransientTextureDesc>(&planned.desc);
    LMX_ASSERT(textureDesc && *textureDesc == desc,
               "texture descriptor differs from its reserved layout");
    if (slot.cursor == slot.placed.size()) {
        auto placed = m_backend.placeTexture(*slot.heap, offset, desc, label);
        if (!placed) {
            return std::unexpected(placed.error());
        }
        slot.placed.emplace_back(*placed);
    }
    return std::get<engine::TextureRef>(slot.placed[slot.cursor++]);
}

//======================================================================================================================
lmx::Result<rojoRHI::Buffer> TransientPool::placeBuffer(const TransientBufferDesc& desc,
                                                        uint64_t offset, std::string_view label,
                                                        bool indirectArgs) {
    LMX_ASSERT(m_frame > 0,
               "TransientPool::placeBuffer: no frame is open -- call beginFrame() first");
    Slot& slot = openSlot();
    LMX_ASSERT(slot.heap.has_value(),
               "TransientPool::placeBuffer: the open slot holds no heap -- reserve() the frame's "
               "transient bytes first");
    const auto& planned = nextPlacement(offset, label);
    const auto* bufferDesc = std::get_if<TransientBufferPlacementDesc>(&planned.desc);
    LMX_ASSERT(bufferDesc && bufferDesc->desc == desc && bufferDesc->indirectArgs == indirectArgs,
               "buffer descriptor differs from its reserved layout");
    if (slot.cursor == slot.placed.size()) {
        auto placed = m_backend.placeBuffer(*slot.heap, offset, desc, label, indirectArgs);
        if (!placed) {
            return std::unexpected(placed.error());
        }
        slot.placed.emplace_back(*placed);
    }
    return std::get<rojoRHI::Buffer>(slot.placed[slot.cursor++]);
}

//======================================================================================================================
void TransientPool::finishPlacement() {
    Slot& slot = openSlot();
    LMX_ASSERT(slot.reserved && slot.cursor == slot.layout.placements.size(),
               "the frame did not consume its complete reserved transient layout");
    slot.complete = true;
}

//======================================================================================================================
rojoRHI::ViewIndex TransientPool::storageView(rojoRHI::Texture texture, uint32_t mip) const {
    LMX_ASSERT(openSlot().heap.has_value(), "no heap for placed storage-view lookup");
    return m_backend.storageView(*openSlot().heap, texture, mip);
}

//======================================================================================================================
rojoRHI::Addr TransientPool::address(rojoRHI::Buffer buffer) const {
    LMX_ASSERT(openSlot().heap.has_value(), "no heap for placed address lookup");
    return m_backend.address(*openSlot().heap, buffer);
}

//======================================================================================================================
size_t TransientPool::liveGenerationCount() const {
    size_t count = m_retiring.size();
    for (const auto& slot : m_slots) {
        count += slot.heap.has_value() ? 1 : 0;
    }
    return count;
}

//======================================================================================================================
uint64_t TransientPool::heapBytes() const {
    return m_frame == 0 ? 0 : openSlot().bytes;
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
