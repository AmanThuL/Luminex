//----------------------------------------------------------------------------------------------------------------------
/// @file TransientPool.h
/// @brief Declares the per-frame-slot placement-heap pool the render graph's transients live in.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "RHI/RHI.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace lmx::render {

/// How many frames the RHI keeps in flight, and therefore how many independent sets of transient
/// memory a pool has to hold. Stated here rather than taken from the backend because it is the
/// number this pool's whole safety argument rests on: a slot is reusable only once the frame that
/// last used it has retired, and the RHI's pacing is what proves that.
inline constexpr uint32_t kTransientFrameSlots = 3;

/// The physical memory the render graph's transient resources are placed in.
///
/// One placement heap per frame-in-flight slot, and never fewer: frames N and N + 1 are alive at
/// the same time, so a transient of one may not share bytes with a transient of the other. A slot
/// is keyed by `Device::frameNumber() % kTransientFrameSlots`, which is the same rotation the RHI
/// paces on -- so the beginFrame() below, called immediately after Device::beginFrame(), stands on
/// a wait the RHI has already performed rather than on a guess about what the GPU has finished.
///
/// A slot holds one heap *generation* at a time, sized to exactly the high-water mark of the frame
/// that reserved it. A frame asking for a different footprint -- a viewport resize, a feature
/// toggled on, pooling switched off -- gets a new generation, and the outgoing one goes on a
/// deferred-release list keyed by the last frame number that used it. Nothing is destroyed until
/// that frame is provably retired.
///
/// The resources themselves are created per frame and released when their slot comes round again,
/// for the same reason: an MTL4 encoder binds a resource by identity and does not retain it, so a
/// placed texture must outlive the frame that referenced it, and the slot rotation is what says
/// when it has.
///
/// The pool must outlive every RenderGraph that names it, and must not outlive its device.
class TransientPool {
public:
    /// Binds the pool to the device its heaps and placed resources are created on. Nothing is
    /// allocated here: a pool that no frame ever reserves memory from owns no GPU objects.
    explicit TransientPool(rhi::Device& device) : m_device(device) {}

    /// Non-copyable: it owns heaps and the resources placed in them, and a copy would duplicate the
    /// slot rotation that keeps them alive.
    TransientPool(const TransientPool&) = delete;
    /// Non-assignable, for the reason the copy constructor is deleted.
    TransientPool& operator=(const TransientPool&) = delete;

    /// Opens the pool's frame, which must be the device frame Device::beginFrame() just opened:
    /// this releases the resources the previous occupant of that slot placed, and that is safe
    /// only because beginFrame() has already waited for it to retire. Frame numbers must not go
    /// backwards.
    void beginFrame();

    /// Makes the open frame's slot hold a heap of exactly `bytes`, creating a new generation and
    /// retiring the old one when that is not what it already holds. `bytes` == 0 releases the
    /// slot's generation, which is what a frame declaring no transients asks for.
    ///
    /// Exact rather than grow-only so a shrink gives the memory back: a frame's transient
    /// footprint changes when the caller changes what it declares, not from frame to frame.
    ///
    /// One reservation per frame, whatever it asks for: a second one is refused rather than handing
    /// a second caller offsets into a heap the first caller's resources are still live in. Two
    /// graphs that both declare transients therefore belong in two frames.
    rhi::Result<void> reserve(uint64_t bytes);

    /// Places a texture at `offset` in the open slot's heap and keeps it alive until that slot is
    /// reused. Requires a preceding reserve() whose bytes cover the placement.
    rhi::Result<rhi::Texture*> placeTexture(const rhi::TextureDesc& desc, uint64_t offset);
    /// The buffer counterpart of placeTexture.
    rhi::Result<rhi::Buffer*> placeBuffer(const rhi::BufferDesc& desc, uint64_t offset);

    /// The device the pool's resources are created on, and the one whose textureSizeAlign and
    /// bufferSizeAlign a graph plans its layout against.
    rhi::Device& device() const { return m_device; }

    /// Every heap the pool still owns: the live generation of each slot plus every generation
    /// waiting to be released. Exposed so a test can assert that toggling and resizing a frame
    /// leaves nothing behind -- a count that grows without bound is a leak.
    size_t liveGenerationCount() const;
    /// The generations that have been replaced but whose last frame has not yet been proved
    /// retired. Zero in a steady state.
    size_t retiringGenerationCount() const { return m_retiring.size(); }
    /// The size of the open slot's heap, which is what the last reserve() asked for. Zero when the
    /// slot holds no generation.
    uint64_t heapBytes() const;

private:
    // One slot's heap and the resources this slot's current frame placed in it. Resources are
    // declared after the heap so they are destroyed first -- a placed resource must not outlive
    // the memory it sits in.
    struct Slot {
        std::unique_ptr<rhi::Heap> heap;
        uint64_t bytes = 0;
        // Whether the open frame has already reserved this slot. Tracked rather than inferred from
        // the placed resources below, because a reservation that happens to ask for the bytes the
        // slot already holds places nothing new and would leave nothing to infer from.
        bool reserved = false;
        // The frame that most recently placed into `heap`, which is what dates the generation
        // when it is retired.
        uint64_t lastFrame = 0;
        std::vector<std::unique_ptr<rhi::Texture>> textures;
        std::vector<std::unique_ptr<rhi::Buffer>> buffers;
    };

    // A generation taken out of service, with the frame number after which nothing can still be
    // reading it. kTransientFrameSlots frames past its last use is the same bound the RHI's pacing
    // uses, restated here so the release rule is checkable rather than inferred from which slot
    // happened to own the heap.
    struct Retiring {
        std::unique_ptr<rhi::Heap> heap;
        uint64_t releaseAtFrame = 0;
    };

    Slot& openSlot();
    const Slot& openSlot() const;

    rhi::Device& m_device;
    Slot m_slots[kTransientFrameSlots];
    std::vector<Retiring> m_retiring;
    // The frame beginFrame() last opened; zero until it has been called, which is what makes a
    // placement before the first beginFrame() detectable.
    uint64_t m_frame = 0;
};

} // namespace lmx::render
