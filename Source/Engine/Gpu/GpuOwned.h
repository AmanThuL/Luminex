//----------------------------------------------------------------------------------------------------------------------
/// @file GpuOwned.h
/// @brief Declares the owner of one GPU object and the adoption of a RojoRHI creation result.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuError.h"

#include <rojoRHI/Result.h>

#include <expected>
#include <memory>
#include <utility>

namespace lmx::engine {

/// Owns one GPU object for as long as the owner lives.
///
/// On RojoRHI v1 the device hands every object out as a `std::unique_ptr`, so the owner holds that
/// pointer. When the v2 objects arrive in phase B, the owner's type and lifetime do not change;
/// calls through `->` change with the v2 objects. The owner must not outlive the device that
/// created the object.
template <typename T>
class GpuOwned {
public:
    /// Constructs an empty owner.
    GpuOwned() = default;

    /// Takes ownership of `object`, which may be null.
    explicit GpuOwned(std::unique_ptr<T> object) : m_object(std::move(object)) {}

    /// Non-copyable: a GPU object has exactly one owner.
    GpuOwned(const GpuOwned&) = delete;
    /// Non-assignable, for the reason the copy constructor is deleted.
    GpuOwned& operator=(const GpuOwned&) = delete;
    /// Moves ownership, leaving `other` empty.
    GpuOwned(GpuOwned&& other) noexcept = default;
    /// Takes ownership from `other`, leaving it empty, then releases the previously held object.
    GpuOwned& operator=(GpuOwned&& other) noexcept = default;

    /// Returns the owned object, or null when the owner is empty.
    T* get() const { return m_object.get(); }
    /// Returns the owned object; the owner must not be empty.
    T& operator*() const { return *m_object; }
    /// Accesses the owned object; the owner must not be empty.
    T* operator->() const { return m_object.get(); }
    /// Returns whether the owner holds an object.
    explicit operator bool() const { return m_object != nullptr; }
    /// Releases the owned object, leaving the owner empty.
    void reset() { m_object.reset(); }

private:
    std::unique_ptr<T> m_object;
};

/// Adopts the object a RojoRHI creation call returned, or translates its error through `gpuError`.
template <typename T>
Result<GpuOwned<T>> adoptGpu(rojoRHI::Result<std::unique_ptr<T>>&& result) {
    if (!result) {
        return std::unexpected(gpuError(result.error()));
    }
    return GpuOwned<T>{std::move(*result)};
}

} // namespace lmx::engine
