//----------------------------------------------------------------------------------------------------------------------
/// @file GpuContext.h
/// @brief Declares device ownership, queue pacing, retired timings and capture.
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

/// Selects fixed device behavior; Release builds refuse validation with Unsupported.
struct GpuContextDesc {
    bool validation = false;     ///< Enables the validation tracker in Debug builds only.
    bool scopeTimings = true;    ///< Collects retired scope timestamps without timing stalls.
    uint32_t framesInFlight = 3; ///< Frame-slot count, either two or three.
    bool enableCapture = true;   ///< Requests platform capture support at device creation.
};

/// One open frame; commands borrows the context's list and expires on the next beginFrame.
struct Frame {
    uint64_t number; ///< Frame number counted from one, including frames without presentation.
    rojoRHI::CommandList& commands; ///< The open graphics command list.
};

/// Owns a device, graphics queue, pacing fences, shader loading and copied retired timings.
/// The context must outlive every GPU owner. Each acquired list must be submitted before endFrame;
/// all frame and capture operations occur on the renderer-owning thread.
class GpuContext {
public:
    /// Creates a device and both fences, preserving a causal creation failure.
    static Result<std::unique_ptr<GpuContext>> create(const GpuContextDesc& desc);
    /// Waits and destroys fences and device; callers must have closed the frame and released
    /// owners.
    ~GpuContext();
    /// Device ownership cannot be copied.
    GpuContext(const GpuContext&) = delete;
    /// Device ownership cannot be copied by assignment.
    GpuContext& operator=(const GpuContext&) = delete;
    /// Returns the resource-creation device, which lives as long as this context.
    rojoRHI::Device& device() const { return m_device; }
    /// Returns the context's graphics queue; callers serialize queue operations.
    rojoRHI::Queue graphicsQueue() const { return m_queue; }
    /// Opens one paced frame and acquires its graphics list, without opening a timing scope.
    Frame beginFrame();
    /// Ends/submits the current list once and signals its numbered frame fence.
    void submit(Frame& frame);
    /// Presents a prepared swapchain image after submission, while the frame remains open.
    void present(rojoRHI::Swapchain swapchain);
    /// Closes a submitted frame and copies the timing publication retired by its beginFrame.
    void endFrame(Frame& frame);
    /// Submits, presents each image and closes the frame; an empty image list is valid.
    void submitAndEndFrame(Frame& frame, std::span<const rojoRHI::Swapchain> present = {});
    /// Returns whether beginFrame has an unmatched endFrame.
    bool inFrame() const { return m_inFrame; }
    /// Waits for all prior graphics work using an empty submission; requires no open frame.
    void waitIdle();
    /// Waits for an already-submitted frame number, or zero for the initial fence value.
    void waitForFrame(uint64_t number);
    /// Returns the newest completed submitted frame without waiting.
    uint64_t completedFrame();
    /// Returns the last opened frame number, initially zero.
    uint64_t frameNumber() const { return m_frame; }
    /// Returns copied timing rows and their actual retired ID, valid until the next endFrame.
    PassTimingFrame passTimings() const;
    /// Returns the first advertised MetalFX scale range and its Luminex display name.
    UpscalerSupportInfo upscalerSupport() const;
    /// Returns whether this process/device can begin a capture.
    bool captureAvailable() const;
    /// Starts a capture and its schema record window, or returns the unchanged causal error.
    Result<void> beginCapture(std::string_view path);
    /// Ends this context's successful capture and writes its schema sidecar; logs sidecar I/O
    /// failure.
    void endCapture();
    /// Loads Shaders/name plus the selected backend extension as bytes; preserves load failures.
    Result<GpuOwned<rojoRHI::ShaderLibrary>> loadShaderLibrary(std::string_view name);

private:
    GpuContext(rojoRHI::Device device, rojoRHI::Fence frameFence, rojoRHI::Fence idleFence);
    void checkFrame(const Frame& frame) const;

    mutable rojoRHI::Device m_device;
    rojoRHI::Queue m_queue;
    rojoRHI::Fence m_frameFence;
    rojoRHI::Fence m_idleFence;
    rojoRHI::CommandList m_list;
    uint64_t m_frame = 0;
    uint64_t m_submittedFrame = 0;
    uint64_t m_idleValue = 0;
    bool m_inFrame = false;
    bool m_submitted = false;
    std::vector<PassTiming> m_timings;
    double m_frameGpuMs = 0.0;
    uint64_t m_timingFrame = 0;
    std::string m_capturePath;
    bool m_capturing = false;
};

/// Returns the shader-directory path with the backend-selected library extension.
std::string shaderLibraryPath(std::string_view name);
/// Copies depth-one rows beneath the graphics lmx.frame scope and its actual retired frame ID.
/// Clears outputs on absent frame scope, reuses destination capacity, and owns every copied label.
void collectPassTimings(std::span<const rojoRHI::ScopeTiming> source,
                        std::vector<PassTiming>& destination, double& frameGpuMs, uint64_t& frame);
/// Returns the first MetalFX range or an unavailable value; the name is static Luminex storage.
UpscalerSupportInfo toUpscalerSupport(std::span<const rojoRHI::UpscalerSupport> support);

} // namespace lmx::engine
