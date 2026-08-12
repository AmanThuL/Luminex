//----------------------------------------------------------------------------------------------------------------------
/// @file Device.h
/// @brief Declares device creation, its single queue, and the capabilities callers must honor.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/Result.h"
#include "NoApi/Types.h"

#include <cstdint>
#include <string_view>

namespace lmx::experimental::noapi {

/// Reports the target properties a caller cannot assume and must not guess.
///
/// The comparison model states a minimum hardware specification instead of a capability query,
/// which works for a single-target design and does not survive a second target. These are the
/// properties that differ between targets while the interface stays the same, so a caller reads
/// them rather than hardcoding the numbers the model's examples use.
struct Capabilities {
    uint32_t bindlessSlotStride = 0;   ///< Bytes per bindless table slot as the shader sees it.
    uint32_t maxBindlessSlots = 0;     ///< Largest bindless table the device supports.
    uint64_t minRootDataAlignment = 0; ///< Smallest alignment a root-data address may use.
    uint64_t minCopyAlignment = 0;     ///< Smallest alignment a copy source or destination may use.
    uint64_t minIndirectAlignment = 0; ///< Smallest alignment an indirect argument address may use.
    bool separateBlendState = false;   ///< Whether blend state can change without a new pipeline.
    /// Whether split barriers signal plain GPU memory rather than an object.
    bool memoryBackedFences = false;
    bool residencyRequired = false; ///< Whether allocations must join a residency set before use.
    /// Whether a GPU capture replays with identical GPU addresses.
    bool capturePersistsAddresses = false;
};

/// Describes the device to create.
struct DeviceDesc {
    std::string_view label; ///< Debug label; must be non-empty.
};

/// Creates a device on the system's default GPU.
///
/// Fails with `ErrorCode::DeviceUnsupported` when no GPU meets the prototype's required feature
/// level. The error message names the missing requirement rather than reporting a bare failure.
Result<Device*> createDevice(const DeviceDesc& desc);

/// Destroys a device and asserts that every object created from it has already been destroyed.
void destroyDevice(Device* device);

/// Returns the device's capabilities.
const Capabilities& capabilities(const Device* device);

/// Returns the device's single queue.
///
/// The queue is owned by the device and outlives every command buffer submitted to it. Multi-queue
/// work is outside the prototype's scope, so there is exactly one.
Queue* mainQueue(Device* device);

/// Live creation counts (M5.1 spec section 9's allocation dimension: "allocation call counts").
///
/// Every field counts objects created and not yet destroyed. Since neither M5.1 adapter destroys
/// anything before its own teardown, a live count sampled at end of setup or end of run equals the
/// cumulative number of creation calls made up to that point -- the two coincide for this
/// experiment's workloads, which is what makes a live count a faithful stand-in for a call count
/// here without a second, redundant cumulative counter.
struct DeviceCreationStats {
    uint32_t liveTextures = 0;    ///< createTexture calls not yet matched by destroyTexture.
    uint32_t livePipelines = 0;   ///< createGraphicsPipeline/createComputePipeline calls live.
    uint32_t liveSamplers = 0;    ///< createSampler calls not yet matched by destroySampler.
    uint32_t liveAllocations = 0; ///< allocate calls not yet matched by deallocate.
    /// Sum of every live allocation's requested `AllocationDesc::size`. This is an unscored
    /// descriptive logical-size total; see `ResidencySet.h`'s `residentBytes` for the prototype's
    /// true Metal-reported diagnostic total.
    uint64_t requestedBytes = 0;
};

/// Returns the device's current live creation counts.
DeviceCreationStats deviceCreationStats(const Device* device);

} // namespace lmx::experimental::noapi
