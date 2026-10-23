//----------------------------------------------------------------------------------------------------------------------
/// @file GpuContext.h
/// @brief Declares the owner of the frame loop, idle waits, pass timings and shader loading.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Engine/Gpu/PassTiming.h"

#include <rojoRHI/RHI.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lmx::engine {

/// Selects optional behavior when creating a GPU context.
struct GpuContextDesc {
    bool validation = true; ///< Enables backend validation diagnostics.
};

/// One open frame: its number and the command list everything in it records into.
struct Frame {
    uint64_t number;                ///< Frame number, counted from one in beginFrame order.
    rojoRHI::CommandList& commands; ///< Command list of the open frame.
};

/// Owns the device's frame loop, idle waits, frame number, pass timings, upscaler support and
/// shader loading, so Luminex reaches those concerns in one place.
///
/// On RojoRHI v1 every body forwards to the one device call it replaces, in the same order the
/// callers issued it. The frame number is the device's own (no second counter); the switch to v2
/// adds a counter here when queues and fences replace the device's frame loop. Code that only
/// creates resources keeps a `rojoRHI::Device&`, which `device()` returns.
class GpuContext {
public:
    /// Creates a device and a context that owns it.
    static Result<std::unique_ptr<GpuContext>> create(const GpuContextDesc& desc);

    /// Wraps a device the caller owns; the device must outlive the context.
    explicit GpuContext(rojoRHI::Device& borrowed);

    /// Non-copyable: the context owns per-device state and possibly the device.
    GpuContext(const GpuContext&) = delete;
    /// Non-assignable, for the reason the copy constructor is deleted.
    GpuContext& operator=(const GpuContext&) = delete;

    /// Returns the device every resource is created on.
    rojoRHI::Device& device() const;

    /// Begins a paced frame and returns its number and command list.
    Frame beginFrame();
    /// Commits `frame` and presents the acquired texture of each swapchain in `present`.
    ///
    /// On RojoRHI v1 the device presents at most one swapchain per frame, so `present` holds zero
    /// or one; Debug builds check that, and that `frame` is the frame beginFrame opened last.
    void submitAndEndFrame(Frame& frame, std::span<rojoRHI::Swapchain* const> present = {});
    /// Blocks until all submitted work completes.
    void waitIdle();
    /// Returns the number of the newest frame beginFrame has opened; zero before the first.
    uint64_t frameNumber() const;

    /// Returns the pass timings of the newest retired frame; valid until the next beginFrame.
    PassTimingFrame passTimings();
    /// Returns the device's temporal upscaler support.
    UpscalerSupportInfo upscalerSupport() const;

    /// Loads the shader library `name` (a bare name such as `"Bloom"`) from the shader directory.
    Result<GpuOwned<rojoRHI::ShaderLibrary>> loadShaderLibrary(std::string_view name);

private:
    explicit GpuContext(std::unique_ptr<rojoRHI::Device> owned);

    std::unique_ptr<rojoRHI::Device> m_owned;
    rojoRHI::Device* m_device = nullptr;
    std::vector<PassTiming> m_timings;
};

/// Returns the path, relative to the working directory, the library `name` is loaded from.
std::string shaderLibraryPath(std::string_view name);

/// Replaces the rows of `destination` with copies of `source`, keeping its capacity.
void copyPassTimings(std::span<const rojoRHI::PassTiming> source,
                     std::vector<PassTiming>& destination);

/// Converts the device's temporal scaler support to the Luminex value.
UpscalerSupportInfo toUpscalerSupport(const rojoRHI::TemporalScalerSupport& support);

} // namespace lmx::engine
