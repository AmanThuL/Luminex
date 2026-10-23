//----------------------------------------------------------------------------------------------------------------------
/// @file RhiTransientBackend.cpp
/// @brief Implements transient sizing, heaps and placement over an RHI device.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/RhiTransientBackend.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/GpuError.h"

#include <algorithm>
#include <format>
#include <utility>

namespace lmx::render {

namespace {

//======================================================================================================================
rojoRHI::TextureDesc textureDescOf(const TransientTextureDesc& desc, std::string_view label) {
    return {.width = desc.width,
            .height = desc.height,
            .format = desc.format,
            .kind = desc.kind,
            .mipLevels = desc.mipLevels,
            .renderTarget = desc.renderTarget,
            .sampled = desc.sampled,
            .storageRead = desc.storageRead,
            .storageWrite = desc.storageWrite,
            .cpuReadback = false,
            .label = label};
}

//======================================================================================================================
rojoRHI::BufferDesc bufferDescOf(const TransientBufferDesc& desc, std::string_view label) {
    return {.size = desc.size,
            .storageRead = desc.storageRead,
            .storageWrite = desc.storageWrite,
            .cpuReadback = false,
            .label = label};
}

} // namespace

//======================================================================================================================
RhiTransientBackend::~RhiTransientBackend() {
    // Each entry destroys its resources before its heap, so clearing the list keeps that order.
    m_heaps.clear();
}

//======================================================================================================================
uint64_t RhiTransientBackend::frameNumber() const {
    return m_device.frameNumber();
}

//======================================================================================================================
TransientFootprint RhiTransientBackend::footprint(const TransientTextureDesc& desc) const {
    const rojoRHI::SizeAlign sizeAlign = m_device.textureSizeAlign(textureDescOf(desc, {}));
    return {.size = sizeAlign.size, .alignment = sizeAlign.alignment};
}

//======================================================================================================================
TransientFootprint RhiTransientBackend::footprint(const TransientBufferDesc& desc) const {
    const rojoRHI::SizeAlign sizeAlign = m_device.bufferSizeAlign(bufferDescOf(desc, {}));
    return {.size = sizeAlign.size, .alignment = sizeAlign.alignment};
}

//======================================================================================================================
lmx::Result<TransientHeapId> RhiTransientBackend::createHeap(uint64_t bytes,
                                                             std::string_view label) {
    auto heap = m_device.createHeap({.size = bytes, .label = label});
    if (!heap) {
        return std::unexpected(engine::gpuError(heap.error()));
    }
    const TransientHeapId id{m_nextHeap++};
    m_heaps.push_back({.id = id, .heap = std::move(*heap)});
    return id;
}

//======================================================================================================================
void RhiTransientBackend::destroyHeap(TransientHeapId heap) {
    const auto found = std::ranges::find(m_heaps, heap, &HeapEntry::id);
    LMX_ASSERT(
        found != m_heaps.end(),
        std::format("RhiTransientBackend::destroyHeap: heap {} is not one this backend holds",
                    heap.value));
    LMX_ASSERT(found->textures.empty() && found->buffers.empty(),
               std::format("RhiTransientBackend::destroyHeap: heap {} still holds placed resources "
                           "-- releasePlaced() it first",
                           heap.value));
    m_heaps.erase(found);
}

//======================================================================================================================
lmx::Result<rojoRHI::Texture*> RhiTransientBackend::placeTexture(TransientHeapId heap,
                                                                 uint64_t offset,
                                                                 const TransientTextureDesc& desc,
                                                                 std::string_view label) {
    HeapEntry& entry = entryOf(heap);
    auto texture = m_device.createPlacedTexture(*entry.heap, offset, textureDescOf(desc, label));
    if (!texture) {
        return std::unexpected(engine::gpuError(texture.error()));
    }
    entry.textures.push_back(std::move(*texture));
    return entry.textures.back().get();
}

//======================================================================================================================
lmx::Result<rojoRHI::Buffer*> RhiTransientBackend::placeBuffer(TransientHeapId heap,
                                                               uint64_t offset,
                                                               const TransientBufferDesc& desc,
                                                               std::string_view label) {
    HeapEntry& entry = entryOf(heap);
    auto buffer = m_device.createPlacedBuffer(*entry.heap, offset, bufferDescOf(desc, label));
    if (!buffer) {
        return std::unexpected(engine::gpuError(buffer.error()));
    }
    entry.buffers.push_back(std::move(*buffer));
    return entry.buffers.back().get();
}

//======================================================================================================================
void RhiTransientBackend::releasePlaced(TransientHeapId heap) {
    HeapEntry& entry = entryOf(heap);
    entry.textures.clear();
    entry.buffers.clear();
}

//======================================================================================================================
RhiTransientBackend::HeapEntry& RhiTransientBackend::entryOf(TransientHeapId heap) {
    const auto found = std::ranges::find(m_heaps, heap, &HeapEntry::id);
    LMX_ASSERT(
        found != m_heaps.end(),
        std::format("RhiTransientBackend: heap {} is not one this backend holds", heap.value));
    return *found;
}

} // namespace lmx::render
