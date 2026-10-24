//----------------------------------------------------------------------------------------------------------------------
/// @file GpuContext.cpp
/// @brief Implements queue and fence frame pacing with owned retired diagnostics.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Gpu/GpuContext.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/IO/File.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Engine/Gpu/GpuError.h"

#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <utility>

namespace lmx::engine {

//======================================================================================================================
Result<std::unique_ptr<GpuContext>> GpuContext::create(const GpuContextDesc& desc) {
    auto device = rojoRHI::createDevice({.validation = desc.validation,
                                         .enableCapture = desc.enableCapture,
                                         .scopeTimings = desc.scopeTimings,
                                         .framesInFlight = desc.framesInFlight,
                                         .label = "lmx.device"});
    if (!device) {
        return std::unexpected(gpuError(device.error()));
    }
    auto frameFence = device->createFence(0, "lmx.frameFence");
    if (!frameFence) {
        rojoRHI::destroyDevice(*device);
        return std::unexpected(gpuError(frameFence.error()));
    }
    auto idleFence = device->createFence(0, "lmx.idleFence");
    if (!idleFence) {
        device->destroy(*frameFence);
        rojoRHI::destroyDevice(*device);
        return std::unexpected(gpuError(idleFence.error()));
    }
    return std::unique_ptr<GpuContext>(new GpuContext(*device, *frameFence, *idleFence));
}

//======================================================================================================================
GpuContext::GpuContext(rojoRHI::Device device, rojoRHI::Fence frameFence, rojoRHI::Fence idleFence)
    : m_device(device), m_queue(device.queue(rojoRHI::QueueType::Graphics)),
      m_frameFence(frameFence), m_idleFence(idleFence) {}

//======================================================================================================================
GpuContext::~GpuContext() {
    if (m_capturing) {
        endCapture();
    }
    LMX_ASSERT(!inFrame(), "GpuContext: cannot destroy a context with an open frame");
    waitIdle();
    m_device.destroy(m_frameFence);
    m_device.destroy(m_idleFence);
    rojoRHI::destroyDevice(m_device);
}

//======================================================================================================================
Frame GpuContext::beginFrame() {
    LMX_ASSERT(!inFrame(), "GpuContext::beginFrame: a frame is already open");
    ++m_frame;
    m_device.beginFrame();
    m_list = m_device.acquireCommandList(rojoRHI::QueueType::Graphics, "lmx.frame");
    m_inFrame = true;
    m_submitted = false;
    return {m_frame, m_list};
}

//======================================================================================================================
void GpuContext::checkFrame(const Frame& frame) const {
    LMX_ASSERT(inFrame() && frame.number == m_frame && &frame.commands == &m_list,
               "GpuContext::submitAndEndFrame: frame is not the one beginFrame opened last");
}

//======================================================================================================================
void GpuContext::submit(Frame& frame) {
    checkFrame(frame);
    LMX_ASSERT(!m_submitted, "GpuContext::submit: the frame list was already submitted");
    m_list.end();
    const std::array signals{rojoRHI::FenceValue{m_frameFence, frame.number}};
    m_queue.submit(std::span(&m_list, 1), {}, signals);
    m_submitted = true;
    m_submittedFrame = frame.number;
}

//======================================================================================================================
void GpuContext::present(rojoRHI::Swapchain swapchain) {
    LMX_ASSERT(inFrame() && m_submitted, "GpuContext::present: the frame must be submitted");
    m_queue.present(swapchain);
}

//======================================================================================================================
void GpuContext::endFrame(Frame& frame) {
    checkFrame(frame);
    LMX_ASSERT(m_submitted, "GpuContext::endFrame: the frame list must be submitted");
    m_device.endFrame();
    m_inFrame = false;
    collectPassTimings(m_device.scopeTimings(), m_timings, m_frameGpuMs, m_timingFrame);
}

//======================================================================================================================
void GpuContext::submitAndEndFrame(Frame& frame,
                                   std::span<const rojoRHI::Swapchain> presentImages) {
    submit(frame);
    for (auto image : presentImages) {
        present(image);
    }
    endFrame(frame);
}

//======================================================================================================================
void GpuContext::waitIdle() {
    LMX_ASSERT(!inFrame(), "GpuContext::waitIdle: a frame is open");
    const rojoRHI::FenceValue signal{m_idleFence, ++m_idleValue};
    m_queue.submit({}, {}, std::span(&signal, 1));
    LMX_ASSERT(m_device.wait(signal, std::numeric_limits<uint64_t>::max()),
               "GpuContext::waitIdle: an unlimited fence wait failed");
}

//======================================================================================================================
void GpuContext::waitForFrame(uint64_t number) {
    LMX_ASSERT(number <= m_submittedFrame,
               "GpuContext::waitForFrame: frame has not been submitted");
    LMX_ASSERT(m_device.wait({m_frameFence, number}, std::numeric_limits<uint64_t>::max()),
               "GpuContext::waitForFrame: an unlimited fence wait failed");
}

//======================================================================================================================
uint64_t GpuContext::completedFrame() {
    return m_device.completedValue(m_frameFence);
}

//======================================================================================================================
PassTimingFrame GpuContext::passTimings() const {
    return {m_timingFrame, m_timings, m_frameGpuMs};
}

//======================================================================================================================
UpscalerSupportInfo GpuContext::upscalerSupport() const {
    return toUpscalerSupport(m_device.capabilities().upscalers);
}

//======================================================================================================================
bool GpuContext::captureAvailable() const {
    return m_device.capabilities().capture;
}

//======================================================================================================================
Result<void> GpuContext::beginCapture(std::string_view path) {
    LMX_ASSERT(!m_capturing, "GpuContext::beginCapture: a capture is already active");
    LMX_ASSERT(!path.empty(), "GpuContext::beginCapture: output path must not be empty");
    std::error_code error;
    const auto absolutePath = std::filesystem::absolute(path, error);
    if (error) {
        return std::unexpected(
            Error{ErrorCode::Io, "Cannot resolve output path: " + error.message()});
    }
    auto result = m_device.beginCapture(absolutePath.string());
    if (!result) {
        return std::unexpected(gpuError(result.error()));
    }
    m_capturePath = absolutePath.string();
    beginCaptureFrameRecords();
    m_capturing = true;
    return {};
}

//======================================================================================================================
void GpuContext::endCapture() {
    LMX_ASSERT(m_capturing, "GpuContext::endCapture: no capture is active");
    m_device.endCapture();
    endCaptureFrameRecords();
    writeCaptureSchema(m_capturePath + ".schema.json");
    m_capturePath.clear();
    m_capturing = false;
}

//======================================================================================================================
Result<GpuOwned<rojoRHI::ShaderLibrary>> GpuContext::loadShaderLibrary(std::string_view name) {
    const std::string path = shaderLibraryPath(name);
    auto bytes = readWholeFile(path);
    if (!bytes) {
        if (bytes.error() == FileReadError::Open) {
            std::error_code error;
            if (!std::filesystem::exists(path, error) && !error) {
                return std::unexpected(
                    Error{ErrorCode::NotFound,
                          std::format("shader library not found: tried '{}'", path)});
            }
        }
        return std::unexpected(
            Error{ErrorCode::Io, std::format("failed to read shader library '{}'", path)});
    }
    if (bytes->empty()) {
        return std::unexpected(
            Error{ErrorCode::Parse, std::format("shader library '{}' is empty", path)});
    }
    return adoptGpu(m_device, m_device.loadShaderLibrary(*bytes, name));
}

//======================================================================================================================
std::string shaderLibraryPath(std::string_view name) {
    return "Shaders/" + std::string(name) + LMX_SHADER_LIBRARY_EXTENSION;
}

//======================================================================================================================
void collectPassTimings(std::span<const rojoRHI::ScopeTiming> source,
                        std::vector<PassTiming>& destination, double& frameGpuMs, uint64_t& frame) {
    size_t count = 0;
    frameGpuMs = 0.0;
    frame = 0;
    bool inFrameScope = false;
    for (const auto& row : source) {
        if (row.queue != rojoRHI::QueueType::Graphics) {
            continue;
        }
        if (row.depth == 0) {
            inFrameScope = row.label == "lmx.frame";
            if (inFrameScope) {
                frame = row.frame;
                frameGpuMs = row.gpuMilliseconds;
            }
        } else if (inFrameScope && row.frame == frame && row.depth == 1) {
            if (count == destination.size()) {
                destination.emplace_back();
            }
            destination[count].label.assign(row.label);
            destination[count].gpuMilliseconds = row.gpuMilliseconds;
            ++count;
        }
    }
    destination.resize(count);
}

//======================================================================================================================
UpscalerSupportInfo toUpscalerSupport(std::span<const rojoRHI::UpscalerSupport> support) {
    for (const auto& entry : support) {
        if (entry.kind == rojoRHI::UpscalerKind::MetalFX) {
            return {true, entry.minInputScale, entry.maxInputScale, "MetalFX Temporal"};
        }
    }
    return {};
}

} // namespace lmx::engine
