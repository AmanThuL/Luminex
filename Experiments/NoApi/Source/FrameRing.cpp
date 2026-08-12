//----------------------------------------------------------------------------------------------------------------------
/// @file FrameRing.cpp
/// @brief Implements the three-frames-in-flight slot ring and its retirement proof.
//----------------------------------------------------------------------------------------------------------------------

#include "Metal4Internal.h"

#include <format>
#include <new>
#include <string>
#include <utility>

namespace lmx::experimental::noapi {

//======================================================================================================================
Result<FrameRing> FrameRing::create(Device* device, const FrameRingDesc& desc) {
    LMX_ASSERT(device != nullptr, "FrameRing::create: device must not be null");
    LMX_ASSERT(desc.bytesPerFrame > 0, "FrameRing::create: bytesPerFrame must be non-zero");
    LMX_ASSERT(!desc.label.empty(), "FrameRing::create: label must not be empty");

    FrameRing ring;
    ring.m_device = device;
    const std::string label(desc.label);

    for (uint32_t slot = 0; slot < kFramesInFlight; ++slot) {
        const std::string slotLabel = label + ".slot" + std::to_string(slot);
        Result<Allocation> storage = allocate(device, {.size = desc.bytesPerFrame,
                                                       .alignment = kDefaultAlignment,
                                                       .kind = MemoryKind::Shared,
                                                       .label = slotLabel});
        if (!storage) {
            // Nothing is left half-owned: the ring's destructor releases whatever was built.
            return std::unexpected(storage.error());
        }
        ring.m_storage[slot] = *storage;
        ring.m_allocators[slot] = LinearAllocator(*storage);
    }

    Result<Semaphore*> semaphore = createSemaphore(device, 0, label + ".pacing");
    if (!semaphore) {
        return std::unexpected(semaphore.error());
    }
    ring.m_semaphore = *semaphore;

    return ring;
}

//======================================================================================================================
FrameRing::~FrameRing() {
    if (m_device == nullptr) {
        return;
    }
    if (m_semaphore != nullptr) {
        // An open frame was never submitted, so the last value the GPU can reach is the frame
        // before it.
        const uint64_t submitted = m_frameOpen ? m_frameIndex - 1 : m_frameIndex;
        if (submitted > 0) {
            waitSemaphore(m_semaphore, submitted);
        }
        destroySemaphore(m_device, m_semaphore);
        m_semaphore = nullptr;
    }
    for (Allocation& storage : m_storage) {
        if (storage.size > 0) {
            deallocate(m_device, storage);
            storage = Allocation{};
        }
    }
    m_device = nullptr;
}

//======================================================================================================================
FrameRing::FrameRing(FrameRing&& other) noexcept
    : m_device(other.m_device), m_semaphore(other.m_semaphore), m_storage(other.m_storage),
      m_allocators(other.m_allocators), m_frameIndex(other.m_frameIndex),
      m_frameOpen(other.m_frameOpen) {
    other.m_device = nullptr;
    other.m_semaphore = nullptr;
    other.m_storage = {};
    other.m_allocators = {};
    other.m_frameIndex = 0;
    other.m_frameOpen = false;
}

//======================================================================================================================
FrameRing& FrameRing::operator=(FrameRing&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    // Releasing this ring's slots means waiting for them, which is exactly the destructor's job;
    // rebuilding in place afterwards keeps the move constructor as the single transfer rule.
    this->~FrameRing();
    new (this) FrameRing(std::move(other));
    return *this;
}

//======================================================================================================================
uint32_t FrameRing::beginFrame() {
    LMX_ASSERT(m_device != nullptr, "beginFrame: this frame ring owns nothing");
    LMX_ASSERT(!m_frameOpen, "beginFrame: the previous frame is still open -- call endFrame");

    // Frames are numbered from one so that the pacing semaphore's initial value of zero means
    // "nothing submitted yet" without a second piece of state.
    m_frameIndex += 1;
    const uint32_t slotIndex = slot();

    if (m_frameIndex > kFramesInFlight) {
        const uint64_t retiring = m_frameIndex - kFramesInFlight;
        waitSemaphore(m_semaphore, retiring);
        // The wait is the proof; this check is what turns a caller who submitted work referencing
        // this slot outside `endFrame` into a diagnosable failure rather than a corrupted frame.
        LMX_ASSERT(semaphoreValue(m_semaphore) >= retiring,
                   std::format("beginFrame: frame {} is about to reuse slot {}, whose last owner "
                               "(frame {}) has not retired -- the GPU stands at {}",
                               m_frameIndex, slotIndex, retiring, semaphoreValue(m_semaphore)));
    }

    m_allocators[slotIndex].reset();
    m_frameOpen = true;
    return slotIndex;
}

//======================================================================================================================
void FrameRing::endFrame(Queue* queue, std::span<CommandBuffer* const> commands) {
    LMX_ASSERT(m_frameOpen, "endFrame: no frame is open -- call beginFrame first");
    LMX_ASSERT(queue != nullptr, "endFrame: queue must not be null");

    // The frame's value is what a later beginFrame waits for before touching this slot again.
    submit(queue, commands, m_semaphore, m_frameIndex);
    m_frameOpen = false;
}

//======================================================================================================================
LinearAllocator& FrameRing::rootAllocator() {
    LMX_ASSERT(m_frameOpen, "rootAllocator: no frame is open -- root data may only be written "
                            "inside the frame that owns the slot");
    return m_allocators[slot()];
}

//======================================================================================================================
bool FrameRing::isSlotRetired(uint32_t slot) const {
    LMX_ASSERT(slot < kFramesInFlight, "isSlotRetired: slot is outside the ring");
    // The slot's last owner is the newest frame that maps to it. An open frame counts as an owner,
    // so a slot being written right now reports as unretired.
    uint64_t owner = 0;
    for (uint64_t back = 0; back < kFramesInFlight && back < m_frameIndex; ++back) {
        const uint64_t candidate = m_frameIndex - back;
        if (candidate % kFramesInFlight == slot) {
            owner = candidate;
            break;
        }
    }
    return owner == 0 || semaphoreValue(m_semaphore) >= owner;
}

} // namespace lmx::experimental::noapi
