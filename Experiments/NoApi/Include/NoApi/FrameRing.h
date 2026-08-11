//----------------------------------------------------------------------------------------------------------------------
/// @file FrameRing.h
/// @brief Declares the three-frames-in-flight slot ring and its retirement proof.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/LinearAllocator.h"
#include "NoApi/Memory.h"
#include "NoApi/Result.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace lmx::noapi {

/// Describes the per-frame storage a frame ring owns.
struct FrameRingDesc {
    /// Root-data bytes each frame slot may suballocate; must be non-zero.
    uint64_t bytesPerFrame = 0;
    std::string_view label; ///< Debug label; must be non-empty and is extended per slot.
};

/// Owns one CPU-writable root-data allocation per frame in flight and the pacing that recycles it.
///
/// This is where the interface's lifetime rule lives. A slot's storage may only be reused once the
/// submission that last referenced it has completed, and the ring proves that with a single
/// timeline semaphore rather than a fence per submission. Reusing a slot that is not retired is the
/// one lifetime error the model's address-based binding cannot detect on the GPU, so it is checked
/// here and is fatal.
///
/// Move-only and not thread-safe: one ring belongs to one recording thread.
class FrameRing {
public:
    /// Number of frames the ring keeps in flight.
    static constexpr uint32_t kFramesInFlight = 3;

    /// Constructs an empty ring that owns nothing.
    FrameRing() = default;

    /// Destroys the ring after waiting for every slot to retire.
    ~FrameRing();

    /// Transfers ownership from `other`, which becomes empty.
    FrameRing(FrameRing&& other) noexcept;

    /// Transfers ownership from `other`, releasing anything this ring owned.
    FrameRing& operator=(FrameRing&& other) noexcept;

    FrameRing(const FrameRing&) = delete;
    FrameRing& operator=(const FrameRing&) = delete;

    /// Creates a ring with `kFramesInFlight` slots of `desc.bytesPerFrame` each.
    ///
    /// Fails with `ErrorCode::AllocationFailed` when the per-frame storage cannot be allocated and
    /// with `ErrorCode::ResourceCreationFailed` when the pacing semaphore cannot be created.
    static Result<FrameRing> create(Device* device, const FrameRingDesc& desc);

    /// Waits until the next slot is retired, resets its allocator, and returns the slot index.
    ///
    /// Asserts when a frame is already open. The wait is the only place this interface blocks the
    /// CPU on GPU progress.
    uint32_t beginFrame();

    /// Submits `commands`, signals the open frame's value, and closes the frame.
    ///
    /// Asserts when no frame is open. After this call the slot's allocator must not be written
    /// again until `beginFrame` returns that slot.
    void endFrame(Queue* queue, std::span<CommandBuffer* const> commands);

    /// Returns the allocator backing the open frame's slot.
    ///
    /// Asserts when no frame is open, which is exactly the misuse of writing root data outside the
    /// frame that owns it.
    LinearAllocator& rootAllocator();

    /// Returns the monotonically increasing index of the open or most recently closed frame.
    uint64_t frameIndex() const { return m_frameIndex; }

    /// Returns the slot index the open frame is using.
    uint32_t slot() const { return static_cast<uint32_t>(m_frameIndex % kFramesInFlight); }

    /// Reports whether every submission that used `slot` has completed.
    bool isSlotRetired(uint32_t slot) const;

private:
    Device* m_device = nullptr;
    Semaphore* m_semaphore = nullptr;
    std::array<Allocation, kFramesInFlight> m_storage{};
    std::array<LinearAllocator, kFramesInFlight> m_allocators{};
    uint64_t m_frameIndex = 0;
    bool m_frameOpen = false;
};

} // namespace lmx::noapi
