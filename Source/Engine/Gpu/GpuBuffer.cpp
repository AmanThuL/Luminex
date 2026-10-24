//----------------------------------------------------------------------------------------------------------------------
/// @file GpuBuffer.cpp
/// @brief Creates and retires buffers with owned descriptors and memory-class checks.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/GpuBuffer.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/CaptureSchema.h"

#include <expected>
#include <utility>

namespace lmx::engine {

//======================================================================================================================
Result<GpuBuffer> GpuBuffer::create(rojoRHI::Device device, const rojoRHI::BufferDesc& desc,
                                    std::span<const std::byte> initial) {
    auto buffer = adoptGpu(device, device.createBuffer(desc, initial));
    if (!buffer) {
        return std::unexpected(std::move(buffer.error()));
    }
    GpuBuffer result;
    result.m_buffer = std::move(*buffer);
    result.m_desc = desc;
    result.m_label = desc.label;
    result.m_desc.label = result.m_label;
    if (desc.memory != rojoRHI::Memory::Readback) {
        result.m_address = device.address(result.rhi());
    }
    if (desc.memory != rojoRHI::Memory::Gpu) {
        result.m_cpuPointer = device.cpuPointer(result.rhi());
    }
    registerCaptureResource(result.rhi(), result.m_desc);
    return result;
}

//======================================================================================================================
GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : m_buffer(std::move(other.m_buffer)), m_address(std::exchange(other.m_address, {})),
      m_cpuPointer(std::exchange(other.m_cpuPointer, nullptr)), m_desc(other.m_desc),
      m_label(std::move(other.m_label)) {
    m_desc.label = m_label;
    other.m_desc = {};
    other.m_desc.label = other.m_label;
}

//======================================================================================================================
GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        reset();
        m_buffer = std::move(other.m_buffer);
        m_address = std::exchange(other.m_address, {});
        m_cpuPointer = std::exchange(other.m_cpuPointer, nullptr);
        m_desc = other.m_desc;
        m_label = std::move(other.m_label);
        m_desc.label = m_label;
        other.m_desc = {};
        other.m_desc.label = other.m_label;
    }
    return *this;
}

//======================================================================================================================
GpuBuffer::~GpuBuffer() {
    reset();
}

//======================================================================================================================
void GpuBuffer::reset() {
    if (m_buffer) {
        unregisterCaptureResource(m_buffer.get());
        m_buffer.reset();
    }
}

//======================================================================================================================
rojoRHI::Buffer GpuBuffer::rhi() const {
    LMX_ASSERT(m_buffer, "GpuBuffer::rhi: the owner is empty");
    return m_buffer.get();
}

//======================================================================================================================
rojoRHI::Addr GpuBuffer::address(uint64_t offset) const {
    LMX_ASSERT(m_desc.memory != rojoRHI::Memory::Readback,
               "GpuBuffer::address: Readback memory has no GPU address");
    LMX_ASSERT(offset < m_desc.size, "GpuBuffer::address: offset is outside the buffer");
    LMX_ASSERT(m_buffer, "GpuBuffer::rhi: the owner is empty");
    return m_address + offset;
}

//======================================================================================================================
std::byte* GpuBuffer::cpuPointer() const {
    LMX_ASSERT(m_desc.memory == rojoRHI::Memory::Upload ||
                   m_desc.memory == rojoRHI::Memory::Readback,
               "GpuBuffer::cpuPointer: GPU memory is not mapped");
    LMX_ASSERT(m_buffer, "GpuBuffer::rhi: the owner is empty");
    return m_cpuPointer;
}

} // namespace lmx::engine
