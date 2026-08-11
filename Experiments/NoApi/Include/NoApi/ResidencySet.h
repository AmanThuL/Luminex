//----------------------------------------------------------------------------------------------------------------------
/// @file ResidencySet.h
/// @brief Declares the single residency set the target API requires.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/Memory.h"
#include "NoApi/Result.h"

#include <cstdint>
#include <string_view>

namespace lmx::noapi {

/// Describes the device's residency set.
struct ResidencySetDesc {
    uint32_t initialCapacity = 0; ///< Expected number of allocations, used only to presize storage.
    std::string_view label;       ///< Debug label; must be non-empty.
};

/// Creates the device's residency set.
///
/// A device owns at most one set; creating a second is a caller contract violation and asserts.
/// The comparison model has no residency concept — memory is simply mapped and addressable — so
/// everything declared here is target-imposed and is recorded as such in the mapping.
///
/// Fails with `ErrorCode::ResourceCreationFailed` when the device rejects the request.
Result<ResidencySet*> createResidencySet(Device* device, const ResidencySetDesc& desc);

/// Destroys the residency set.
///
/// Every submission referencing the set must be retired; this is a caller contract and asserts.
void destroyResidencySet(Device* device, ResidencySet* set);

/// Adds an allocation to the set, so that every address inside it is GPU-accessible.
///
/// Takes effect at the next `commitResidency`.
void addAllocationResidency(ResidencySet* set, const Allocation& allocation);

/// Removes an allocation previously added with `addAllocationResidency`.
///
/// Takes effect at the next `commitResidency`.
void removeAllocationResidency(ResidencySet* set, const Allocation& allocation);

/// Applies every pending addition and removal.
///
/// Must be called before submitting work that dereferences a newly added allocation's addresses.
/// Failing to do so produces a GPU fault rather than a silent wrong result, which is why this is
/// the only residency operation with ordering significance.
void commitResidency(ResidencySet* set);

/// Returns the total byte size of every allocation currently in the set.
uint64_t residentBytes(const ResidencySet* set);

} // namespace lmx::noapi
