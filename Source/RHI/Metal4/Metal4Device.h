#pragma once
#include "RHI/Metal4/Metal4Common.h"
#include "RHI/RHI.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace lmx::rhi::metal4 {

// Global Constraint: three frames in flight. Sizes the command-allocator ring and the
// lag the frame-pacing shared event enforces (Task 10 consumes both).
inline constexpr uint32_t kFramesInFlight = 3;

// Ownership convention for the whole Metal 4 backend: every metal-cpp object this class
// owns is held in an NS::SharedPtr obtained with NS::TransferPtr, because all the
// factories used here are `new*` methods that hand back a +1 reference. Members are
// therefore released in reverse declaration order -- which is reverse creation order --
// without a hand-written release sequence that could drift out of sync. The destructor
// body only performs the *unwiring* that has to happen before any release (detaching the
// residency set from the queue).
class Metal4Device final : public Device {
public:
    static Result<std::unique_ptr<Device>> create(const DeviceDesc& desc);

    ~Metal4Device() override;

    Metal4Device(const Metal4Device&) = delete;
    Metal4Device& operator=(const Metal4Device&) = delete;

    Result<std::unique_ptr<Swapchain>> createSwapchain(const SwapchainDesc& desc) override;
    Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc& desc,
                                                 const void* initialData) override;
    Result<std::unique_ptr<Texture>> createTexture(const TextureDesc& desc) override;
    Result<std::unique_ptr<ShaderLibrary>> loadShaderLibrary(std::string_view pathNoExt) override;
    Result<std::unique_ptr<GraphicsPipeline>>
    createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;

    CommandList& beginFrame() override;
    void endFrame(Swapchain* presentTo) override;
    void waitIdle() override;

    std::string_view deviceName() const override { return m_deviceName; }

private:
    Metal4Device() = default;

    // Owns the characters the deviceName() view points at -- MTL::Device::name()'s
    // utf8String() buffer is only valid while the autorelease pool that produced it lives.
    std::string m_deviceName;

    // Declaration order == creation order; see the ownership note above.
    NS::SharedPtr<MTL::Device> m_device;
    NS::SharedPtr<MTL4::CommandQueue> m_queue;
    NS::SharedPtr<MTL4::Compiler> m_compiler;
    NS::SharedPtr<MTL::ResidencySet> m_residency;
    std::array<NS::SharedPtr<MTL4::CommandAllocator>, kFramesInFlight> m_allocators;
    NS::SharedPtr<MTL::SharedEvent> m_frameEvent;
    uint64_t m_frameNumber = 0;
};

} // namespace lmx::rhi::metal4
