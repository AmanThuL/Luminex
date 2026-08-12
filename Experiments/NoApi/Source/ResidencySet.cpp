//----------------------------------------------------------------------------------------------------------------------
/// @file ResidencySet.cpp
/// @brief Implements the single, target-imposed residency set over the device's allocations.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include <format>

namespace lmx::experimental::noapi {

//======================================================================================================================
Result<ResidencySet*> createResidencySet(Device* device, const ResidencySetDesc& desc) {
    LMX_ASSERT(device != nullptr, "createResidencySet: device must not be null");
    LMX_ASSERT(device->residencyHandle == nullptr,
               "createResidencySet: this device already owns a residency set");
    LMX_ASSERT(!desc.label.empty(), "createResidencySet: ResidencySetDesc.label must not be empty");

    // The Metal set itself is created with the device, because Metal wants it attached to the
    // queue before any submission and because every allocation joins it as it is made. This handle
    // is the caller-facing view of that set; `initialCapacity` has nothing to presize.
    auto* set = new ResidencySet{.device = device};
    device->residencyHandle = set;
    return set;
}

//======================================================================================================================
void destroyResidencySet(Device* device, ResidencySet* set) {
    LMX_ASSERT(device != nullptr, "destroyResidencySet: device must not be null");
    LMX_ASSERT(set != nullptr, "destroyResidencySet: set must not be null");
    LMX_ASSERT(set->device == device, "destroyResidencySet: this set belongs to another device");
    assertNoWorkInFlight(device, "destroyResidencySet");

    device->residencyHandle = nullptr;
    delete set;
}

//======================================================================================================================
void addAllocationResidency(ResidencySet* set, const Allocation& allocation) {
    LMX_ASSERT(set != nullptr, "addAllocationResidency: set must not be null");
    Device* device = set->device;
    AllocationRecord* record = findAllocation(device, allocation.gpu);
    LMX_ASSERT(record != nullptr,
               std::format("addAllocationResidency: GPU address {:#x} names no live allocation",
                           allocation.gpu));

    // `allocate` already joined the set, because an address whose memory is not resident faults
    // rather than misbehaves. Re-adding is idempotent on Metal and keeps the declared contract --
    // add, then commit -- meaningful to a caller who follows it.
    const MTL::Allocation* resident =
        record->heap ? static_cast<const MTL::Allocation*>(record->heap.get())
                     : static_cast<const MTL::Allocation*>(record->buffer.get());
    if (record->resident) {
        device->residency->addAllocation(resident);
        device->residencyDirty = true;
        return;
    }
    addResidency(device, resident, record->allocatedSize);
    record->resident = true;
}

//======================================================================================================================
void removeAllocationResidency(ResidencySet* set, const Allocation& allocation) {
    LMX_ASSERT(set != nullptr, "removeAllocationResidency: set must not be null");
    Device* device = set->device;
    AllocationRecord* record = findAllocation(device, allocation.gpu);
    LMX_ASSERT(record != nullptr,
               std::format("removeAllocationResidency: GPU address {:#x} names no live allocation",
                           allocation.gpu));
    if (!record->resident) {
        return;
    }

    const MTL::Allocation* resident =
        record->heap ? static_cast<const MTL::Allocation*>(record->heap.get())
                     : static_cast<const MTL::Allocation*>(record->buffer.get());
    removeResidency(device, resident, record->allocatedSize);
    record->resident = false;
}

//======================================================================================================================
void commitResidency(ResidencySet* set) {
    LMX_ASSERT(set != nullptr, "commitResidency: set must not be null");
    commitPendingResidency(set->device);
}

//======================================================================================================================
uint64_t residentBytes(const ResidencySet* set) {
    LMX_ASSERT(set != nullptr, "residentBytes: set must not be null");
    return set->device->residentBytes;
}

} // namespace lmx::experimental::noapi
