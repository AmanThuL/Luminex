//----------------------------------------------------------------------------------------------------------------------
/// @file TransientBackend.h
/// @brief Declares the placement and cached metadata seam used by transient pools.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Graph/TransientResourceDesc.h"
#include <cstdint>
#include <rojoRHI/RHI.h>
#include <string_view>

namespace lmx::render {
/// The exact size and alignment returned for one effective creation descriptor.
struct TransientFootprint {
    uint64_t size = 0;      ///< Allocation bytes.
    uint64_t alignment = 0; ///< Required offset alignment.
    /// Compares both fields.
    bool operator==(const TransientFootprint&) const = default;
};
/// Backend-owned heap identity.
struct TransientHeapId {
    uint32_t value = 0; ///< Identity within one backend.
    /// Compares identities.
    bool operator==(const TransientHeapId&) const = default;
};
/// Owns heaps and placed objects; the pool owns layout and retirement policy.
/// Metadata lookups read cached values and perform no device call. The backend outlives its pool.
class TransientBackend {
public:
    /// Releases remaining views, resources and then heaps.
    virtual ~TransientBackend() = default;
    /// Ownership cannot be copied.
    TransientBackend(const TransientBackend&) = delete;
    /// Ownership cannot be assigned.
    TransientBackend& operator=(const TransientBackend&) = delete;
    /// Context frame number, after its beginFrame pacing wait.
    virtual uint64_t frameNumber() const = 0;
    /// Queries a valid texture descriptor during declaration, never compilation.
    virtual TransientFootprint footprint(const TransientTextureDesc&) const = 0;
    /// Queries the exact normal or indirect buffer descriptor during declaration.
    virtual TransientFootprint footprint(const TransientBufferDesc&,
                                         bool indirectArgs = false) const = 0;
    /// Creates a labelled GPU heap.
    virtual lmx::Result<TransientHeapId> createHeap(uint64_t bytes, std::string_view label) = 0;
    /// Destroys a heap after releasePlaced and retirement.
    virtual void destroyHeap(TransientHeapId heap) = 0;
    /// Creates a placed texture and all requested sampled/storage views.
    virtual lmx::Result<engine::TextureRef> placeTexture(TransientHeapId heap, uint64_t offset,
                                                         const TransientTextureDesc& desc,
                                                         std::string_view label) = 0;
    /// Creates a placed buffer using the same effective usage its footprint described.
    virtual lmx::Result<rojoRHI::Buffer> placeBuffer(TransientHeapId heap, uint64_t offset,
                                                     const TransientBufferDesc& desc,
                                                     std::string_view label,
                                                     bool indirectArgs = false) = 0;
    /// Returns an actually created storage view; invalid resource/mip is misuse.
    virtual rojoRHI::ViewIndex storageView(TransientHeapId heap, rojoRHI::Texture texture,
                                           uint32_t mip) const = 0;
    /// Returns the cached GPU address of a placed buffer.
    virtual rojoRHI::Addr address(TransientHeapId heap, rojoRHI::Buffer buffer) const = 0;
    /// Releases views before resources; the caller proves their last use retired.
    virtual void releasePlaced(TransientHeapId heap) = 0;

protected:
    /// Constructible only through an implementation.
    TransientBackend() = default;
};
} // namespace lmx::render
