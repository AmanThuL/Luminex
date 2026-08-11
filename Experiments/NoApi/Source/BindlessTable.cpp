//----------------------------------------------------------------------------------------------------------------------
/// @file BindlessTable.cpp
/// @brief Implements the single shader-visible table of texture and sampler resource IDs.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include <cstring>
#include <format>

namespace lmx::noapi {
namespace {

//======================================================================================================================
// Writes one resource ID into the table's memory. The table is plain GPU memory, so this is a
// store rather than a descriptor-heap API call -- which is the whole point of the model's design.
void storeSlot(BindlessTable* table, uint32_t slot, MTL::ResourceID id) {
    auto* slots = static_cast<MTL::ResourceID*>(table->storage->contents());
    slots[slot] = id;
}

//======================================================================================================================
// Whether a view selects the texture whole and keeps its format, in which case the texture's own
// resource ID is what the slot holds and no view object is created.
bool isWholeTexture(const TextureDesc& desc, const TextureViewDesc& view) {
    const bool wholeMips = view.baseMipLevel == 0 &&
                           (view.mipCount == kAllMipLevels || view.mipCount == desc.mipCount);
    const bool wholeLayers = view.baseArrayLayer == 0 && (view.arrayLayers == kAllArrayLayers ||
                                                          view.arrayLayers == desc.arrayLayers);
    return wholeMips && wholeLayers &&
           (view.format == Format::Undefined || view.format == desc.format);
}

} // namespace

//======================================================================================================================
Result<BindlessTable*> createBindlessTable(Device* device, const BindlessTableDesc& desc) {
    LMX_ASSERT(device != nullptr, "createBindlessTable: device must not be null");
    LMX_ASSERT(device->table == nullptr,
               "createBindlessTable: this device already owns a bindless table");
    LMX_ASSERT(desc.slotCount > 0, "createBindlessTable: BindlessTableDesc.slotCount must be "
                                   "non-zero");
    LMX_ASSERT(!desc.label.empty(), "createBindlessTable: BindlessTableDesc.label must not be "
                                    "empty");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    if (desc.slotCount > device->caps.maxBindlessSlots) {
        return fail(ErrorCode::ResourceCreationFailed,
                    "createBindlessTable: " + std::to_string(desc.slotCount) +
                        " slots exceeds the device's limit of " +
                        std::to_string(device->caps.maxBindlessSlots));
    }

    auto table = std::make_unique<BindlessTable>();
    const uint64_t bytes = uint64_t{desc.slotCount} * kBindlessSlotStride;
    // The table is CPU-written and GPU-read every frame, so it lives in shared memory like any
    // other allocation the model would make -- it is not a driver-owned heap.
    table->storage = NS::TransferPtr(device->mtl->newBuffer(
        bytes, MTL::ResourceStorageModeShared | MTL::ResourceHazardTrackingModeUntracked));
    if (!table->storage) {
        return fail(ErrorCode::ResourceCreationFailed, "createBindlessTable: failed to allocate " +
                                                           std::to_string(bytes) +
                                                           " bytes of table storage");
    }
    table->storage->setLabel(makeString(desc.label).get());
    std::memset(table->storage->contents(), 0, bytes);
    table->slotCount = desc.slotCount;
    table->slots.resize(desc.slotCount);

    addResidency(device, table->storage.get(), table->storage->allocatedSize());

    device->table = table.get();
    return table.release();
}

//======================================================================================================================
void destroyBindlessTable(Device* device, BindlessTable* table) {
    LMX_ASSERT(device != nullptr, "destroyBindlessTable: device must not be null");
    LMX_ASSERT(table != nullptr, "destroyBindlessTable: table must not be null");
    LMX_ASSERT(device->table == table, "destroyBindlessTable: this table belongs to another "
                                       "device");
    assertNoWorkInFlight(device, "destroyBindlessTable");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    removeResidency(device, table->storage.get(), table->storage->allocatedSize());
    device->table = nullptr;
    delete table;
}

//======================================================================================================================
GpuAddress bindlessTableAddress(const BindlessTable* table) {
    LMX_ASSERT(table != nullptr, "bindlessTableAddress: table must not be null");
    return table->storage->gpuAddress();
}

//======================================================================================================================
uint32_t bindlessTableSlotCount(const BindlessTable* table) {
    LMX_ASSERT(table != nullptr, "bindlessTableSlotCount: table must not be null");
    return table->slotCount;
}

//======================================================================================================================
TextureHandle writeTextureSlot(BindlessTable* table, uint32_t slot, const Texture* texture,
                               const TextureViewDesc& view) {
    LMX_ASSERT(table != nullptr, "writeTextureSlot: table must not be null");
    LMX_ASSERT(texture != nullptr, "writeTextureSlot: texture must not be null");
    LMX_ASSERT(slot < table->slotCount,
               std::format("writeTextureSlot: slot {} is outside a {}-slot table", slot,
                           table->slotCount));
    const TextureDesc& desc = texture->desc;
    LMX_ASSERT(view.baseMipLevel < desc.mipCount,
               std::format("writeTextureSlot: view base mip {} exceeds the {} mip level(s) of '{}'",
                           view.baseMipLevel, desc.mipCount, desc.label));
    const uint32_t mipCount =
        view.mipCount == kAllMipLevels ? desc.mipCount - view.baseMipLevel : view.mipCount;
    LMX_ASSERT(mipCount >= 1 && view.baseMipLevel + mipCount <= desc.mipCount,
               std::format("writeTextureSlot: view mip range [{}, {}) exceeds the {} mip level(s) "
                           "of '{}'",
                           view.baseMipLevel, view.baseMipLevel + mipCount, desc.mipCount,
                           desc.label));
    LMX_ASSERT(view.baseArrayLayer < desc.arrayLayers,
               std::format("writeTextureSlot: view base layer {} exceeds the {} layer(s) of '{}'",
                           view.baseArrayLayer, desc.arrayLayers, desc.label));
    const uint32_t layerCount = view.arrayLayers == kAllArrayLayers
                                    ? desc.arrayLayers - view.baseArrayLayer
                                    : view.arrayLayers;
    LMX_ASSERT(layerCount >= 1 && view.baseArrayLayer + layerCount <= desc.arrayLayers,
               std::format("writeTextureSlot: view layer range [{}, {}) exceeds the {} layer(s) of "
                           "'{}'",
                           view.baseArrayLayer, view.baseArrayLayer + layerCount, desc.arrayLayers,
                           desc.label));
    LMX_ASSERT(!view.storage || hasUsage(desc.usage, TextureUsage::Storage),
               std::format("writeTextureSlot: '{}' was created without TextureUsage::Storage, so "
                           "it cannot fill a storage slot",
                           desc.label));
    LMX_ASSERT(view.storage || hasUsage(desc.usage, TextureUsage::Sampled),
               std::format("writeTextureSlot: '{}' was created without TextureUsage::Sampled, so "
                           "it cannot fill a sampled slot",
                           desc.label));
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    BindlessSlot& state = table->slots[slot];
    state.viewOwner.reset();
    if (isWholeTexture(desc, view)) {
        storeSlot(table, slot, texture->handle->gpuResourceID());
    } else {
        const MTL::PixelFormat format =
            view.format == Format::Undefined ? texture->handle->pixelFormat() : toMTL(view.format);
        state.viewOwner = NS::TransferPtr(texture->handle->newTextureView(
            format, texture->handle->textureType(), NS::Range::Make(view.baseMipLevel, mipCount),
            NS::Range::Make(view.baseArrayLayer, layerCount)));
        LMX_ASSERT(state.viewOwner,
                   std::format("writeTextureSlot: the device refused a view of '{}'", desc.label));
        state.viewOwner->setLabel(
            makeString(std::string(desc.label) + ".view.slot" + std::to_string(slot)).get());
        storeSlot(table, slot, state.viewOwner->gpuResourceID());
    }

    state.texture = texture;
    state.sampler = nullptr;
    state.generation += 1;
    table->writeCalls += 1;
    table->writeBytes += kBindlessSlotStride;
    return {.slot = slot, .generation = state.generation};
}

//======================================================================================================================
SamplerHandle writeSamplerSlot(BindlessTable* table, uint32_t slot, const Sampler* sampler) {
    LMX_ASSERT(table != nullptr, "writeSamplerSlot: table must not be null");
    LMX_ASSERT(sampler != nullptr, "writeSamplerSlot: sampler must not be null");
    LMX_ASSERT(slot < table->slotCount,
               std::format("writeSamplerSlot: slot {} is outside a {}-slot table", slot,
                           table->slotCount));

    BindlessSlot& state = table->slots[slot];
    state.viewOwner.reset();
    state.texture = nullptr;
    state.sampler = sampler;
    state.generation += 1;
    storeSlot(table, slot, sampler->handle->gpuResourceID());
    table->writeCalls += 1;
    table->writeBytes += kBindlessSlotStride;
    return {.slot = slot, .generation = state.generation};
}

//======================================================================================================================
void clearBindlessSlot(BindlessTable* table, uint32_t slot) {
    LMX_ASSERT(table != nullptr, "clearBindlessSlot: table must not be null");
    LMX_ASSERT(slot < table->slotCount,
               std::format("clearBindlessSlot: slot {} is outside a {}-slot table", slot,
                           table->slotCount));

    BindlessSlot& state = table->slots[slot];
    state.viewOwner.reset();
    state.texture = nullptr;
    state.sampler = nullptr;
    state.generation += 1;
    // Zeroing is only a CPU-side courtesy: a shader reading a cleared slot is undefined either
    // way, and the generation bump is what makes the misuse detectable on this side.
    storeSlot(table, slot, MTL::ResourceID{});
}

//======================================================================================================================
BindlessTableStats bindlessTableStats(const BindlessTable* table) {
    LMX_ASSERT(table != nullptr, "bindlessTableStats: table must not be null");
    return {.writeCalls = table->writeCalls, .writeBytes = table->writeBytes};
}

} // namespace lmx::noapi
