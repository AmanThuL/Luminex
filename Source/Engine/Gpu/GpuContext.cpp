//----------------------------------------------------------------------------------------------------------------------
/// @file GpuContext.cpp
/// @brief Implements the GPU context by forwarding to the RojoRHI v1 device.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/GpuContext.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/GpuError.h"

#include <expected>
#include <utility>

namespace lmx::engine {

//======================================================================================================================
Result<std::unique_ptr<GpuContext>> GpuContext::create(const GpuContextDesc& desc) {
    auto device = rojoRHI::createDevice({.enableValidation = desc.validation});
    if (!device) {
        return std::unexpected(gpuError(device.error()));
    }
    return std::unique_ptr<GpuContext>(new GpuContext(std::move(*device)));
}

//======================================================================================================================
GpuContext::GpuContext(rojoRHI::Device& borrowed) : m_device(&borrowed) {}

//======================================================================================================================
GpuContext::GpuContext(std::unique_ptr<rojoRHI::Device> owned)
    : m_owned(std::move(owned)), m_device(m_owned.get()) {}

//======================================================================================================================
rojoRHI::Device& GpuContext::device() const {
    return *m_device;
}

//======================================================================================================================
Frame GpuContext::beginFrame() {
    rojoRHI::CommandList& list = device().beginFrame();
    return {device().frameNumber(), list};
}

//======================================================================================================================
void GpuContext::submitAndEndFrame([[maybe_unused]] Frame& frame,
                                   std::span<rojoRHI::Swapchain* const> present) {
#ifndef NDEBUG
    LMX_ASSERT(present.size() <= 1,
               "GpuContext::submitAndEndFrame: RojoRHI v1 presents at most one swapchain");
    LMX_ASSERT(frame.number == device().frameNumber(),
               "GpuContext::submitAndEndFrame: frame is not the one beginFrame opened last");
#endif
    device().endFrame(present.empty() ? nullptr : present.front());
}

//======================================================================================================================
void GpuContext::waitIdle() {
    device().waitIdle();
}

//======================================================================================================================
uint64_t GpuContext::frameNumber() const {
    return device().frameNumber();
}

//======================================================================================================================
PassTimingFrame GpuContext::passTimings() {
    copyPassTimings(device().passTimings(), m_timings);
    return {device().passTimingsFrame(), m_timings};
}

//======================================================================================================================
UpscalerSupportInfo GpuContext::upscalerSupport() const {
    return toUpscalerSupport(device().capabilities().temporalScaler);
}

//======================================================================================================================
Result<GpuOwned<rojoRHI::ShaderLibrary>> GpuContext::loadShaderLibrary(std::string_view name) {
    return adoptGpu(device().loadShaderLibrary(shaderLibraryPath(name)));
}

//======================================================================================================================
std::string shaderLibraryPath(std::string_view name) {
    return "Shaders/" + std::string(name);
}

//======================================================================================================================
void copyPassTimings(std::span<const rojoRHI::PassTiming> source,
                     std::vector<PassTiming>& destination) {
    destination.clear();
    for (const rojoRHI::PassTiming& timing : source) {
        destination.push_back({timing.label, timing.gpuMilliseconds});
    }
}

//======================================================================================================================
UpscalerSupportInfo toUpscalerSupport(const rojoRHI::TemporalScalerSupport& support) {
    return {.available = support.available,
            .minInputScale = support.minInputScale,
            .maxInputScale = support.maxInputScale,
            .name = support.name};
}

} // namespace lmx::engine
