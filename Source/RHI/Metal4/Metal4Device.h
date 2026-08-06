#pragma once
#include "RHI/Metal4/Metal4CommandList.h"
#include "RHI/Metal4/Metal4Common.h"
#include "RHI/RHI.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
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

    // Backend-internal, same role as the handle() on every resource wrapper: sibling Metal 4
    // files reach the native object through it (Metal4Capture needs the MTLDevice to name as a
    // capture object). Deliberately not on the RHI Device interface -- no Metal type appears in
    // RHI.h.
    MTL::Device* handle() const { return m_device.get(); }

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
    // One command buffer for the whole device, re-opened against this frame's allocator by
    // every beginFrame. In Metal 4 the recorded commands live in the *allocator*, not in the
    // command buffer, so the buffer is a reusable encoding handle and the ring of allocators
    // (plus the shared-event pacing) is what keeps a frame from overwriting in-flight work.
    NS::SharedPtr<MTL4::CommandBuffer> m_commandBuffer;
    // One argument table for the whole device, likewise. Its contents are read by the GPU while
    // a frame is in flight, so rebinding a *different* address per frame would need one table
    // per frame in flight; M1 binds the same static vertex buffer every frame, so one suffices.
    NS::SharedPtr<MTL4::ArgumentTable> m_argumentTable;
    // Not a member by value: Metal4CommandList's constructor needs the two objects above, which
    // do not exist until create() has run.
    std::optional<Metal4CommandList> m_commandList;

    uint64_t m_frameNumber = 0;
    // Guards the beginFrame/endFrame pairing; see the assertions in both.
    bool m_frameOpen = false;
};

} // namespace lmx::rhi::metal4
