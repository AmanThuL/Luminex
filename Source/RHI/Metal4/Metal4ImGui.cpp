#include "RHI/Metal4/Metal4ImGui.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "RHI/Metal4/Metal4CommandList.h"
#include "RHI/Metal4/Metal4Common.h"
#include "RHI/Metal4/Metal4Device.h"
#include "RHI/Metal4/Metal4Resources.h"

// Selects the backend's metal-cpp declarations rather than its Objective-C ones: the header offers
// both, gated on IMGUI_IMPL_METAL_CPP (defined publicly by the ImGui target in xmake.lua) and on
// __OBJC__ being absent, which it is in a .cpp. That is why this file is plain C++ and not a .mm
// -- the whole ObjC surface of the backend stays inside imgui_impl_metal4.mm.
#include <imgui_impl_metal4.h>

// For imguiForgetTexture()'s reach into the backend's own residency set -- see the comment on
// imguiResidencySet() below. Plain C headers, usable from C++; metal-cpp itself is built on them.
#include <objc/message.h>
#include <objc/runtime.h>

#include <cstdint>
#include <utility>

namespace lmx::rhi::metal4 {
namespace {

// File-scope for the same reason Metal4Capture's is: the API being wrapped is itself global. The
// ImGui_ImplMetal4_* entry points find their state through the current ImGui context, not through
// a handle a caller holds, so there is nothing to return from imguiInit() and hold onto. Null
// outside an init/shutdown pair, which is what makes "was imguiInit called?" answerable.
Metal4Device* g_device = nullptr;

// A pipeline-compatibility stand-in for the UI render pass -- not a render target, and never bound
// or rendered into.
//
// ImGui_ImplMetal4_NewFrame() takes an MTL4::RenderPassDescriptor and does exactly one thing with
// it: builds a FramebufferDescriptor, which reads four values and keeps nothing else
// (imgui_impl_metal4.mm:569-579) -- colorAttachments[0].texture's sampleCount and pixelFormat, and
// the depth and stencil attachment textures' pixelFormats. Those four are the key of its
// render-pipeline-state cache (imgui_impl_metal4.mm:246-254, 762-772) and the descriptor is never
// encoded from on the main-viewport path.
//
// So a descriptor carrying a 1x1 texture in the UI pass's color format describes that pass
// *exactly* as far as ImGui can tell, and it costs four bytes to be exact. Depth and stencil are
// left without textures, which reads back as PixelFormatInvalid -- the honest description of the
// depth-less pass this glue documents.
//
// The alternative, feeding the backend the frame's real drawable, was rejected: it would tie
// imguiNewFrame() to Device::beginFrame() and Swapchain::acquireNextTexture(), when Dear ImGui
// requires the renderer's NewFrame *before* ImGui::NewFrame() and therefore before the UI code
// that decides what -- or whether -- the frame renders.
NS::SharedPtr<MTL::Texture> g_formatCarrier;
NS::SharedPtr<MTL4::RenderPassDescriptor> g_passDescriptor;

// The slot imguiNewFrame() last handed the backend, or kNoFrameStarted between an imguiRender()
// and the next imguiNewFrame().
//
// This exists because skipping imguiNewFrame() is the one misuse in this file that would otherwise
// be *silent*. Every other one aborts: a missing imguiInit, a closed render pass, a missing
// ImGui::Render(). But the backend keeps its frame slot in a member (`currentFrameSlot`), so
// rendering without a matching NewFrame simply reuses the previous frame's slot and writes vertex,
// index and constant buffers a frame still on the GPU may be reading -- corruption that shows up,
// if at all, as an intermittently torn UI several frames later.
//
// Comparing the *slot* rather than a bare bool costs nothing extra and catches more: it also fires
// when frames were begun and ended between the NewFrame and the render, which lands in the same
// place for the same reason.
constexpr uint32_t kNoFrameStarted = UINT32_MAX;
uint32_t g_pendingFrameSlot = kNoFrameStarted;

// The ImGui Metal 4 backend's own MTLResidencySet, or nullptr when the backend is not up.
//
// This is the one place in the project that reaches into a vendored implementation rather than
// through its header, and it is deliberate: the backend adds every user texture to this set
// (imgui_impl_metal4.mm:333) and offers no way to take one out again -- there is no removal hook
// in imgui_impl_metal4.h at all, and ImGui_ImplMetal4_DestroyDeviceObjects (:488-502) does not
// touch the set either. The alternatives were worse: patching the vendored tree loses the change
// on the next `xmake setup`, and leaving it alone strands a full-viewport texture per resize.
//
// The reach is exactly the one the backend makes internally, in two documented steps:
//
//  1. ImGui_ImplMetal4_GetBackendData() is `(ImGui_ImplMetal4_Data*)io.BackendRendererUserData`
//     (:112), and `struct ImGui_ImplMetal4_Data { MetalContext* SharedMetalContext; id<...>
//     RenderCommandEncoder; }` (:104-110) -- SharedMetalContext is the *first* member, so the
//     first pointer-sized word of that allocation is the MetalContext object itself.
//  2. MetalContext declares `@property (nonatomic, strong) id<MTLResidencySet> residencySet;`
//     (:86), whose synthesised getter is the `residencySet` selector sent below.
//
// Both couplings are asserted rather than assumed: if a future imgui pin reorders the struct or
// renames the property, the responds-to-selector check below aborts with this comment's name on
// it instead of silently returning garbage. Raw objc_msgSend rather than NS::Object::sendMessage
// because metal-cpp keeps that one protected.
MTL::ResidencySet* imguiResidencySet() {
    void* backendData = ImGui::GetIO().BackendRendererUserData;
    if (backendData == nullptr) {
        return nullptr;
    }
    void* context = *static_cast<void**>(backendData);
    if (context == nullptr) {
        return nullptr;
    }

    static const SEL kResidencySetSelector = sel_registerName("residencySet");
    LMX_ASSERT(
        class_respondsToSelector(object_getClass(static_cast<id>(context)), kResidencySetSelector),
        "imguiForgetTexture: the ImGui Metal 4 backend's private layout changed -- the "
        "first member of ImGui_ImplMetal4_Data is no longer a MetalContext with a "
        "'residencySet' property. Re-derive imguiResidencySet() against the new "
        "imgui_impl_metal4.mm");

    using SendObjectMessage = void* (*)(const void*, SEL);
    void* residency =
        reinterpret_cast<SendObjectMessage>(objc_msgSend)(context, kResidencySetSelector);
    return static_cast<MTL::ResidencySet*>(residency);
}

} // namespace

bool imguiInit(Device& device, Format colorFormat) {
    // Load-bearing here and in every entry point below, not boilerplate: the vendored backend is
    // compiled without ARC (see xmake.lua) and sends -autorelease itself, so a call arriving with
    // no pool in place leaks the object and logs about it.
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    LMX_ASSERT(g_device == nullptr, "imguiInit: already initialised -- call imguiShutdown first");
    // The same whitelist Validate.cpp applies to SwapchainDesc::format: a depth or Unknown format
    // here would abort inside Metal's texture-descriptor validator rather than fail a check.
    LMX_ASSERT(colorFormat == Format::BGRA8Unorm || colorFormat == Format::RGBA8Unorm,
               "imguiInit: colorFormat must be a color-renderable format (BGRA8Unorm or "
               "RGBA8Unorm)");

    // Every Device this backend hands out is a Metal4Device; a foreign pointer is a caller
    // contract violation, not a runtime error path (the convention Metal4Capture states).
    auto& metalDevice = static_cast<Metal4Device&>(device);

    // Built through MTL::Device directly rather than Device::createTexture: this texture is never
    // bound, sampled or drawn into, so the residency membership the RHI wrapper exists to manage
    // would be pure cost, and it must not be handed out as an RHI Texture either.
    auto textureDesc = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
    textureDesc->setTextureType(MTL::TextureType2D);
    textureDesc->setPixelFormat(toMTL(colorFormat));
    textureDesc->setWidth(1);
    textureDesc->setHeight(1);
    textureDesc->setMipmapLevelCount(1);
    // The least obvious line here and the one that actually does the work: this is what makes
    // ImGui's UI pipeline single-sampled. The backend takes its sample count off the *attachment
    // texture* (imgui_impl_metal4.mm:573) and feeds it straight to the pipeline's
    // rasterSampleCount (:762); it never reads the render pass descriptor's
    // defaultRasterSampleCount, so setting that here would be inert. Stated rather than left to
    // MTLTextureDescriptor's default of 1, because that default is the entire mechanism.
    textureDesc->setSampleCount(1);
    // RenderTarget rather than the descriptor's default ShaderRead: it stands in for a color
    // attachment, and the usage bits are the only place that intent can be written down.
    textureDesc->setUsage(MTL::TextureUsageRenderTarget);
    textureDesc->setStorageMode(MTL::StorageModePrivate);

    NS::SharedPtr<MTL::Texture> carrier =
        NS::TransferPtr(metalDevice.handle()->newTexture(textureDesc.get()));
    if (!carrier) {
        LMX_LOG_ERROR("ImGui init: failed to create the 1x1 render-pass format carrier texture");
        return false;
    }
    carrier->setLabel(makeString("lmx.imgui.formatCarrier").get());

    // Nothing else is set on it: the color attachment's load/store actions and clear color are
    // never read (the backend only ever builds a FramebufferDescriptor from this), and the sample
    // count that matters rode in on the texture above.
    auto passDesc = NS::TransferPtr(MTL4::RenderPassDescriptor::alloc()->init());
    passDesc->colorAttachments()->object(0)->setTexture(carrier.get());

    // kFramesInFlight, not a number of its own: the backend sizes its per-frame vertex, index and
    // constant buffers by this, and imguiNewFrame() indexes them with the very slot this device
    // rotates its allocators and argument tables on. The two rings have to be the same length for
    // that index to mean the same thing on both sides.
    if (!ImGui_ImplMetal4_Init(metalDevice.handle(), metalDevice.queue(),
                               static_cast<int>(kFramesInFlight))) {
        LMX_LOG_ERROR("ImGui init: the Metal 4 renderer backend refused to initialise");
        return false;
    }

    g_formatCarrier = std::move(carrier);
    g_passDescriptor = std::move(passDesc);
    g_device = &metalDevice;
    LMX_LOG_INFO("ImGui renderer: imgui_impl_metal4, {} frames in flight", kFramesInFlight);
    return true;
}

void imguiShutdown() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // Tolerant rather than assertive, like endCapture(): teardown runs on paths that do not all
    // know whether init got that far, and there is nothing to protect -- the ImGui backend's own
    // shutdown is the thing that would object to being called twice.
    if (g_device == nullptr) {
        return;
    }

    // Drained here rather than demanded of the caller, because this is the only place that both
    // knows the requirement and can satisfy it. ImGui_ImplMetal4_Shutdown() frees the font atlas
    // and every cached vertex/index buffer without waiting for anything, so a frame still in
    // flight would be reading freed allocations. A second drain at teardown costs nothing -- the
    // queue is idle by then in every path that already called waitIdle.
    g_device->waitIdle();

    ImGui_ImplMetal4_Shutdown();
    g_passDescriptor.reset();
    g_formatCarrier.reset();
    g_pendingFrameSlot = kNoFrameStarted;
    g_device = nullptr;
}

void imguiNewFrame() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    LMX_ASSERT(g_device != nullptr, "imguiNewFrame: call imguiInit first");
    // frameInFlightIndex() answers for the frame *about to open* when called here, before
    // Device::beginFrame -- see its comment. Passing the just-ended frame's slot instead would
    // have ImGui write the vertex, index and constant buffers of a frame still on the GPU.
    const uint32_t slot = g_device->frameInFlightIndex();
    ImGui_ImplMetal4_NewFrame(g_passDescriptor.get(), static_cast<int>(slot));
    g_pendingFrameSlot = slot;
}

void imguiRender(CommandList& commands) {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    LMX_ASSERT(g_device != nullptr, "imguiRender: call imguiInit first");
    // Same contract-violation rationale as the Device downcast in imguiInit: every CommandList
    // this backend hands out is the device's one Metal4CommandList.
    auto& commandList = static_cast<Metal4CommandList&>(commands);

    ImDrawData* drawData = ImGui::GetDrawData();
    LMX_ASSERT(drawData != nullptr,
               "imguiRender: no draw data -- call ImGui::Render() before this, in the same frame");

    // See kNoFrameStarted: this pairing is the one thing here that would fail silently. A frame is
    // open by now, so frameInFlightIndex() names the open frame's slot -- the same value
    // imguiNewFrame() computed for it, whether it ran before or after beginFrame.
    LMX_ASSERT(g_pendingFrameSlot != kNoFrameStarted,
               "imguiRender: no ImGui frame is open -- call imguiNewFrame() (then ImGui::NewFrame, "
               "build the UI, ImGui::Render) once per frame before this");
    LMX_ASSERT(g_pendingFrameSlot == g_device->frameInFlightIndex(),
               "imguiRender: this frame's imguiNewFrame() ran against a different frame in flight "
               "-- call imguiNewFrame() and imguiRender() exactly once each per Device frame");
    g_pendingFrameSlot = kNoFrameStarted;

    // Both accessors assert a pass is open, so the "called outside beginRenderPass" case is
    // reported from there rather than as a Metal abort part-way through encoding.
    ImGui_ImplMetal4_RenderDrawData(drawData, commandList.commandBuffer(),
                                    commandList.currentEncoder());
}

ImTextureID imguiTextureID(Texture& texture) {
    // Not needed to compute the value -- this is a pure cast -- but an identifier handed to a
    // renderer that does not exist yet can only be a sequencing mistake, and every other entry
    // point here says so.
    LMX_ASSERT(g_device != nullptr, "imguiTextureID: call imguiInit first");

    // The identifier is the MTLTexture object pointer, not its gpuResourceID: the backend stores
    // `(ImTextureID)(intptr_t)texture` for its own atlas (imgui_impl_metal4.mm:406), casts it
    // straight back to an id<MTLTexture> when it meets one in a draw command
    // (imgui_impl_metal4.mm:332), and only then derives .gpuResourceID to bind it
    // (imgui_impl_metal4.mm:334). imgui_impl_metal4.h's feature list still says gpuResourceID; the
    // .mm is what runs, and its own header comment was corrected to "MTLTexture". A gpuResourceID
    // would not survive that round trip -- MTLResourceID is not the object.
    //
    // metal-cpp's MTL::Texture* is that same pointer: the backend's C++ overloads reach the
    // Objective-C ones by nothing more than a __bridge cast (imgui_impl_metal4.mm:121-138).
    //
    // Same contract-violation downcast as everywhere else in this file.
    MTL::Texture* handle = static_cast<Metal4Texture&>(texture).handle();
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(handle));
}

void imguiForgetTexture(Texture& texture) {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // Tolerant rather than assertive, like imguiShutdown(): a caller that never brought the UI up
    // still releases its textures, and has nothing to undo here.
    if (g_device == nullptr) {
        return;
    }
    MTL::ResidencySet* residency = imguiResidencySet();
    if (residency == nullptr) {
        return;
    }

    // Same contract-violation downcast as everywhere else in this file.
    MTL::Texture* handle = static_cast<Metal4Texture&>(texture).handle();
    const NS::UInteger before = residency->allocationCount();
    residency->removeAllocation(handle);
    // The set only republishes its allocation list when told to -- the same reason
    // ResidencyRegistration commits on both sides.
    residency->commit();
    // Logged, not silent: this is the only visibility anything has into a set we do not own, and
    // a count that climbs across resizes instead of returning to its previous value is the exact
    // shape of the leak this function exists to prevent. It runs once per settled resize, so it
    // is no noisier than the resize log it accompanies.
    LMX_LOG_INFO("ImGui residency: forgot a displayed texture ({} -> {} allocations)", before,
                 residency->allocationCount());
}

} // namespace lmx::rhi::metal4
