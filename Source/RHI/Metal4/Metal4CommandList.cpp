#include "RHI/Metal4/Metal4CommandList.h"

#include "Core/Assert.h"
#include "RHI/Metal4/Metal4Resources.h"

namespace lmx::rhi::metal4 {

void Metal4CommandList::beginRenderPass(const RenderPassDesc& desc) {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

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
    // buffer was created (Metal4Buffer's ResidencyRegistration).
    m_argumentTable->setAddress(static_cast<Metal4Buffer&>(buffer).handle()->gpuAddress(), slot);

    // Both stages, deliberately: Slang emits `gVertices` into the vertex *and* fragment
    // signatures at buffer 0 (Task 6 record), and a stage whose signature names a buffer that
    // no attached argument table binds trips validation at draw time.
    m_encoder->setArgumentTable(m_argumentTable, MTL::RenderStageVertex | MTL::RenderStageFragment);
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

} // namespace lmx::rhi::metal4
