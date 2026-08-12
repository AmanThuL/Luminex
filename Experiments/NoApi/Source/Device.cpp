//----------------------------------------------------------------------------------------------------------------------
/// @file Device.cpp
/// @brief Implements Metal 4 device creation, capability reporting, and the single queue.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include <format>

namespace lmx::experimental::noapi {
namespace {

// Alignment minima Metal exposes no query for, pinned by the prototype from Apple silicon's
// documented four-byte buffer-argument, copy, and indirect-argument alignment. A caller still owes
// each root block its own natural alignment, which is what `pushRoot`'s default of sixteen covers.
constexpr uint64_t kRootDataAlignment = 4;
constexpr uint64_t kCopyAlignment = 4;
constexpr uint64_t kIndirectAlignment = 4;

} // namespace

//======================================================================================================================
Result<Device*> createDevice(const DeviceDesc& desc) {
    LMX_ASSERT(!desc.label.empty(), "createDevice: DeviceDesc.label must not be empty");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    auto device = std::make_unique<Device>();
    device->mtl = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    if (!device->mtl) {
        return fail(ErrorCode::DeviceUnsupported, "no Metal device available on this system");
    }
    const NS::String* name = device->mtl->name();
    device->name =
        name != nullptr && name->utf8String() != nullptr ? name->utf8String() : "<unnamed device>";

    if (!device->mtl->supportsFamily(MTL::GPUFamilyMetal4)) {
        return fail(ErrorCode::DeviceUnsupported,
                    "device '" + device->name + "' does not support MTLGPUFamilyMetal4");
    }
    // The bindless table is a caller-owned buffer of resource IDs, which is exactly what tier-2
    // argument buffers are; tier 1 cannot express it at all.
    if (device->mtl->argumentBuffersSupport() != MTL::ArgumentBuffersTier2) {
        return fail(ErrorCode::DeviceUnsupported,
                    "device '" + device->name +
                        "' does not support tier-2 argument buffers, so a shader-indexable "
                        "bindless table cannot be expressed");
    }

    NS::Error* error = nullptr;

    auto queueDesc = NS::TransferPtr(MTL4::CommandQueueDescriptor::alloc()->init());
    queueDesc->setLabel(makeString(std::string(desc.label) + ".queue").get());
    device->queue = NS::TransferPtr(device->mtl->newMTL4CommandQueue(queueDesc.get(), &error));
    if (!device->queue) {
        return fail(ErrorCode::DeviceUnsupported,
                    "failed to create the MTL4 command queue: " + describe(error));
    }

    error = nullptr;
    auto compilerDesc = NS::TransferPtr(MTL4::CompilerDescriptor::alloc()->init());
    compilerDesc->setLabel(makeString(std::string(desc.label) + ".compiler").get());
    device->compiler = NS::TransferPtr(device->mtl->newCompiler(compilerDesc.get(), &error));
    if (!device->compiler) {
        return fail(ErrorCode::DeviceUnsupported,
                    "failed to create the MTL4 compiler: " + describe(error));
    }

    error = nullptr;
    auto residencyDesc = NS::TransferPtr(MTL::ResidencySetDescriptor::alloc()->init());
    residencyDesc->setLabel(makeString(std::string(desc.label) + ".residency").get());
    device->residency = NS::TransferPtr(device->mtl->newResidencySet(residencyDesc.get(), &error));
    if (!device->residency) {
        return fail(ErrorCode::DeviceUnsupported,
                    "failed to create the residency set: " + describe(error));
    }
    // Attaching the set to the queue once is what makes a later commit publish to every
    // submission; the model has no residency concept at all, so all of this is target-imposed.
    device->residency->commit();
    device->queue->addResidencySet(device->residency.get());

    device->timeline = NS::TransferPtr(device->mtl->newSharedEvent());
    if (!device->timeline) {
        return fail(ErrorCode::DeviceUnsupported, "failed to create the submission timeline event");
    }
    device->timeline->setLabel(makeString(std::string(desc.label) + ".submissionTimeline").get());
    device->timeline->setSignaledValue(0);

    device->caps = Capabilities{.bindlessSlotStride = kBindlessSlotStride,
                                .maxBindlessSlots = kMaxBindlessSlots,
                                .minRootDataAlignment = kRootDataAlignment,
                                .minCopyAlignment = kCopyAlignment,
                                .minIndirectAlignment = kIndirectAlignment,
                                .separateBlendState = false,
                                .memoryBackedFences = false,
                                .residencyRequired = true,
                                .capturePersistsAddresses = false};
    device->queueHandle.device = device.get();

    return device.release();
}

//======================================================================================================================
void destroyDevice(Device* device) {
    LMX_ASSERT(device != nullptr, "destroyDevice: device must not be null");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // Draining first makes the object-leak assertions below report what the caller forgot rather
    // than what the GPU is still using.
    drainDevice(device);

    LMX_ASSERT(
        device->liveTextures == 0,
        std::format("destroyDevice: {} texture(s) were never destroyed", device->liveTextures));
    LMX_ASSERT(
        device->liveSamplers == 0,
        std::format("destroyDevice: {} sampler(s) were never destroyed", device->liveSamplers));
    LMX_ASSERT(
        device->livePipelines == 0,
        std::format("destroyDevice: {} pipeline(s) were never destroyed", device->livePipelines));
    LMX_ASSERT(device->liveDepthStates == 0,
               std::format("destroyDevice: {} depth-stencil state(s) were never destroyed",
                           device->liveDepthStates));
    LMX_ASSERT(
        device->liveSemaphores == 0,
        std::format("destroyDevice: {} semaphore(s) were never destroyed", device->liveSemaphores));
    LMX_ASSERT(device->table == nullptr, "destroyDevice: the bindless table was never destroyed");
    LMX_ASSERT(device->residencyHandle == nullptr,
               "destroyDevice: the residency set was never destroyed");
    LMX_ASSERT(device->allocations.empty(),
               std::format("destroyDevice: {} allocation(s) were never freed",
                           device->allocations.size()));

    device->queue->removeResidencySet(device->residency.get());
    delete device;
}

//======================================================================================================================
const Capabilities& capabilities(const Device* device) {
    LMX_ASSERT(device != nullptr, "capabilities: device must not be null");
    return device->caps;
}

//======================================================================================================================
Queue* mainQueue(Device* device) {
    LMX_ASSERT(device != nullptr, "mainQueue: device must not be null");
    return &device->queueHandle;
}

//======================================================================================================================
DeviceCreationStats deviceCreationStats(const Device* device) {
    LMX_ASSERT(device != nullptr, "deviceCreationStats: device must not be null");
    uint64_t requestedBytes = 0;
    for (const AllocationRecord& record : device->allocations) {
        requestedBytes += record.size;
    }
    return {.liveTextures = device->liveTextures,
            .livePipelines = device->livePipelines,
            .liveSamplers = device->liveSamplers,
            .liveAllocations = static_cast<uint32_t>(device->allocations.size()),
            .requestedBytes = requestedBytes};
}

} // namespace lmx::experimental::noapi
