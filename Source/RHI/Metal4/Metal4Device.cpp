#include "RHI/Metal4/Metal4Device.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Validate.h"

#include <cstdlib>
#include <utility>

namespace lmx::rhi::metal4 {
namespace {

// Generous: any wait longer than this means the GPU is wedged, not busy.
constexpr uint64_t kWaitIdleTimeoutMs = 10'000;

std::string describe(NS::Error* error) {
    if (error == nullptr || error->localizedDescription() == nullptr) {
        return "no additional detail";
    }
    return error->localizedDescription()->utf8String();
}

std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

// Metal reads its validation-layer switch from the environment when the device is
// created, so this must run before MTL::CreateSystemDefaultDevice(). setenv is advisory:
// if the Metal framework already initialised (another library creating a device first)
// the variable is ignored, which is why the documented way to force validation on for a
// non-Xcode launch is `MTL_DEBUG_LAYER=1 ./App`. overwrite = 0 keeps an externally
// supplied value, so a developer can turn validation off without recompiling.
void requestMetalValidation() {
    ::setenv("MTL_DEBUG_LAYER", "1", 0);
}

} // namespace

Result<std::unique_ptr<Device>> Metal4Device::create(const DeviceDesc& desc) {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    if (desc.enableValidation) {
        requestMetalValidation();
    }

    auto self = std::unique_ptr<Metal4Device>(new Metal4Device());

    self->m_device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    if (!self->m_device) {
        return fail(ErrorCode::DeviceUnsupported, "no Metal device available on this system");
    }
    if (self->m_device->name() != nullptr) {
        self->m_deviceName = self->m_device->name()->utf8String();
    }

    if (!self->m_device->supportsFamily(MTL::GPUFamilyMetal4)) {
        return fail(ErrorCode::DeviceUnsupported,
                    "device '" + self->m_deviceName + "' does not support MTLGPUFamilyMetal4");
    }

    NS::Error* error = nullptr;

    {
        auto queueDesc = NS::TransferPtr(MTL4::CommandQueueDescriptor::alloc()->init());
        queueDesc->setLabel(makeString("lmx.device.queue").get());
        self->m_queue =
            NS::TransferPtr(self->m_device->newMTL4CommandQueue(queueDesc.get(), &error));
        if (!self->m_queue) {
            return fail(ErrorCode::DeviceUnsupported,
                        "failed to create MTL4 command queue: " + describe(error));
        }
    }

    {
        auto compilerDesc = NS::TransferPtr(MTL4::CompilerDescriptor::alloc()->init());
        compilerDesc->setLabel(makeString("lmx.device.compiler").get());
        self->m_compiler = NS::TransferPtr(self->m_device->newCompiler(compilerDesc.get(), &error));
        if (!self->m_compiler) {
            return fail(ErrorCode::DeviceUnsupported,
                        "failed to create MTL4 compiler: " + describe(error));
        }
    }

    {
        auto residencyDesc = NS::TransferPtr(MTL::ResidencySetDescriptor::alloc()->init());
        residencyDesc->setLabel(makeString("lmx.device.residency").get());
        self->m_residency =
            NS::TransferPtr(self->m_device->newResidencySet(residencyDesc.get(), &error));
        if (!self->m_residency) {
            return fail(ErrorCode::DeviceUnsupported,
                        "failed to create residency set: " + describe(error));
        }
        // Publish the (currently empty) allocation list, then let the queue keep it
        // resident for every command buffer it runs. Later tasks add allocations and
        // re-commit; the queue attachment is made once, here.
        self->m_residency->commit();
        self->m_queue->addResidencySet(self->m_residency.get());
    }

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        auto allocatorDesc = NS::TransferPtr(MTL4::CommandAllocatorDescriptor::alloc()->init());
        allocatorDesc->setLabel(makeString("lmx.device.allocator." + std::to_string(i)).get());
        self->m_allocators[i] =
            NS::TransferPtr(self->m_device->newCommandAllocator(allocatorDesc.get(), &error));
        if (!self->m_allocators[i]) {
            return fail(ErrorCode::DeviceUnsupported, "failed to create MTL4 command allocator " +
                                                          std::to_string(i) + ": " +
                                                          describe(error));
        }
    }

    self->m_frameEvent = NS::TransferPtr(self->m_device->newSharedEvent());
    if (!self->m_frameEvent) {
        return fail(ErrorCode::DeviceUnsupported, "failed to create frame-pacing shared event");
    }
    self->m_frameEvent->setLabel(makeString("lmx.device.frameEvent").get());
    self->m_frameEvent->setSignaledValue(0);
    self->m_frameNumber = 0;

    return self;
}

Metal4Device::~Metal4Device() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // Unwire before releasing: the queue holds a reference to the residency set, so drop
    // that edge explicitly rather than relying on teardown order inside Metal.
    if (m_queue && m_residency) {
        m_queue->removeResidencySet(m_residency.get());
    }
    // Everything else releases through the SharedPtr members, in reverse declaration
    // order (== reverse creation order). See the ownership note in Metal4Device.h.
}

Result<std::unique_ptr<Swapchain>> Metal4Device::createSwapchain(const SwapchainDesc& desc) {
    if (auto ok = validate(desc); !ok) {
        return std::unexpected(ok.error());
    }
    LMX_ASSERT(false, "Metal4Device::createSwapchain is implemented in Task 10");
    std::unreachable();
}

Result<std::unique_ptr<Buffer>>
Metal4Device::createBuffer(const BufferDesc& desc, [[maybe_unused]] const void* initialData) {
    if (auto ok = validate(desc); !ok) {
        return std::unexpected(ok.error());
    }
    LMX_ASSERT(false, "Metal4Device::createBuffer is implemented in Task 9");
    std::unreachable();
}

Result<std::unique_ptr<Texture>> Metal4Device::createTexture(const TextureDesc& desc) {
    if (auto ok = validate(desc); !ok) {
        return std::unexpected(ok.error());
    }
    LMX_ASSERT(false, "Metal4Device::createTexture is implemented in Task 9");
    std::unreachable();
}

Result<std::unique_ptr<ShaderLibrary>>
Metal4Device::loadShaderLibrary([[maybe_unused]] std::string_view pathNoExt) {
    LMX_ASSERT(false, "Metal4Device::loadShaderLibrary is implemented in Task 9");
    std::unreachable();
}

Result<std::unique_ptr<GraphicsPipeline>>
Metal4Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (auto ok = validate(desc); !ok) {
        return std::unexpected(ok.error());
    }
    LMX_ASSERT(false, "Metal4Device::createGraphicsPipeline is implemented in Task 9");
    std::unreachable();
}

CommandList& Metal4Device::beginFrame() {
    LMX_ASSERT(false, "Metal4Device::beginFrame is implemented in Task 10");
    std::unreachable();
}

void Metal4Device::endFrame([[maybe_unused]] Swapchain* presentTo) {
    LMX_ASSERT(false, "Metal4Device::endFrame is implemented in Task 10");
}

void Metal4Device::waitIdle() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // A throwaway event rather than m_frameEvent: the frame-pacing event's values are
    // owned by the frame loop (Task 10), and signalling an out-of-band value on it would
    // corrupt that sequence. The queue signals in submission order, so once this fires
    // every command buffer committed before it has completed.
    NS::SharedPtr<MTL::SharedEvent> done = NS::TransferPtr(m_device->newSharedEvent());
    LMX_ASSERT(done, "waitIdle: failed to create shared event");
    done->setLabel(makeString("lmx.device.waitIdle").get());
    done->setSignaledValue(0);

    m_queue->signalEvent(done.get(), 1);
    const bool signaled = done->waitUntilSignaledValue(1, kWaitIdleTimeoutMs);
    LMX_ASSERT(signaled, "waitIdle: GPU did not complete within the timeout");
}

} // namespace lmx::rhi::metal4

namespace lmx::rhi {

Result<std::unique_ptr<Device>> createDevice(const DeviceDesc& desc) {
    return metal4::Metal4Device::create(desc);
}

} // namespace lmx::rhi
