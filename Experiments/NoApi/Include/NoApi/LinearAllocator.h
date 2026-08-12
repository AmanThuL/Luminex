//----------------------------------------------------------------------------------------------------------------------
/// @file LinearAllocator.h
/// @brief Declares the bump allocator that turns one allocation into per-use address pairs.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Memory.h"
#include "NoApi/Types.h"

#include "Core/Align.h"
#include "Core/Assert.h"

#include <cstdint>
#include <type_traits>

namespace lmx::experimental::noapi {

/// Holds one suballocation as the address pair the caller writes and the GPU reads.
struct Suballocation {
    void* cpu = nullptr;           ///< Mapped host address to write through.
    GpuAddress gpu = kNullAddress; ///< Address to store in GPU-visible data or pass to a command.
    uint64_t size = 0;             ///< Byte size of the suballocation.
};

/// Suballocates one CPU-writable allocation linearly, returning address pairs.
///
/// This is the whole per-frame binding mechanism of the model: root blocks, index data, indirect
/// arguments, and bindless slot payloads are bump-allocated here and referenced by address. It is
/// caller-owned userland code with no driver involvement, so its cost is CPU pointer arithmetic
/// plus the caller's own writes.
///
/// Not thread-safe. Overflow is fatal rather than wrapping, so an undersized ring is diagnosed at
/// the offending allocation rather than silently corrupting live data.
class LinearAllocator {
public:
    /// Constructs an empty allocator that can only be reset or assigned.
    LinearAllocator() = default;

    /// Constructs an allocator over `storage`, which must be non-empty and CPU-writable.
    ///
    /// Asserts when `storage` has no mapped host address, which rules out `MemoryKind::Private`.
    explicit LinearAllocator(const Allocation& storage) : m_storage(storage) {
        LMX_ASSERT(storage.size > 0, "LinearAllocator requires non-empty storage");
        LMX_ASSERT(storage.cpu != nullptr, "LinearAllocator requires CPU-writable storage");
    }

    /// Suballocates `size` bytes aligned to `alignment` and returns the address pair.
    ///
    /// `alignment` must be a power of two. Exhausting the storage is a contract violation and
    /// asserts with the requested size, the alignment, and the remaining capacity.
    Suballocation allocate(uint64_t size, uint64_t alignment = kDefaultAlignment) {
        LMX_ASSERT(size > 0, "LinearAllocator allocation size must be non-zero");
        LMX_ASSERT(alignment > 0 && (alignment & (alignment - 1)) == 0,
                   "LinearAllocator alignment must be a power of two");
        const uint64_t offset = lmx::alignUp(m_used, alignment);
        LMX_ASSERT(offset + size <= m_storage.size, "LinearAllocator storage exhausted");
        m_used = offset + size;
        return Suballocation{static_cast<uint8_t*>(m_storage.cpu) + offset, m_storage.gpu + offset,
                             size};
    }

    /// Suballocates storage for `count` objects of type `T` with `T`'s natural alignment.
    ///
    /// `T` must be trivially copyable, because the caller writes it through a mapped
    /// write-combined pointer that the GPU reads without any further translation.
    template <typename T>
    Suballocation allocate(uint32_t count = 1) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "GPU-visible root data must be trivially copyable");
        return allocate(sizeof(T) * count, alignof(T));
    }

    /// Returns the allocator to its empty state, invalidating every prior suballocation.
    ///
    /// Callers reset only after proving that every submission referencing the storage is retired;
    /// `FrameRing` owns that proof for per-frame allocators.
    void reset() { m_used = 0; }

    /// Returns the bytes currently suballocated.
    uint64_t used() const { return m_used; }

    /// Returns the total bytes available in the underlying allocation.
    uint64_t capacity() const { return m_storage.size; }

    /// Returns the GPU address of the underlying allocation's first byte.
    GpuAddress base() const { return m_storage.gpu; }

private:
    Allocation m_storage{};
    uint64_t m_used = 0;
};

} // namespace lmx::experimental::noapi
