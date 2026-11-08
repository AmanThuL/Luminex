//----------------------------------------------------------------------------------------------------------------------
/// @file GpuOwned.h
/// @brief Declares deferred handle ownership and GPU creation result adoption.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuError.h"

#include <rojoRHI/RHI.h>

#include <expected>
#include <type_traits>
#include <utility>

namespace lmx::engine {

/// Owns one handle and destroys it through its creating device's deferred release path.
/// The device and its GpuContext must outlive this owner. View and sampler index zero is valid.
template <typename T>
class GpuOwned {
public:
    /// Constructs an empty owner without a device.
    GpuOwned() = default;
    /// Takes ownership of a handle created on device; the handle may be empty.
    GpuOwned(rojoRHI::Device device, T object) : m_device(device), m_object(object) {}
    /// A GPU object has exactly one owner.
    GpuOwned(const GpuOwned&) = delete;
    /// A GPU object cannot acquire a second owner by assignment.
    GpuOwned& operator=(const GpuOwned&) = delete;
    /// Transfers the handle, leaving other empty.
    GpuOwned(GpuOwned&& other) noexcept
        : m_device(other.m_device), m_object(std::exchange(other.m_object, empty())) {}
    /// Releases this handle and transfers other's, leaving other empty.
    GpuOwned& operator=(GpuOwned&& other) noexcept {
        if (this != &other) {
            reset();
            m_device = other.m_device;
            m_object = std::exchange(other.m_object, empty());
        }
        return *this;
    }
    /// Releases the owned handle without waiting for native retirement.
    ~GpuOwned() { reset(); }
    /// Returns the owned value, or the type's empty sentinel.
    T get() const { return m_object; }
    /// Returns whether this owner holds a handle, including a valid index zero.
    explicit operator bool() const { return m_object != empty(); }
    /// Defers destruction once and leaves the owner empty.
    void reset() {
        if (*this) {
            m_device.destroy(std::exchange(m_object, empty()));
        }
    }

private:
    static constexpr T empty() {
        if constexpr (std::is_same_v<T, rojoRHI::ViewIndex> ||
                      std::is_same_v<T, rojoRHI::SamplerIndex>) {
            return T{~0u};
        } else {
            return T{};
        }
    }

    rojoRHI::Device m_device;
    T m_object = empty();
};

/// Adopts a creation result on device or preserves its causal error through gpuError.
template <typename T>
Result<GpuOwned<T>> adoptGpu(rojoRHI::Device device, rojoRHI::Result<T>&& result) {
    if (!result) {
        return std::unexpected(gpuError(result.error()));
    }
    return GpuOwned<T>{device, *result};
}

} // namespace lmx::engine
