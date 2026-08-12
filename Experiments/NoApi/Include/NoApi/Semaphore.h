//----------------------------------------------------------------------------------------------------------------------
/// @file Semaphore.h
/// @brief Declares the monotonic counter used for GPU-to-CPU frame pacing.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/Result.h"

#include <cstdint>
#include <string_view>

namespace lmx::experimental::noapi {

/// Creates a timeline semaphore starting at `initialValue`.
///
/// One semaphore paces every frame: submissions signal increasing values and the CPU waits for the
/// value that retires the frame slot it wants to reuse. There is no per-submission fence object.
///
/// Fails with `ErrorCode::ResourceCreationFailed` when the device rejects the request.
Result<Semaphore*> createSemaphore(Device* device, uint64_t initialValue, std::string_view label);

/// Destroys a semaphore created by `createSemaphore`.
///
/// Every submission that signals the semaphore must have completed; this is a caller contract and
/// asserts.
void destroySemaphore(Device* device, Semaphore* semaphore);

/// Blocks the calling thread until the semaphore reaches at least `value`.
void waitSemaphore(const Semaphore* semaphore, uint64_t value);

/// Returns the value the semaphore has currently reached.
uint64_t semaphoreValue(const Semaphore* semaphore);

} // namespace lmx::experimental::noapi
