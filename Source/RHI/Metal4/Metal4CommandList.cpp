#include "RHI/Metal4/Metal4CommandList.h"

#include "Core/Align.h"
#include "Core/Assert.h"
#include "RHI/Metal4/Metal4Device.h" // kUniformOffsetAlignment
#include "RHI/Metal4/Metal4Resources.h"

#include <cstring>

namespace lmx::rhi::metal4 {

void Metal4CommandList::beginRenderPass(const RenderPassDesc& desc) {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    // These are nulled by endFrameReset as well as unset before the first frame, so this is a
    // real "is a frame open?" check and not merely "has beginFrame ever run?".
    LMX_ASSERT(m_argumentTable != nullptr && m_uniformRing != nullptr,
               "beginRenderPass: no frame is open -- this command list is only valid between "
               "Device::beginFrame and Device::endFrame");
    LMX_ASSERT(!m_encoder, "beginRenderPass: a render pass is already open on this command list");
    // Task 9's D1 generalised: Metal 4 descriptor validation *aborts* on a nil field instead of
    // returning an NS::Error, and beginRenderPass has no error channel anyway. A null color
    // target would surface as "Fragment attachment 0 has no texture" from inside Metal, with no
    // hint about which RHI call produced it.
    LMX_ASSERT(desc.colorTarget != nullptr, "RenderPassDesc.colorTarget must not be null");

    // Every Texture this backend hands out is a Metal4Texture -- including the transient
    // drawable wrapper the swapchain returns -- so a foreign pointer is a contract violation,
    // not a runtime error path.
    auto* colorTarget = static_cast<Metal4Texture*>(desc.colorTarget);

    auto passDesc = NS::TransferPtr(MTL4::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassColorAttachmentDescriptor* color = passDesc->colorAttachments()->object(0);
    color->setTexture(colorTarget->handle());
    color->setLoadAction(desc.clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
    // Always Store: M1 has no transient attachments, and a DontCare here would discard the very
    // pixels the swapchain is about to present.
    color->setStoreAction(MTL::StoreActionStore);
    if (desc.clear) {
        color->setClearColor(MTL::ClearColor(desc.clearColor[0], desc.clearColor[1],
                                             desc.clearColor[2], desc.clearColor[3]));
    }
    // Stated rather than inherited, for the same reason the pipeline states it (Task 9): the
    // pass's sample count must match the pipeline's, and M1 never multisamples.
    passDesc->setDefaultRasterSampleCount(1);

    m_encoder = NS::RetainPtr(m_commandBuffer->renderCommandEncoder(passDesc.get()));
    LMX_ASSERT(m_encoder, "beginRenderPass: failed to create a render command encoder");
    m_encoder->setLabel(makeString("lmx.encoder.render").get());

    // The default viewport is derived from the attachment, so this is redundant *today*; it is
    // set explicitly because the derived value silently stops being the right one the moment a
    // pass renders into a sub-region of its target, and that is a bug that looks like a shader
    // bug.
    const MTL::Viewport viewport{0.0,
                                 0.0,
                                 static_cast<double>(colorTarget->width()),
                                 static_cast<double>(colorTarget->height()),
                                 0.0,
                                 1.0};
    m_encoder->setViewport(viewport);

    // Attached here rather than in bindVertexBuffer so that *every* pass has a table, including
    // one that binds a pipeline but no buffer: a stage whose signature names a buffer that no
    // attached argument table binds trips validation at draw time, and the table is
    // zero-initialised (setInitializeBindings, Task 8), so an unbound slot reads as a null
    // address rather than as garbage. Both stages, deliberately: Slang emits `gVertices` into
    // the vertex *and* fragment signatures at buffer 0 (Task 6 record).
    m_encoder->setArgumentTable(m_argumentTable, MTL::RenderStageVertex | MTL::RenderStageFragment);
}

void Metal4CommandList::bindPipeline(GraphicsPipeline& pipeline) {
    LMX_ASSERT(m_encoder, "bindPipeline must be called between beginRenderPass and endRenderPass");
    m_encoder->setRenderPipelineState(static_cast<Metal4Pipeline&>(pipeline).handle());
}

void Metal4CommandList::bindVertexBuffer(uint32_t slot, Buffer& buffer) {
    LMX_ASSERT(m_encoder,
               "bindVertexBuffer must be called between beginRenderPass and endRenderPass");

    // Metal 4 binds by raw GPU address through an argument table rather than by resource
    // handle; residency is what keeps the allocation alive, and that was arranged when the
    // buffer was created (Metal4Buffer's ResidencyRegistration). The table itself was attached
    // to the encoder by beginRenderPass.
    m_argumentTable->setAddress(static_cast<Metal4Buffer&>(buffer).handle()->gpuAddress(), slot);
}

void Metal4CommandList::setUniforms(uint32_t slot, const void* data, uint64_t size) {
    // No autorelease pool, and that is verified rather than assumed: contents(), length(),
    // gpuAddress() and setAddress() are all scalar- or void-returning sendMessage in the
    // vendored headers, so nothing on this path is autoreleased. See the header's note.
    LMX_ASSERT(m_encoder, "setUniforms must be called between beginRenderPass and endRenderPass");
    LMX_ASSERT(data != nullptr && size > 0, "setUniforms: data must be non-null and non-empty");
    const uint64_t offset = *m_uniformOffset;
    const uint64_t capacity = m_uniformRing->length();
    // Split in two, and phrased as a subtraction, because the obvious `offset + size <= capacity`
    // wraps on a bogus size near UINT64_MAX and would wave through the unbounded memcpy below --
    // the exact case the guard exists for. `size <= capacity` first is what makes the
    // `capacity - size` in the second check safe.
    LMX_ASSERT(size <= capacity, "setUniforms: upload is larger than the entire per-frame uniform "
                                 "ring -- grow kUniformRingBytes");
    LMX_ASSERT(offset <= capacity - size,
               "setUniforms: per-frame uniform ring exhausted -- grow kUniformRingBytes");
    std::memcpy(static_cast<uint8_t*>(m_uniformRing->contents()) + offset, data, size);
    m_argumentTable->setAddress(m_uniformRing->gpuAddress() + offset, slot);
    *m_uniformOffset = alignUp(offset + size, kUniformOffsetAlignment);
}

void Metal4CommandList::draw(uint32_t vertexCount, uint32_t firstVertex) {
    LMX_ASSERT(m_encoder, "draw must be called between beginRenderPass and endRenderPass");
    LMX_ASSERT(vertexCount > 0, "draw: vertexCount must be greater than zero");
    m_encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, firstVertex, vertexCount);
}

void Metal4CommandList::endRenderPass() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    LMX_ASSERT(m_encoder, "endRenderPass: no render pass is open on this command list");
    m_encoder->endEncoding();
    m_encoder.reset();
}

void Metal4CommandList::resetForFrame(MTL4::ArgumentTable* argumentTable, MTL::Buffer* uniformRing,
                                      uint64_t* uniformOffset) {
    // An open encoder here means the previous frame never ended its pass, and swapping the table
    // under a live encoder would move bindings the device has already been told about. The
    // device asserts the same thing from the other side in endFrame; this catches the case where
    // the list is re-pointed without an endFrame at all.
    LMX_ASSERT(!m_encoder, "resetForFrame: a render pass is still open from the previous frame");
    LMX_ASSERT(argumentTable != nullptr, "resetForFrame: argument table must not be null");
    LMX_ASSERT(uniformRing != nullptr && uniformOffset != nullptr,
               "resetForFrame: uniform ring and its offset cursor must not be null");
    m_argumentTable = argumentTable;
    m_uniformRing = uniformRing;
    m_uniformOffset = uniformOffset;
}

void Metal4CommandList::endFrameReset() {
    // The device already asserted the pass was closed before it committed, so there is nothing
    // to tear down here -- only per-frame pointers to forget. Deliberately unconditional: this
    // is the state that makes "used outside a frame" detectable at all.
    m_argumentTable = nullptr;
    m_uniformRing = nullptr;
    m_uniformOffset = nullptr;
}

} // namespace lmx::rhi::metal4
