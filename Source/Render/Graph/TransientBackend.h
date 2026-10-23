//----------------------------------------------------------------------------------------------------------------------
/// @file TransientBackend.h
/// @brief Declares the device interface the transient pool sizes, creates and places through.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Render/Graph/TransientResourceDesc.h"

#include <rojoRHI/RHI.h>

#include <cstdint>
#include <string_view>

namespace lmx::render {

/// What a transient costs in a placement heap: its size and the alignment its offset needs, both
/// in bytes, as the device that will place it answers them.
struct TransientFootprint {
    uint64_t size = 0;      ///< Bytes the placed resource occupies.
    uint64_t alignment = 0; ///< Alignment its offset in the heap requires.
    /// Compares both fields.
    friend bool operator==(const TransientFootprint&, const TransientFootprint&) = default;
};

/// Names one placement heap a TransientBackend created. The value means something only to the
/// backend that issued it, and only until that backend destroys the heap.
struct TransientHeapId {
    uint32_t value = 0; ///< Backend-assigned heap identity.
    /// Compares heap identity.
    friend bool operator==(TransientHeapId, TransientHeapId) = default;
};

/// The device operations a TransientPool needs and nothing else: what a descriptor costs, heaps,
/// and resources placed in them. The pool owns the policy (frame slots, heap generations, when a
/// heap may be released); a backend only carries it out on a device.
///
/// The backend owns every heap it creates and every resource it places. A placed resource lives
/// until releasePlaced() names its heap, and a heap until destroyHeap() names it; whatever is still
/// held when the backend is destroyed is released then, resources before their heaps.
class TransientBackend {
public:
    /// Releases whatever the backend still owns, resources before the heaps they sit in.
    virtual ~TransientBackend() = default;

    /// Non-copyable: a backend owns device objects that a copy would release twice.
    TransientBackend(const TransientBackend&) = delete;
    /// Non-assignable, for the reason the copy constructor is deleted.
    TransientBackend& operator=(const TransientBackend&) = delete;

    /// The device frame currently open, the number the pool's slot rotation is keyed by.
    virtual uint64_t frameNumber() const = 0;

    /// What placing a texture of `desc` costs on this device.
    virtual TransientFootprint footprint(const TransientTextureDesc& desc) const = 0;
    /// What placing a buffer of `desc` costs on this device.
    virtual TransientFootprint footprint(const TransientBufferDesc& desc) const = 0;

    /// Creates a placement heap of exactly `bytes`, labelled `label`, and returns its identity.
    virtual lmx::Result<TransientHeapId> createHeap(uint64_t bytes, std::string_view label) = 0;
    /// Destroys `heap`. Every resource placed in it must already have been released by
    /// releasePlaced(); the caller also proves no frame in flight still reads it.
    virtual void destroyHeap(TransientHeapId heap) = 0;

    /// Places a texture of `desc` at `offset` in `heap`, labelled `label`. The backend keeps the
    /// texture alive until releasePlaced() names `heap`; the pointer is borrowed until then.
    virtual lmx::Result<rojoRHI::Texture*> placeTexture(TransientHeapId heap, uint64_t offset,
                                                        const TransientTextureDesc& desc,
                                                        std::string_view label) = 0;
    /// The buffer counterpart of placeTexture, on the same lifetime terms.
    virtual lmx::Result<rojoRHI::Buffer*> placeBuffer(TransientHeapId heap, uint64_t offset,
                                                      const TransientBufferDesc& desc,
                                                      std::string_view label) = 0;
    /// Destroys every resource placed in `heap` since its last release. The heap itself stays and
    /// may be placed into again; the caller proves nothing in flight still reads those resources.
    virtual void releasePlaced(TransientHeapId heap) = 0;

protected:
    /// Constructible only as a base of an implementation.
    TransientBackend() = default;
};

} // namespace lmx::render
