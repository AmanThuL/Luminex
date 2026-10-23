//----------------------------------------------------------------------------------------------------------------------
/// @file GpuBuffer.cpp
/// @brief Implements the buffer owner over the RojoRHI v1 device.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/GpuBuffer.h"

#include "Core/Diagnostics/Assert.h"

#include <expected>
#include <utility>

namespace lmx::engine {

//======================================================================================================================
Result<GpuBuffer> GpuBuffer::create(rojoRHI::Device& device, const rojoRHI::BufferDesc& desc,
                                    const void* initial) {
    auto buffer = adoptGpu(device.createBuffer(desc, initial));
    if (!buffer) {
        return std::unexpected(std::move(buffer.error()));
    }
    return GpuBuffer{std::move(*buffer), desc};
}

//======================================================================================================================
GpuBuffer GpuBuffer::adopt(std::unique_ptr<rojoRHI::Buffer> buffer,
                           const rojoRHI::BufferDesc& desc) {
    return GpuBuffer{GpuOwned<rojoRHI::Buffer>{std::move(buffer)}, desc};
}

//======================================================================================================================
GpuBuffer::GpuBuffer(GpuOwned<rojoRHI::Buffer> buffer, const rojoRHI::BufferDesc& desc)
    : m_buffer(std::move(buffer)), m_desc(desc), m_label(desc.label) {
    m_desc.label = m_label;
}

//======================================================================================================================
GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : m_buffer(std::move(other.m_buffer)), m_desc(other.m_desc), m_label(std::move(other.m_label)) {
    m_desc.label = m_label;
    other.m_desc = {};
    other.m_desc.label = other.m_label;
}

//======================================================================================================================
GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        m_buffer = std::move(other.m_buffer);
        m_desc = other.m_desc;
        m_label = std::move(other.m_label);
        m_desc.label = m_label;
        other.m_desc = {};
        other.m_desc.label = other.m_label;
    }
    return *this;
}

//======================================================================================================================
rojoRHI::Buffer& GpuBuffer::rhi() const {
    LMX_ASSERT(m_buffer, "GpuBuffer::rhi: the owner is empty");
    return *m_buffer;
}

} // namespace lmx::engine
