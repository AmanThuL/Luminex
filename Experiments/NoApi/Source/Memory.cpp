//----------------------------------------------------------------------------------------------------------------------
/// @file Memory.cpp
/// @brief Implements address-returning GPU memory allocation over Metal buffers and heaps.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include "Core/Align.h"

#include <algorithm>
#include <format>

namespace lmx::experimental::noapi {
namespace {

//======================================================================================================================
MTL::ResourceOptions optionsFor(MemoryKind kind) {
    // Everything is untracked: Metal 4 does not hazard-track across encoders anyway, and the model
    // makes the caller state every dependency through barriers.
    switch (kind) {
    case MemoryKind::Shared:
        // Write-combined is the model's default mapping: the CPU streams root data in and never
        // reads it back.
        return MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined |
               MTL::ResourceHazardTrackingModeUntracked;
    case MemoryKind::Readback:
        // Shared with the default (cached) CPU cache mode is Metal's readback memory on unified
        // memory; there is no separate readback heap type.
        return MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeDefaultCache |
               MTL::ResourceHazardTrackingModeUntracked;
    case MemoryKind::Private:
        break;
    }
    return MTL::ResourceStorageModePrivate | MTL::ResourceHazardTrackingModeUntracked;
}

//======================================================================================================================
void insertSorted(Device* device, AllocationRecord&& record) {
    const auto position = std::upper_bound(
        device->allocations.begin(), device->allocations.end(), record.base,
        [](GpuAddress value, const AllocationRecord& entry) { return value < entry.base; });
    device->allocations.insert(position, std::move(record));
}

} // namespace

//======================================================================================================================
Result<Allocation> allocate(Device* device, const AllocationDesc& desc) {
    LMX_ASSERT(device != nullptr, "allocate: device must not be null");
    LMX_ASSERT(desc.size > 0, "allocate: AllocationDesc.size must be non-zero");
    LMX_ASSERT(isPowerOfTwo(desc.alignment),
               "allocate: AllocationDesc.alignment must be a power of two");
    LMX_ASSERT(!desc.label.empty(), "allocate: AllocationDesc.label must not be empty");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // Metal chooses the address, so the caller's alignment is met by over-allocating and aligning
    // inside the reservation rather than by asking for an aligned address.
    const uint64_t padding = desc.alignment - 1;
    const uint64_t reserved = desc.size + padding;
    const MTL::ResourceOptions options = optionsFor(desc.kind);
    const std::string label(desc.label);

    AllocationRecord record{};
    record.size = desc.size;
    record.kind = desc.kind;

    if (desc.kind == MemoryKind::Private) {
        // A texture can only be placed inside a heap, never at an offset in a buffer, so private
        // memory is a placement heap. The cover buffer spanning it is what turns the heap into an
        // addressable range: it supplies the base GPU address and resolves copy commands.
        auto heapDesc = NS::TransferPtr(MTL::HeapDescriptor::alloc()->init());
        heapDesc->setType(MTL::HeapTypePlacement);
        heapDesc->setStorageMode(MTL::StorageModePrivate);
        heapDesc->setHazardTrackingMode(MTL::HazardTrackingModeUntracked);
        // The cover buffer is a placed resource, so the heap is sized by what Metal says that
        // buffer occupies rather than by the caller's byte count.
        heapDesc->setSize(device->mtl->heapBufferSizeAndAlign(reserved, options).size);
        record.heap = NS::TransferPtr(device->mtl->newHeap(heapDesc.get()));
        if (!record.heap) {
            return fail(ErrorCode::AllocationFailed,
                        "failed to create a " + std::to_string(reserved) +
                            "-byte placement heap for allocation '" + label + "'");
        }
        record.heap->setLabel(makeString(label).get());
        record.buffer = NS::TransferPtr(record.heap->newBuffer(reserved, options, /*offset=*/0));
        if (!record.buffer) {
            return fail(ErrorCode::AllocationFailed,
                        "failed to cover the placement heap of allocation '" + label +
                            "' with an addressable buffer");
        }
        record.buffer->setLabel(makeString(label + ".cover").get());
        record.allocatedSize = record.heap->size();
    } else {
        record.buffer = NS::TransferPtr(device->mtl->newBuffer(reserved, options));
        if (!record.buffer) {
            return fail(ErrorCode::AllocationFailed, "failed to allocate " +
                                                         std::to_string(reserved) + " bytes for '" +
                                                         label + "'");
        }
        record.buffer->setLabel(makeString(label).get());
        record.allocatedSize = record.buffer->allocatedSize();
    }

    const GpuAddress bufferBase = record.buffer->gpuAddress();
    record.base = alignUp(bufferBase, desc.alignment);
    record.bufferOffset = record.base - bufferBase;
    if (desc.kind != MemoryKind::Private) {
        record.cpu = static_cast<uint8_t*>(record.buffer->contents()) + record.bufferOffset;
    }

    // The heap is the residency unit for private memory: everything placed inside it, cover buffer
    // included, becomes resident with it.
    const MTL::Allocation* resident =
        record.heap ? static_cast<const MTL::Allocation*>(record.heap.get())
                    : static_cast<const MTL::Allocation*>(record.buffer.get());
    addResidency(device, resident, record.allocatedSize);
    record.resident = true;

    const Allocation allocation{
        .cpu = record.cpu, .gpu = record.base, .size = record.size, .kind = record.kind};
    insertSorted(device, std::move(record));
    return allocation;
}

//======================================================================================================================
void deallocate(Device* device, const Allocation& allocation) {
    LMX_ASSERT(device != nullptr, "deallocate: device must not be null");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    const auto position =
        std::find_if(device->allocations.begin(), device->allocations.end(),
                     [&](const AllocationRecord& record) { return record.base == allocation.gpu; });
    LMX_ASSERT(
        position != device->allocations.end(),
        std::format("deallocate: GPU address {:#x} names no live allocation", allocation.gpu));
    LMX_ASSERT(position->placedTextures == 0,
               std::format("deallocate: allocation at {:#x} still holds {} placed texture(s)",
                           allocation.gpu, position->placedTextures));
    assertNoWorkInFlight(device, "deallocate");

    if (position->resident) {
        const MTL::Allocation* resident =
            position->heap ? static_cast<const MTL::Allocation*>(position->heap.get())
                           : static_cast<const MTL::Allocation*>(position->buffer.get());
        removeResidency(device, resident, position->allocatedSize);
    }
    device->allocations.erase(position);
}

} // namespace lmx::experimental::noapi
