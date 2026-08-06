#include "RHI/Metal4/Metal4Capture.h"

#include "Core/Log.h"
#include "RHI/Metal4/Metal4Common.h"
#include "RHI/Metal4/Metal4Device.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace lmx::rhi::metal4 {
namespace {

// The document beginCapture() opened, so endCapture() can name it in the log. File-scope state
// mirrors the API it wraps: MTLCaptureManager is a process-wide singleton with exactly one
// capture open at a time, and this backend is single-threaded by construction.
std::string g_capturePath;

} // namespace

bool beginCapture(Device& device, std::string_view outPath) {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    MTL::CaptureManager* manager = MTL::CaptureManager::sharedCaptureManager();
    LMX_ASSERT(manager != nullptr, "beginCapture: no shared capture manager");

    // The expected failure, not an exceptional one: Metal reads MTL_CAPTURE_ENABLED once at
    // launch and refuses every programmatic capture in a process that started without it. The
    // message carries the fix because there is nothing the code can do about it at this point --
    // the process has to be restarted.
    if (!manager->supportsDestination(MTL::CaptureDestinationGPUTraceDocument)) {
        LMX_LOG_WARN("GPU capture unavailable: this process cannot write a .gputrace document. "
                     "Relaunch with capture enabled in the environment, e.g. "
                     "`MTL_CAPTURE_ENABLED=1 xmake run App`");
        return false;
    }
    if (manager->isCapturing()) {
        LMX_LOG_WARN("GPU capture already in progress; ignoring the request");
        return false;
    }

    // Absolute, because the URL outlives this call's notion of the working directory and a
    // relative one in a log line is useless to whoever has to find the document afterwards.
    std::error_code pathError;
    std::filesystem::path path = std::filesystem::absolute(outPath, pathError);
    if (pathError) {
        LMX_LOG_ERROR("GPU capture: cannot resolve output path '{}': {}", outPath,
                      pathError.message());
        return false;
    }

    // A .gputrace is a bundle *directory*, and Metal fails the capture outright rather than
    // overwriting one that already exists -- so a second capture in the same build directory
    // would silently never happen without this.
    std::error_code removeError;
    std::filesystem::remove_all(path, removeError);
    if (removeError) {
        LMX_LOG_ERROR("GPU capture: cannot remove the existing document at '{}': {}", path.string(),
                      removeError.message());
        return false;
    }

    auto url =
        NS::TransferPtr(NS::URL::alloc()->initFileURLWithPath(makeString(path.string()).get()));

    auto desc = NS::TransferPtr(MTL::CaptureDescriptor::alloc()->init());
    // Capturing the device rather than the queue: it is the widest scope Metal offers and it
    // needs no plumbing to reach the MTL4::CommandQueue, which is private to Metal4Device.
    // Every Device this backend hands out is a Metal4Device, so a foreign pointer is a caller
    // contract violation, not a runtime error path.
    desc->setCaptureObject(static_cast<Metal4Device&>(device).handle());
    desc->setDestination(MTL::CaptureDestinationGPUTraceDocument);
    desc->setOutputURL(url.get());

    NS::Error* error = nullptr;
    if (!manager->startCapture(desc.get(), &error)) {
        const NS::String* reason = error != nullptr ? error->localizedDescription() : nullptr;
        const char* utf8 = reason != nullptr ? reason->utf8String() : nullptr;
        LMX_LOG_ERROR("GPU capture failed to start: {}",
                      utf8 != nullptr ? utf8 : "no additional detail");
        return false;
    }

    g_capturePath = path.string();
    LMX_LOG_INFO("GPU capture started -> {}", g_capturePath);
    return true;
}

void endCapture() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    MTL::CaptureManager* manager = MTL::CaptureManager::sharedCaptureManager();
    LMX_ASSERT(manager != nullptr, "endCapture: no shared capture manager");
    if (!manager->isCapturing()) {
        return;
    }

    manager->stopCapture();
    LMX_LOG_INFO("GPU capture written: {} (open it with `open {}`)", g_capturePath, g_capturePath);
    g_capturePath.clear();
}

} // namespace lmx::rhi::metal4
