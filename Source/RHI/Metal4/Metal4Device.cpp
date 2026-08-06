#include "RHI/Metal4/Metal4Device.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Metal4/Metal4Resources.h"
#include "RHI/Metal4/Metal4Swapchain.h"
#include "RHI/Validate.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace lmx::rhi::metal4 {
namespace {

// Generous: any wait longer than this means the GPU is wedged, not busy. Shared by waitIdle()
// and by beginFrame's pacing wait, which have the same "this should never actually elapse"
// character.
constexpr uint64_t kGpuTimeoutMs = 10'000;

// Sized for M1's single vertex buffer with room to grow before anyone has to think about it;
// the table is a fixed-size allocation, so the cost of the slack is a few dozen bytes.
constexpr NS::UInteger kMaxBufferBindCount = 8;
constexpr NS::UInteger kMaxTextureBindCount = 8;

// Highest 4.x the vendored metal-cpp headers expose (MTLLibrary.hpp: LanguageVersion4_0 is
// the last entry of the enum). Runtime-compiled MSL must match what the offline `metal
// -std=metal4.0` step in the slang2metallib rule produces, so both Amendment-A1 branches
// yield the same language semantics.
constexpr MTL::LanguageVersion kShaderLanguageVersion = MTL::LanguageVersion4_0;

// NS::String::utf8String() is documented to be able to return null, so every conversion
// back to std::string goes through here rather than feeding a possibly-null char* to the
// std::string constructor (which would be UB).
std::string toStdString(const NS::String* text, std::string fallback = {}) {
    if (text == nullptr) {
        return fallback;
    }
    const char* utf8 = text->utf8String();
    return utf8 != nullptr ? std::string(utf8) : fallback;
}

std::string describe(NS::Error* error) {
    if (error == nullptr) {
        return "no additional detail";
    }
    return toStdString(error->localizedDescription(), "no additional detail");
}

std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

std::vector<std::string> libraryFunctionNames(MTL::Library* library) {
    std::vector<std::string> names;
    NS::Array* array = library->functionNames();
    if (array == nullptr) {
        return names;
    }
    names.reserve(array->count());
    for (NS::UInteger i = 0; i < array->count(); ++i) {
        names.push_back(toStdString(array->object<NS::String>(i)));
    }
    return names;
}

const char* describe(MTL::FunctionType type) {
    switch (type) {
    case MTL::FunctionTypeVertex:
        return "vertex";
    case MTL::FunctionTypeFragment:
        return "fragment";
    case MTL::FunctionTypeKernel:
        return "kernel";
    case MTL::FunctionTypeVisible:
        return "visible";
    case MTL::FunctionTypeIntersection:
        return "intersection";
    case MTL::FunctionTypeMesh:
        return "mesh";
    case MTL::FunctionTypeObject:
        return "object";
    }
    return "unknown";
}

std::string join(const std::vector<std::string>& items) {
    if (items.empty()) {
        return "<none>";
    }
    std::string joined;
    for (const std::string& item : items) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += item;
    }
    return joined;
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

// Reads the whole file as bytes. Returns nullopt if it cannot be opened or read; the caller
// turns that into a ShaderLoadFailed naming the path.
std::optional<std::string> readTextFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::string contents((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());
    if (stream.bad()) {
        return std::nullopt;
    }
    return contents;
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
    self->m_deviceName = toStdString(self->m_device->name(), "<unnamed device>");

    if (!self->m_device->supportsFamily(MTL::GPUFamilyMetal4)) {
        return fail(ErrorCode::DeviceUnsupported,
                    "device '" + self->m_deviceName + "' does not support MTLGPUFamilyMetal4");
    }

    // One error slot for the whole sequence; each call below resets it first, because Metal
    // only *writes* it on failure and a stale pointer from an earlier call would otherwise
    // be reported against a later one.
    NS::Error* error = nullptr;

    {
        auto queueDesc = NS::TransferPtr(MTL4::CommandQueueDescriptor::alloc()->init());
        queueDesc->setLabel(makeString("lmx.device.queue").get());
        error = nullptr;
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
        error = nullptr;
        self->m_compiler = NS::TransferPtr(self->m_device->newCompiler(compilerDesc.get(), &error));
        if (!self->m_compiler) {
            return fail(ErrorCode::DeviceUnsupported,
                        "failed to create MTL4 compiler: " + describe(error));
        }
    }

    {
        auto residencyDesc = NS::TransferPtr(MTL::ResidencySetDescriptor::alloc()->init());
        residencyDesc->setLabel(makeString("lmx.device.residency").get());
        error = nullptr;
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
        error = nullptr;
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

    self->m_commandBuffer = NS::TransferPtr(self->m_device->newCommandBuffer());
    if (!self->m_commandBuffer) {
        return fail(ErrorCode::DeviceUnsupported, "failed to create MTL4 command buffer");
    }
    self->m_commandBuffer->setLabel(makeString("lmx.device.commandBuffer").get());

    {
        auto tableDesc = NS::TransferPtr(MTL4::ArgumentTableDescriptor::alloc()->init());
        tableDesc->setMaxBufferBindCount(kMaxBufferBindCount);
        tableDesc->setMaxTextureBindCount(kMaxTextureBindCount);
        // Zero-fills the unbound slots. Without it a slot a shader declares but nobody binds
        // holds whatever was in the allocation, which is a garbage GPU address rather than a
        // diagnosable null.
        tableDesc->setInitializeBindings(true);
        tableDesc->setLabel(makeString("lmx.device.argumentTable").get());
        error = nullptr;
        self->m_argumentTable =
            NS::TransferPtr(self->m_device->newArgumentTable(tableDesc.get(), &error));
        if (!self->m_argumentTable) {
            return fail(ErrorCode::DeviceUnsupported,
                        "failed to create MTL4 argument table: " + describe(error));
        }
    }

    self->m_commandList.emplace(self->m_commandBuffer.get(), self->m_argumentTable.get());

    return self;
}

Metal4Device::~Metal4Device() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // Drain first, unwire second, release third. The last committed frame may still be running
    // on the GPU, and it references the command allocator and the frame event that the member
    // releases below are about to drop. create() bails out before m_queue exists on some paths,
    // hence the guard.
    if (m_queue) {
        waitIdle();
    }

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
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // The windowing layer created and owns the CAMetalLayer; RetainPtr takes a second reference
    // so the swapchain cannot be left holding a dangling pointer if the window is torn down
    // out of order. Only the layer's *configuration* below is ours.
    NS::SharedPtr<CA::MetalLayer> layer =
        NS::RetainPtr(static_cast<CA::MetalLayer*>(desc.nativeLayer));

    layer->setDevice(m_device.get());
    layer->setPixelFormat(toMTL(desc.format));
    layer->setDrawableSize(
        CGSize{static_cast<CGFloat>(desc.width), static_cast<CGFloat>(desc.height)});
    // We only ever render into the drawable and present it -- never sample or read it back --
    // and telling Core Animation so lets it hand out textures with the cheapest layout.
    layer->setFramebufferOnly(true);

    // Divergence check recorded for Task 11: the vendored CAMetalLayer.hpp *does* expose
    // residencySet() (a getter only). Null-guarded anyway, since it is documented as vending a
    // set only once the layer has a device.
    MTL::ResidencySet* layerResidency = layer->residencySet();
    if (layerResidency == nullptr) {
        LMX_LOG_WARN("CAMetalLayer vends no residency set; relying on Metal's default drawable "
                     "residency handling");
    }

    return std::make_unique<Metal4Swapchain>(std::move(layer), m_queue, layerResidency);
}

Result<std::unique_ptr<Buffer>> Metal4Device::createBuffer(const BufferDesc& desc,
                                                           const void* initialData) {
    if (auto ok = validate(desc); !ok) {
        return std::unexpected(ok.error());
    }
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // Shared storage unconditionally: on Apple Silicon CPU and GPU share one physical pool,
    // so Shared costs nothing over Private and keeps every buffer CPU-writable for the
    // upload-on-create path below and for later per-frame updates.
    NS::SharedPtr<MTL::Buffer> buffer =
        NS::TransferPtr(m_device->newBuffer(desc.size, MTL::ResourceStorageModeShared));
    if (!buffer) {
        return fail(ErrorCode::ResourceCreationFailed,
                    "failed to create buffer of " + std::to_string(desc.size) + " bytes");
    }
    if (initialData != nullptr) {
        std::memcpy(buffer->contents(), initialData, desc.size);
    }
    if (!desc.label.empty()) {
        buffer->setLabel(makeString(desc.label).get());
    }

    // Passing the residency set registers the allocation and, just as importantly, unregisters
    // it when the wrapper dies -- see ResidencyRegistration.
    return std::make_unique<Metal4Buffer>(std::move(buffer), m_residency);
}

Result<std::unique_ptr<Texture>> Metal4Device::createTexture(const TextureDesc& desc) {
    if (auto ok = validate(desc); !ok) {
        return std::unexpected(ok.error());
    }
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    auto textureDesc = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
    textureDesc->setTextureType(MTL::TextureType2D);
    textureDesc->setPixelFormat(toMTL(desc.format));
    textureDesc->setWidth(desc.width);
    textureDesc->setHeight(desc.height);
    textureDesc->setMipmapLevelCount(1);
    // ShaderRead is always on: it is free here and every texture we create is either
    // sampled or resolved at some point. RenderTarget is the one usage that constrains
    // allocation, so it stays opt-in via the desc.
    textureDesc->setUsage(desc.renderTarget
                              ? MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget
                              : MTL::TextureUsageShaderRead);
    // Shared keeps the pixels in CPU-addressable memory so getBytes() can read them without
    // a blit; Private lets the driver pick an optimal (possibly compressed) layout.
    textureDesc->setStorageMode(desc.cpuReadback ? MTL::StorageModeShared
                                                 : MTL::StorageModePrivate);

    NS::SharedPtr<MTL::Texture> texture = NS::TransferPtr(m_device->newTexture(textureDesc.get()));
    if (!texture) {
        return fail(ErrorCode::ResourceCreationFailed,
                    "failed to create " + std::to_string(desc.width) + "x" +
                        std::to_string(desc.height) + " texture");
    }
    if (!desc.label.empty()) {
        texture->setLabel(makeString(desc.label).get());
    }

    return std::make_unique<Metal4Texture>(std::move(texture), desc.width, desc.height,
                                           desc.cpuReadback, m_residency);
}

Result<std::unique_ptr<ShaderLibrary>> Metal4Device::loadShaderLibrary(std::string_view pathNoExt) {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // pathNoExt is a string_view and need not be NUL-terminated, so it is copied before it
    // reaches any C-string API.
    const std::string base(pathNoExt);
    const std::filesystem::path libraryPath(base + ".metallib");
    const std::filesystem::path sourcePath(base + ".metal");

    NS::Error* error = nullptr;

    // Amendment A1: prefer the offline-compiled metallib, fall back to compiling the
    // readable MSL at runtime. Both artifacts are emitted by the slang2metallib rule (the
    // metallib only when the Metal toolchain is installed), so which branch runs depends on
    // the machine that built the target, not on this code.
    // The error_code overloads are the non-throwing ones. The code itself is deliberately
    // not inspected: "cannot stat it" is handled exactly like "not there", and the
    // not-found failure at the bottom names both candidate paths either way.
    std::error_code statError;
    if (std::filesystem::is_regular_file(libraryPath, statError)) {
        NS::SharedPtr<NS::URL> url = NS::TransferPtr(
            NS::URL::alloc()->initFileURLWithPath(makeString(libraryPath.string()).get()));
        NS::SharedPtr<MTL::Library> library =
            NS::TransferPtr(m_device->newLibrary(url.get(), &error));
        if (!library) {
            return fail(ErrorCode::ShaderLoadFailed, "failed to load precompiled library '" +
                                                         libraryPath.string() +
                                                         "': " + describe(error));
        }
        library->setLabel(makeString(base).get());
        // Which branch ran is otherwise unobservable through the RHI interface, and it is
        // the first thing worth knowing when shader behaviour differs between machines.
        LMX_LOG_INFO("shader library '{}': loaded precompiled metallib", base);
        return std::make_unique<Metal4ShaderLibrary>(std::move(library));
    }

    if (std::filesystem::is_regular_file(sourcePath, statError)) {
        const std::optional<std::string> source = readTextFile(sourcePath);
        if (!source) {
            return fail(ErrorCode::ShaderLoadFailed,
                        "failed to read shader source '" + sourcePath.string() + "'");
        }
        auto options = NS::TransferPtr(MTL::CompileOptions::alloc()->init());
        options->setLanguageVersion(kShaderLanguageVersion);

        error = nullptr;
        NS::SharedPtr<MTL::Library> library =
            NS::TransferPtr(m_device->newLibrary(makeString(*source).get(), options.get(), &error));
        if (!library) {
            return fail(ErrorCode::ShaderLoadFailed, "failed to compile shader source '" +
                                                         sourcePath.string() +
                                                         "': " + describe(error));
        }
        library->setLabel(makeString(base).get());
        LMX_LOG_INFO("shader library '{}': compiled MSL source at runtime", base);
        return std::make_unique<Metal4ShaderLibrary>(std::move(library));
    }

    return fail(ErrorCode::ShaderLoadFailed, "shader library not found: tried '" +
                                                 libraryPath.string() + "' and '" +
                                                 sourcePath.string() + "'");
}

Result<std::unique_ptr<GraphicsPipeline>>
Metal4Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (auto ok = validate(desc); !ok) {
        return std::unexpected(ok.error());
    }
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // validate() only proves the pointer is non-null; it cannot know the library came from
    // this backend. Every ShaderLibrary this device hands out is a Metal4ShaderLibrary, so a
    // foreign pointer is a caller contract violation, not a runtime error path.
    auto* library = static_cast<Metal4ShaderLibrary*>(desc.library);

    // Both failure modes below are *fatal* inside Metal, not recoverable errors, which is
    // why they are checked here rather than left to newRenderPipelineState:
    //   - an unknown entry name resolves to a nil function, and the pipeline-descriptor
    //     validator aborts with "vertexFunction must not be nil";
    //   - a name that exists but belongs to the wrong stage (fragmentMain as the vertex
    //     entry) aborts with "functionType is not a MTLFunctionTypeVertex".
    // Both measured. newFunction() answers existence *and* type in one call; functionNames()
    // is only used to build the suggestion list on the not-found path.
    const auto rejectEntry = [&](std::string_view entry,
                                 MTL::FunctionType expected) -> std::optional<std::string> {
        NS::SharedPtr<MTL::Function> function =
            NS::TransferPtr(library->handle()->newFunction(makeString(entry).get()));
        if (!function) {
            return "shader entry point '" + std::string(entry) +
                   "' not found in library; available entry points: " +
                   join(libraryFunctionNames(library->handle()));
        }
        if (function->functionType() != expected) {
            return "shader entry point '" + std::string(entry) + "' is a " +
                   describe(function->functionType()) + " function, but a " + describe(expected) +
                   " function is required here";
        }
        return std::nullopt;
    };

    if (auto problem = rejectEntry(desc.vertexEntry, MTL::FunctionTypeVertex)) {
        return fail(ErrorCode::PipelineCreationFailed, std::move(*problem));
    }
    if (auto problem = rejectEntry(desc.fragmentEntry, MTL::FunctionTypeFragment)) {
        return fail(ErrorCode::PipelineCreationFailed, std::move(*problem));
    }

    auto vertexFunction = NS::TransferPtr(MTL4::LibraryFunctionDescriptor::alloc()->init());
    vertexFunction->setLibrary(library->handle());
    vertexFunction->setName(makeString(desc.vertexEntry).get());

    auto fragmentFunction = NS::TransferPtr(MTL4::LibraryFunctionDescriptor::alloc()->init());
    fragmentFunction->setLibrary(library->handle());
    fragmentFunction->setName(makeString(desc.fragmentEntry).get());

    auto pipelineDesc = NS::TransferPtr(MTL4::RenderPipelineDescriptor::alloc()->init());
    pipelineDesc->setVertexFunctionDescriptor(vertexFunction.get());
    pipelineDesc->setFragmentFunctionDescriptor(fragmentFunction.get());
    pipelineDesc->colorAttachments()->object(0)->setPixelFormat(toMTL(desc.colorFormat));
    // Single-sampled; stated rather than inherited because the pipeline's sample count must
    // match the render pass's and M1 never multisamples.
    pipelineDesc->setRasterSampleCount(1);
    // MTL::RenderPipelineState has no setLabel (only a getter) -- the label has to be set on
    // the descriptor and is carried into the compiled state.
    if (!desc.label.empty()) {
        pipelineDesc->setLabel(makeString(desc.label).get());
    }

    NS::Error* error = nullptr;
    NS::SharedPtr<MTL::RenderPipelineState> state =
        NS::TransferPtr(m_compiler->newRenderPipelineState(
            pipelineDesc.get(), /*compilerTaskOptions=*/nullptr, &error));
    if (!state) {
        return fail(ErrorCode::PipelineCreationFailed,
                    "failed to create render pipeline (vertex '" + std::string(desc.vertexEntry) +
                        "', fragment '" + std::string(desc.fragmentEntry) +
                        "'): " + describe(error));
    }

    return std::make_unique<Metal4Pipeline>(std::move(state));
}

CommandList& Metal4Device::beginFrame() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    LMX_ASSERT(!m_frameOpen, "beginFrame: the previous frame is still open -- call endFrame");

    ++m_frameNumber;

    // Frame N and frame N-kFramesInFlight share an allocator, so N cannot reset it until N-3
    // has actually finished on the GPU. endFrame signals the event with the frame number after
    // that frame's command buffer, which makes "signaled value >= N-3" exactly that guarantee.
    // The first kFramesInFlight frames have no predecessor to wait for.
    if (m_frameNumber > kFramesInFlight) {
        const uint64_t completedFrame = m_frameNumber - kFramesInFlight;
        const bool signaled = m_frameEvent->waitUntilSignaledValue(completedFrame, kGpuTimeoutMs);
        LMX_ASSERT(signaled, "beginFrame: the GPU did not finish the frame that owns this "
                             "frame's command allocator within the timeout");
    }

    MTL4::CommandAllocator* allocator = m_allocators[m_frameNumber % kFramesInFlight].get();
    allocator->reset();
    m_commandBuffer->beginCommandBuffer(allocator);

    m_frameOpen = true;
    return *m_commandList;
}

void Metal4Device::endFrame(Swapchain* presentTo) {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    LMX_ASSERT(m_frameOpen, "endFrame: no frame is open -- call beginFrame first");
    LMX_ASSERT(!m_commandList->inRenderPass(),
               "endFrame: a render pass is still open -- call endRenderPass first");

    m_commandBuffer->endCommandBuffer();

    // Every Swapchain this backend hands out is a Metal4Swapchain; a foreign pointer is a
    // caller contract violation, not a runtime error path.
    auto* swapchain = static_cast<Metal4Swapchain*>(presentTo);
    CA::MetalDrawable* drawable = swapchain != nullptr ? swapchain->currentDrawable() : nullptr;
    LMX_ASSERT(swapchain == nullptr || drawable != nullptr,
               "endFrame: asked to present a swapchain whose texture was never acquired");

    if (drawable != nullptr) {
        // Order matters and is not interchangeable: the wait must be queued *before* the work
        // that writes the drawable (it gates that work on the display being finished with the
        // surface), and the signal after it (it tells Core Animation the pixels are ready).
        // present() then schedules the flip; it does not block.
        m_queue->wait(drawable);
    }

    const MTL4::CommandBuffer* commandBuffers[] = {m_commandBuffer.get()};
    m_queue->commit(commandBuffers, 1);

    if (drawable != nullptr) {
        m_queue->signalDrawable(drawable);
        drawable->present();
        swapchain->releaseCurrentDrawable();
    }

    // Retires this frame's allocator for the frame kFramesInFlight later; see beginFrame.
    m_queue->signalEvent(m_frameEvent.get(), m_frameNumber);
    m_frameOpen = false;
}

void Metal4Device::waitIdle() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // A throwaway event rather than m_frameEvent: the frame-pacing event's values are owned by
    // beginFrame/endFrame, and signalling an out-of-band value on it would corrupt that
    // sequence. The queue signals in submission order, so once this fires every command buffer
    // committed before it has completed.
    NS::SharedPtr<MTL::SharedEvent> done = NS::TransferPtr(m_device->newSharedEvent());
    LMX_ASSERT(done, "waitIdle: failed to create shared event");
    done->setLabel(makeString("lmx.device.waitIdle").get());
    done->setSignaledValue(0);

    m_queue->signalEvent(done.get(), 1);
    const bool signaled = done->waitUntilSignaledValue(1, kGpuTimeoutMs);
    LMX_ASSERT(signaled, "waitIdle: GPU did not complete within the timeout");
}

} // namespace lmx::rhi::metal4

namespace lmx::rhi {

Result<std::unique_ptr<Device>> createDevice(const DeviceDesc& desc) {
    return metal4::Metal4Device::create(desc);
}

} // namespace lmx::rhi
