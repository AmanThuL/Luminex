//----------------------------------------------------------------------------------------------------------------------
/// @file Memory.h
/// @brief Declares GPU memory allocation returning addresses rather than buffer objects.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/Result.h"
#include "NoApi/Types.h"

#include <cstdint>
#include <string_view>

namespace lmx::noapi {

/// Selects where an allocation lives and who may write it directly.
enum class MemoryKind : uint8_t {
    Shared,   ///< GPU memory the CPU can write directly through a mapped, write-combined pointer.
    Private,  ///< GPU-only memory; the CPU reaches it only through copy commands.
    Readback, ///< CPU-cached memory the GPU writes and the CPU reads after a completed submission.
};

/// Holds one memory allocation as an address pair plus its size.
///
/// `cpu` is the mapped host address and is null for `MemoryKind::Private`. `gpu` is the address the
/// GPU dereferences and is the only one that may be written into GPU-visible data structures.
/// Both are stable for the allocation's lifetime, so the translation is paid once at allocation
/// rather than at every use.
struct Allocation {
    void* cpu = nullptr;           ///< Mapped host address, or null when the CPU cannot write it.
    GpuAddress gpu = kNullAddress; ///< Address the GPU dereferences.
    uint64_t size = 0;             ///< Byte size of the allocation.
    MemoryKind kind = MemoryKind::Shared; ///< Memory kind the allocation was created with.
};

/// Describes a requested memory allocation.
struct AllocationDesc {
    uint64_t size = 0;                      ///< Byte size to allocate; must be non-zero.
    uint64_t alignment = kDefaultAlignment; ///< Required address alignment; must be a power of two.
    MemoryKind kind = MemoryKind::Shared;   ///< Where the allocation lives.
    std::string_view label;                 ///< Debug label; must be non-empty.
};

/// Allocates GPU memory and returns its address pair.
///
/// The allocation is suballocated by the caller: this interface has no buffer object, no bind
/// offset alignment query, and no per-use descriptor. Texture placement, root data rings, index
/// data, indirect arguments, and the bindless table are all suballocations of memory obtained here.
///
/// Fails with `ErrorCode::AllocationFailed` when the request cannot be satisfied and with
/// `ErrorCode::InvalidDesc` when the descriptor is internally inconsistent. A zero size, a
/// non-power-of-two alignment, or an empty label is a caller contract violation and asserts.
Result<Allocation> allocate(Device* device, const AllocationDesc& desc);

/// Releases an allocation obtained from `allocate`.
///
/// Every texture placed in the allocation must already be destroyed, and every submission that
/// referenced it must be retired. Both are caller contracts and are asserted.
void deallocate(Device* device, const Allocation& allocation);

} // namespace lmx::noapi
