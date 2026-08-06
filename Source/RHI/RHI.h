#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace lmx::rhi {

enum class ErrorCode {
    DeviceUnsupported,
    ShaderLoadFailed,
    PipelineCreationFailed,
    ResourceCreationFailed,
    SwapchainFailed,
    InvalidDesc,
};
struct Error {
    ErrorCode code;
    std::string message;
};
template <typename T>
using Result = std::expected<T, Error>;

enum class Format { Unknown, BGRA8Unorm, RGBA8Unorm, D32Float };

struct BufferDesc {
    uint64_t size = 0;
    std::string_view label;
};
class Buffer {
public:
    virtual ~Buffer() = default;
    virtual uint64_t size() const = 0;
};

struct TextureDesc {
    uint32_t width = 0, height = 0;
    Format format = Format::BGRA8Unorm;
    bool renderTarget = false;
    bool cpuReadback = false; // shared storage; enables readback()
    std::string_view label;
};
class Texture {
public:
    virtual ~Texture() = default;
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
    // Blocking readback of the full texture (requires cpuReadback). out must hold
    // width*height*4 bytes for 8-bit formats. Caller ensures GPU work completed
    // (Device::waitIdle).
    virtual void readback(void* out, uint64_t outSize) = 0;
};

class ShaderLibrary {
public:
    virtual ~ShaderLibrary() = default;
};

struct GraphicsPipelineDesc {
    ShaderLibrary* library = nullptr;
    std::string_view vertexEntry;
    std::string_view fragmentEntry;
    Format colorFormat = Format::BGRA8Unorm;
    std::string_view label;
};
class GraphicsPipeline {
public:
    virtual ~GraphicsPipeline() = default;
};

struct RenderPassDesc {
    Texture* colorTarget = nullptr;
    float clearColor[4] = {0.f, 0.f, 0.f, 1.f};
    bool clear = true;
};

class CommandList {
public:
    virtual ~CommandList() = default;
    virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void bindPipeline(GraphicsPipeline& pipeline) = 0;
    virtual void bindVertexBuffer(uint32_t slot, Buffer& buffer) = 0; // argument-table slot
    virtual void draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void endRenderPass() = 0;
};

struct SwapchainDesc {
    void* nativeLayer = nullptr; // CAMetalLayer* — created by the windowing layer
    uint32_t width = 0, height = 0;
    Format format = Format::BGRA8Unorm;
};
class Swapchain {
public:
    virtual ~Swapchain() = default;
    virtual Result<Texture*> acquireNextTexture() = 0; // valid until endFrame/present
    virtual void resize(uint32_t width, uint32_t height) = 0;
};

struct DeviceDesc {
    bool enableValidation = true;
};
class Device {
public:
    virtual ~Device() = default;
    virtual Result<std::unique_ptr<Swapchain>> createSwapchain(const SwapchainDesc&) = 0;
    virtual Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc&,
                                                         const void* initialData) = 0;
    virtual Result<std::unique_ptr<Texture>> createTexture(const TextureDesc&) = 0;
    // pathNoExt: resolves "<pathNoExt>.metallib" (precompiled) first, else
    // "<pathNoExt>.metal" (runtime-compiled MSL, Metal 4 language version) — Amendment A1.
    virtual Result<std::unique_ptr<ShaderLibrary>>
    loadShaderLibrary(std::string_view pathNoExt) = 0;
    virtual Result<std::unique_ptr<GraphicsPipeline>>
    createGraphicsPipeline(const GraphicsPipelineDesc&) = 0;

    // Frame loop: beginFrame blocks on pacing (3 in flight), returns the frame CommandList.
    // endFrame commits; if presentTo != nullptr, presents its acquired texture.
    virtual CommandList& beginFrame() = 0;
    virtual void endFrame(Swapchain* presentTo) = 0;
    virtual void waitIdle() = 0;

    virtual std::string_view deviceName() const = 0;
};

// Factory — the only symbol the backend exports. Metal4 is the sole backend in M1.
Result<std::unique_ptr<Device>> createDevice(const DeviceDesc& desc = {});

} // namespace lmx::rhi
