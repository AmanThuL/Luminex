//----------------------------------------------------------------------------------------------------------------------
/// @file Semaphore.cpp
/// @brief Implements the monotonic counter used for GPU-to-CPU frame pacing.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include <format>
#include <string>

namespace lmx::experimental::noapi {

//======================================================================================================================
Result<Semaphore*> createSemaphore(Device* device, uint64_t initialValue, std::string_view label) {
    LMX_ASSERT(device != nullptr, "createSemaphore: device must not be null");
    LMX_ASSERT(!label.empty(), "createSemaphore: label must not be empty");
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    auto semaphore = std::make_unique<Semaphore>();
    semaphore->handle = NS::TransferPtr(device->mtl->newSharedEvent());
    if (!semaphore->handle) {
        return fail(ErrorCode::ResourceCreationFailed,
                    "createSemaphore: failed to create shared event '" + std::string(label) + "'");
    }
    semaphore->handle->setLabel(makeString(label).get());
    semaphore->handle->setSignaledValue(initialValue);

    device->liveSemaphores += 1;
    return semaphore.release();
}

//======================================================================================================================
void destroySemaphore(Device* device, Semaphore* semaphore) {
    LMX_ASSERT(device != nullptr, "destroySemaphore: device must not be null");
    LMX_ASSERT(semaphore != nullptr, "destroySemaphore: semaphore must not be null");
    assertNoWorkInFlight(device, "destroySemaphore");

    LMX_ASSERT(device->liveSemaphores > 0, "destroySemaphore: no semaphore is live on this device");
    device->liveSemaphores -= 1;
    delete semaphore;
}

//======================================================================================================================
void waitSemaphore(const Semaphore* semaphore, uint64_t value) {
    LMX_ASSERT(semaphore != nullptr, "waitSemaphore: semaphore must not be null");
    const bool signaled = semaphore->handle->waitUntilSignaledValue(value, kGpuTimeoutMs);
    LMX_ASSERT(signaled,
               std::format("waitSemaphore: the GPU did not reach value {} within {} ms -- it "
                           "stands at {}",
                           value, kGpuTimeoutMs, semaphore->handle->signaledValue()));
}

//======================================================================================================================
uint64_t semaphoreValue(const Semaphore* semaphore) {
    LMX_ASSERT(semaphore != nullptr, "semaphoreValue: semaphore must not be null");
    return semaphore->handle->signaledValue();
}

} // namespace lmx::experimental::noapi
