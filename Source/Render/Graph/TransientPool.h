//----------------------------------------------------------------------------------------------------------------------
/// @file TransientPool.h
/// @brief Declares paced placement caches identified by complete structural layouts.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Graph/TransientBackend.h"
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lmx::engine {
/// Owns the device and pacing counter borrowed by real transient placement.
class GpuContext;
} // namespace lmx::engine
namespace lmx::render {
/// Independent slots covering both supported device frame counts, two and three.
inline constexpr uint32_t kTransientFrameSlots = 3;
/// The exact effective buffer descriptor selected by the schedule.
struct TransientBufferPlacementDesc {
    TransientBufferDesc desc;  ///< Source descriptor.
    bool indirectArgs = false; ///< Additional inferred usage.
    /// Compares every effective field.
    bool operator==(const TransientBufferPlacementDesc&) const = default;
};
/// One used resource's complete native placement identity.
struct TransientPlacementDesc {
    uint32_t resource = 0;        ///< Graph resource association.
    uint64_t offset = 0;          ///< Byte offset in the heap.
    TransientFootprint footprint; ///< Answer for this exact effective descriptor.
    std::variant<TransientTextureDesc, TransientBufferPlacementDesc> desc; ///< Creation fields.
    std::string label;                                                     ///< Owned object label.
    /// Compares complete structure, never only a hash or byte total.
    bool operator==(const TransientPlacementDesc&) const = default;
};
/// One slot's complete requested allocation and placed-resource layout.
struct TransientLayout {
    uint64_t bytes = 0;                             ///< Heap size.
    std::vector<TransientPlacementDesc> placements; ///< Used resources in deterministic order.
    /// Compares complete structure.
    bool operator==(const TransientLayout&) const = default;
};
/// Caches resources/views by exact layout in three paced frame slots.
/// beginFrame follows GpuContext::beginFrame; frame N-3 is then retired for either supported
/// framesInFlight value. The borrowed context/backend outlives this pool.
class TransientPool {
public:
    /// Owns a real backend borrowing gpu; allocates nothing.
    explicit TransientPool(engine::GpuContext& gpu);
    /// Borrows a backend, including a device-free test implementation.
    explicit TransientPool(TransientBackend& backend) : m_backend(backend) {}
    /// Releases placed objects before heaps.
    ~TransientPool();
    /// Non-copyable ownership.
    TransientPool(const TransientPool&) = delete;
    /// Non-assignable ownership.
    TransientPool& operator=(const TransientPool&) = delete;
    /// Opens the context's newly paced slot; frame numbers must increase.
    void beginFrame();
    /// Reserves exactly once per frame; only an exact completed layout is a cache hit.
    lmx::Result<void> reserve(const TransientLayout& layout);
    /// Consumes the next reserved texture entry, returning its cached or newly created ref.
    lmx::Result<engine::TextureRef> placeTexture(const TransientTextureDesc&, uint64_t offset,
                                                 std::string_view label);
    /// Consumes the next reserved buffer entry using its exact effective usage.
    lmx::Result<rojoRHI::Buffer> placeBuffer(const TransientBufferDesc&, uint64_t offset,
                                             std::string_view label, bool indirectArgs = false);
    /// Proves that every reserved entry was consumed; a partial creation is never a cache hit.
    void finishPlacement();
    /// Queries during declaration only.
    TransientFootprint footprint(const TransientTextureDesc& desc) const {
        return m_backend.footprint(desc);
    }
    /// Queries one effective variant during declaration only.
    TransientFootprint footprint(const TransientBufferDesc& desc, bool indirectArgs = false) const {
        return m_backend.footprint(desc, indirectArgs);
    }
    /// Reads a retained storage view, without a device call.
    rojoRHI::ViewIndex storageView(rojoRHI::Texture texture, uint32_t mip) const;
    /// Reads a retained address, without a device call.
    rojoRHI::Addr address(rojoRHI::Buffer buffer) const;
    /// The context/backend frame counter.
    uint64_t frameNumber() const { return m_backend.frameNumber(); }
    /// Counts live plus retiring heap generations.
    size_t liveGenerationCount() const;
    /// Counts outgoing generations awaiting release.
    size_t retiringGenerationCount() const { return m_retiring.size(); }
    /// Returns the open slot's heap size, or zero before a frame.
    uint64_t heapBytes() const;

private:
    using Placed = std::variant<engine::TextureRef, rojoRHI::Buffer>;
    struct Slot {
        std::optional<TransientHeapId> heap;
        uint64_t bytes = 0;
        bool reserved = false;
        uint64_t lastFrame = 0;
        TransientLayout layout;
        std::vector<Placed> placed;
        size_t cursor = 0;
        bool complete = false;
    };
    struct Retiring {
        TransientHeapId heap;
        uint64_t releaseAtFrame = 0;
    };
    Slot& openSlot();
    const Slot& openSlot() const;
    const TransientPlacementDesc& nextPlacement(uint64_t offset, std::string_view label) const;
    std::unique_ptr<TransientBackend> m_ownedBackend;
    TransientBackend& m_backend;
    Slot m_slots[kTransientFrameSlots];
    std::vector<Retiring> m_retiring;
    uint64_t m_frame = 0;
};
} // namespace lmx::render
