//----------------------------------------------------------------------------------------------------------------------
/// @file GpuBuffer.h
/// @brief Declares buffer ownership, GPU addresses and mapped CPU access.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuOwned.h"

#include <rojoRHI/RHI.h>

#include <span>
#include <string>

namespace lmx::engine {

/// Owns a buffer and a descriptor whose label survives caller strings and owner moves.
/// The creating context must outlive the owner; CPU pointer access does not synchronize GPU work.
class GpuBuffer {
public:
    /// Constructs an empty owner.
    GpuBuffer() = default;
    /// Creates a buffer with optional initial bytes, returning the original creation error.
    static Result<GpuBuffer> create(rojoRHI::Device device, const rojoRHI::BufferDesc& desc,
                                    std::span<const std::byte> initial = {});
    /// A buffer has exactly one owner.
    GpuBuffer(const GpuBuffer&) = delete;
    /// A buffer cannot acquire a second owner by assignment.
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    /// Transfers the buffer and descriptor, preserving its owned label.
    GpuBuffer(GpuBuffer&& other) noexcept;
    /// Releases this buffer then transfers other's ownership.
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;
    /// Unregisters the resource and defers native destruction.
    ~GpuBuffer();
    /// Returns the creation descriptor; label borrows this owner's string.
    const rojoRHI::BufferDesc& desc() const { return m_desc; }
    /// Returns the owned handle; the owner must not be empty.
    rojoRHI::Buffer rhi() const;
    /// Returns a GPU address at a valid byte offset; memory must be Gpu or Upload.
    rojoRHI::Addr address(uint64_t offset = 0) const;
    /// Returns the mapped pointer for Upload or Readback; callers synchronize GPU use themselves.
    std::byte* cpuPointer() const;
    /// Returns whether this owner holds a buffer.
    explicit operator bool() const { return static_cast<bool>(m_buffer); }

private:
    void reset();

    GpuOwned<rojoRHI::Buffer> m_buffer;
    rojoRHI::Addr m_address{};
    std::byte* m_cpuPointer = nullptr;
    rojoRHI::BufferDesc m_desc;
    std::string m_label;
};

} // namespace lmx::engine
