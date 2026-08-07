#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace lmx::rhi {

// Deliberate omissions -- deferred until a real feature demands them, not designed against
// in advance (ADR 0004, "thin, explicit, honest"; spec §4):
//   - Compute
//   - Multi-queue
//   - Dynamic residency (everything lives in one MTLResidencySet)
//   - Queries
//   - Ray tracing
// Each grows in once a milestone actually needs it. "Explicit barriers" sat on this list through
// M1, which rendered a single pass and so had nothing to order; M2 renders scene-then-UI and grew
// exactly the one edge a real feature demands -- CommandList::textureBarrier -- and no barrier
// system.

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
    bool sampled = false;     // bound for shader reads after rendering (scene RT, shadow maps)
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

// How a texture is being used at a barrier boundary. Grows per real feature demand, exactly
// like the rest of this header (ADR 0004).
enum class TextureUse { RenderTarget, ShaderRead };

class ShaderLibrary {
public:
    virtual ~ShaderLibrary() = default;
};

struct GraphicsPipelineDesc {
    ShaderLibrary* library = nullptr;
    std::string_view vertexEntry;
    std::string_view fragmentEntry;
    Format colorFormat = Format::BGRA8Unorm;
    // Unknown = no depth attachment. Metal 4 pipelines carry no depth pixel format (it is a
    // render-pass property there) -- this field is validated CPU-side against the depth flags
    // and kept in the desc because the future Vulkan backend bakes it into the pipeline.
    Format depthFormat = Format::Unknown;
    bool depthTestEnable = false; // compare LESS when enabled
    bool depthWriteEnable = false;
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
    // Optional depth attachment. Cleared to clearDepth when set (load) and discarded after the
    // pass (store) -- M2 never reads depth back. Null = depth-less pass, as in M1.
    //
    // Requires clear == true: since no pass stores depth, loading it could only ever read
    // undefined memory. Setting depthTarget with clear == false is a caller error and asserts.
    Texture* depthTarget = nullptr;
    float clearDepth = 1.0f;
};

class CommandList {
public:
    virtual ~CommandList() = default;
    virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void bindPipeline(GraphicsPipeline& pipeline) = 0;
    virtual void bindVertexBuffer(uint32_t slot, Buffer& buffer) = 0; // argument-table slot
    // Binds a texture for shader reads at the given argument-table texture slot. Texture slots
    // are their own index space -- slot 0 here and buffer slot 0 coexist. Valid only inside a
    // render pass; the texture must have been created with sampled = true.
    virtual void bindTexture(uint32_t slot, Texture& texture) = 0;
    // Copies `size` bytes into the frame's transient uniform ring and binds the copy's GPU
    // address at the given argument-table slot for subsequent draws. The data is captured at
    // call time -- the caller may reuse or free its buffer immediately. Valid only inside a
    // render pass. Ring capacity is a fixed per-frame budget; exhausting it is fatal
    // (LMX_ASSERT) -- grow the backend constant when a real scene hits it.
    virtual void setUniforms(uint32_t slot, const void* data, uint64_t size) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    // Indexed draw. Indices are uint32 (the only index type this RHI models); the index buffer
    // is any Buffer holding them -- Metal 4 consumes it per-draw by GPU address, so there is no
    // separate index-buffer bind state. firstIndex is an element offset into the buffer.
    virtual void drawIndexed(Buffer& indexBuffer, uint32_t indexCount, uint32_t firstIndex = 0) = 0;
    virtual void endRenderPass() = 0;
    // Makes writes of `from` visible to reads of `to` for subsequent passes. Valid only
    // *between* render passes on this frame's command list. M2 supports the one edge a real
    // feature demands -- RenderTarget -> ShaderRead (scene RT sampled by the UI pass); any
    // other combination is a contract violation until a feature grows it.
    virtual void textureBarrier(Texture& texture, TextureUse from, TextureUse to) = 0;
};

struct SwapchainDesc {
    void* nativeLayer = nullptr; // CAMetalLayer* — created by the windowing layer
    uint32_t width = 0, height = 0;
    Format format = Format::BGRA8Unorm;
};
// Lifetime: a Swapchain must not outlive the Device that created it, and must be destroyed
// before the native surface it was built on. Destruction blocks until all GPU work referencing
// the swapchain has completed — a backend may not release presentation resources out from under
// in-flight command buffers — so no waitIdle() is required around it.
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
