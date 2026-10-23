//----------------------------------------------------------------------------------------------------------------------
/// @file GpuBuffer.h
/// @brief Declares the owner of one GPU buffer and the descriptor it was created with.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuOwned.h"

#include <rojoRHI/Buffer.h>
#include <rojoRHI/Device.h>

#include <memory>
#include <string>

namespace lmx::engine {

/// Owns one GPU buffer together with a copy of the descriptor it was created with.
///
/// The owner exposes no size getter by design: v2 buffers are handles without getters, so callers
/// read `desc()` instead. The descriptor's `label` views a string the owner holds, so it stays
/// valid after the caller's string dies and across moves. Phase B adds the buffer's GPU address
/// here. The owner must not outlive the device that created the buffer.
class GpuBuffer {
public:
    /// Constructs an empty owner.
    GpuBuffer() = default;

    /// Creates a buffer on `device` from `desc`, filled from `initial` (`desc.size` bytes) when
    /// given.
    static Result<GpuBuffer> create(rojoRHI::Device& device, const rojoRHI::BufferDesc& desc,
                                    const void* initial = nullptr);
    /// Takes ownership of a buffer created elsewhere from `desc`.
    static GpuBuffer adopt(std::unique_ptr<rojoRHI::Buffer> buffer,
                           const rojoRHI::BufferDesc& desc);

    /// Non-copyable: a GPU buffer has exactly one owner.
    GpuBuffer(const GpuBuffer&) = delete;
    /// Non-assignable, for the reason the copy constructor is deleted.
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    /// Moves the buffer and its descriptor, re-pointing the label at this owner's string.
    GpuBuffer(GpuBuffer&& other) noexcept;
    /// Releases the held buffer, then moves `other`'s, re-pointing the label at this owner.
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;
    /// Releases the owned buffer.
    ~GpuBuffer() = default;

    /// Returns the descriptor the buffer was created with; its label views this owner's string.
    const rojoRHI::BufferDesc& desc() const { return m_desc; }
    /// Returns the owned buffer; the owner must not be empty.
    rojoRHI::Buffer& rhi() const;
    /// Returns whether the owner holds a buffer.
    explicit operator bool() const { return static_cast<bool>(m_buffer); }

private:
    GpuBuffer(GpuOwned<rojoRHI::Buffer> buffer, const rojoRHI::BufferDesc& desc);

    GpuOwned<rojoRHI::Buffer> m_buffer;
    rojoRHI::BufferDesc m_desc;
    std::string m_label;
};

} // namespace lmx::engine
