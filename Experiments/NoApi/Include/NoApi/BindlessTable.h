//----------------------------------------------------------------------------------------------------------------------
/// @file BindlessTable.h
/// @brief Declares the single shader-visible table of texture and sampler handles.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "NoApi/Handles.h"
#include "NoApi/Result.h"
#include "NoApi/Texture.h"
#include "NoApi/Types.h"

#include <cstdint>
#include <string_view>

namespace lmx::experimental::noapi {

/// References a texture view by its slot in the bindless table.
///
/// Only `slot` is shader-visible; a shader indexes the table with it and may compute it from any
/// data it can read. `generation` is CPU-side validation state that makes use of a slot whose view
/// has been replaced or cleared a diagnosable error rather than a silent read of the wrong texture.
struct TextureHandle {
    uint32_t slot = kInvalidSlot; ///< Table slot index the shader uses.
    uint32_t generation = 0;      ///< CPU-side validation counter; not shader-visible.
};

/// References a sampler by its slot in the bindless table.
///
/// @copydoc TextureHandle
struct SamplerHandle {
    uint32_t slot = kInvalidSlot; ///< Table slot index the shader uses.
    uint32_t generation = 0;      ///< CPU-side validation counter; not shader-visible.
};

/// Describes the single bindless table a device owns.
struct BindlessTableDesc {
    uint32_t slotCount = 0; ///< Number of slots; must be non-zero and within the device capability.
    std::string_view label; ///< Debug label; must be non-empty.
};

/// Creates the device's bindless table.
///
/// A device owns at most one table; creating a second is a caller contract violation and asserts.
/// The table's storage is plain GPU memory whose address `bindlessTableAddress` returns, so a
/// shader reads it as an array and a caller may store slot indices anywhere it likes.
///
/// Fails with `ErrorCode::ResourceCreationFailed` when the device rejects the slot count.
Result<BindlessTable*> createBindlessTable(Device* device, const BindlessTableDesc& desc);

/// Destroys the bindless table.
///
/// Every submission referencing the table must be retired; this is a caller contract and asserts.
void destroyBindlessTable(Device* device, BindlessTable* table);

/// Returns the GPU address of the table's first slot.
///
/// Shaders index from this address. The stride between slots is
/// `Capabilities::bindlessSlotStride`, which is a device property rather than a fixed 32-bit index
/// width, because the shader-visible handle size differs by target.
GpuAddress bindlessTableAddress(const BindlessTable* table);

/// Returns the number of slots the table was created with.
uint32_t bindlessTableSlotCount(const BindlessTable* table);

/// Writes a texture view into `slot` and returns the handle that names it.
///
/// The write is immediate and CPU-side. When the table is read by a submission still in flight, or
/// when a preceding GPU pass wrote the table, the caller owes a barrier carrying
/// `Hazard::Descriptors`; this interface never inserts one.
///
/// Asserts when `slot` is out of range, when `view` exceeds the texture's mip or layer count, or
/// when `view.storage` is set for a texture created without `TextureUsage::Storage`.
TextureHandle writeTextureSlot(BindlessTable* table, uint32_t slot, const Texture* texture,
                               const TextureViewDesc& view);

/// Writes a sampler into `slot` and returns the handle that names it.
///
/// @copydetails writeTextureSlot
SamplerHandle writeSamplerSlot(BindlessTable* table, uint32_t slot, const Sampler* sampler);

/// Clears `slot` so that any handle naming it becomes invalid.
///
/// Reading a cleared slot from a shader is undefined; the generation bump only makes the CPU-side
/// misuse detectable.
void clearBindlessSlot(BindlessTable* table, uint32_t slot);

/// Bindless table write traffic since the table's creation (M5.1 spec section 9's binding-traffic
/// dimension: "bindless table writes and bytes").
struct BindlessTableStats {
    uint64_t writeCalls = 0; ///< Every writeTextureSlot/writeSamplerSlot call.
    uint64_t writeBytes = 0; ///< Bytes written, at `kBindlessSlotStride` (8) per slot.
};

/// Returns the table's cumulative write traffic since `createBindlessTable`.
BindlessTableStats bindlessTableStats(const BindlessTable* table);

} // namespace lmx::experimental::noapi
