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

    // Optional: a null depthTarget leaves the descriptor's depth attachment without a texture,
    // which is how Metal spells "depth-less pass" -- exactly the M1 behaviour, unchanged.
    if (desc.depthTarget != nullptr) {
        // Same contract-violation rationale as colorTarget above.
        auto* depthTarget = static_cast<Metal4Texture*>(desc.depthTarget);
        // The abort this replaces: a color-formatted texture in the depth slot makes Metal's
        // pass validator abort with "the depth attachment's pixel format is not a depth
        // format", naming neither the RHI call nor the texture.
        LMX_ASSERT(depthTarget->handle()->pixelFormat() == MTL::PixelFormatDepth32Float,
                   "RenderPassDesc.depthTarget must be a D32Float texture");
        // Not a Metal rule but ours, and it closes a silent one: the store action below is
        // DontCare, so no pass ever leaves depth behind, so LoadActionLoad could only ever read
        // undefined memory -- producing a plausible-looking image with wrong occlusion and no
        // diagnostic anywhere. Lift this together with the store action, not before.
        LMX_ASSERT(desc.clear,
                   "RenderPassDesc: a depth attachment requires clear -- depth is never stored "
                   "in M2, so a load would read undefined memory");

        MTL::RenderPassDepthAttachmentDescriptor* depth = passDesc->depthAttachment();
        depth->setTexture(depthTarget->handle());
        depth->setLoadAction(desc.clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
        // DontCare: nothing reads scene depth after the pass in M2; Store would spill it for no
        // consumer. Flip to Store the day a depth-reading pass exists.
        depth->setStoreAction(MTL::StoreActionDontCare);
        depth->setClearDepth(desc.clearDepth);
    }

    // Stated rather than inherited, for the same reason the pipeline states it (Task 9): the
    // pass's sample count must match the pipeline's, and M1 never multisamples.
    passDesc->setDefaultRasterSampleCount(1);

    m_encoder = NS::RetainPtr(m_commandBuffer->renderCommandEncoder(passDesc.get()));
    LMX_ASSERT(m_encoder, "beginRenderPass: failed to create a render command encoder");
    m_encoder->setLabel(makeString("lmx.encoder.render").get());

    // A textureBarrier recorded since the last pass lands here, as the first thing this encoder
    // does: Metal 4 barriers are encoder operations, but the RHI call that asks for one sits
    // *between* passes, when no encoder exists. So it is recorded as pending and encoded
    // consumer-side, on the pass that reads the data (Apple, "Synchronizing passes with consumer
    // barriers"). Consumer rather than producer form because the reader is the pass we can still
    // reach -- the producing encoder was already ended when textureBarrier was called.
    if (m_pendingBarrier) {
        // Attachment writes happen in the fragment stage (MTL::Stages has no render-target
        // stage), so the RT-write -> fragment-read edge is fragment -> fragment.
        //
        // barrierAfterQueueStages, not barrierAfterStages: afterQueueStages names stages of
        // *prior* encoders and beforeStages names work in *this* one, which is exactly the
        // direction we need (the producer-side call would have to be encoded in the pass that
        // wrote, and that pass is gone). Its documented restriction -- "Don't use this method for
        // synchronizing resource access within the same pass" -- is satisfied by textureBarrier's
        // between-passes assert. Position: Apple asks for the barrier "as close to the command
        // that consumes the resource as possible"; the RHI cannot know which draw that is, so it
        // goes first, which is the conservative end of the range (beforeStages applies to
        // everything encoded after it here) and is what makes it correct for every draw in the
        // pass.
        m_encoder->barrierAfterQueueStages(MTL::StageFragment, MTL::StageFragment,
                                           MTL4::VisibilityOptionDevice);
        m_pendingBarrier = false;
    }

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
    auto& metalPipeline = static_cast<Metal4Pipeline&>(pipeline);
    m_encoder->setRenderPipelineState(metalPipeline.handle());
    // Depth state rides along with the pipeline even though Metal keeps the two separate, so
    // that one RHI bind fully describes the depth behaviour a GraphicsPipelineDesc asked for.
    // Null (neither test nor write requested) leaves the encoder's default in place, which is
    // already compare-Always/no-write -- see Metal4Pipeline::depthState.
    if (MTL::DepthStencilState* depthState = metalPipeline.depthState()) {
        m_encoder->setDepthStencilState(depthState);
    }
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

void Metal4CommandList::bindTexture(uint32_t slot, Texture& texture) {
    LMX_ASSERT(m_encoder, "bindTexture must be called between beginRenderPass and endRenderPass");
    auto& metalTexture = static_cast<Metal4Texture&>(texture);
    // RHI.h states the sampled requirement; this is what enforces it, and the enforcement cannot
    // be left to Metal: setTexture below takes an opaque MTL::ResourceID, not a texture object,
    // so the validation layer has nothing to resolve a usage flag from at bind time. Without this
    // the mistake surfaces (if at all) as a shader reading a texture that never got ShaderRead
    // usage, far from the call that bound it.
    //
    // Deliberately a usage-bit test rather than a desc.sampled test: Metal4Device grants
    // ShaderRead for `sampled || cpuReadback` (Metal4Device.cpp:375), and a readback texture bound
    // for sampling is legal -- the bit is exactly the question being asked.
    LMX_ASSERT((metalTexture.handle()->usage() & MTL::TextureUsageShaderRead) != 0,
               "bindTexture: texture has no ShaderRead usage -- create it with sampled = true");
    // Textures bind by MTL::ResourceID rather than by GPU address (the buffer path above);
    // residency is again what keeps the allocation alive, arranged at creation. `slot` indexes
    // the argument table's *texture* bindings, a separate array from its addresses -- so this
    // does not collide with bindVertexBuffer/setUniforms on the same number.
    m_argumentTable->setTexture(metalTexture.handle()->gpuResourceID(), slot);
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

void Metal4CommandList::drawIndexed(Buffer& indexBuffer, uint32_t indexCount, uint32_t firstIndex) {
    LMX_ASSERT(m_encoder, "drawIndexed must be called between beginRenderPass and endRenderPass");
    LMX_ASSERT(indexCount > 0, "drawIndexed: indexCount must be greater than zero");
    auto& mtlBuffer = static_cast<Metal4Buffer&>(indexBuffer);
    const uint64_t offsetBytes = uint64_t{firstIndex} * sizeof(uint32_t);
    const uint64_t lengthBytes = mtlBuffer.handle()->length();
    // Written as a plain sum, unlike setUniforms' subtraction dance, because here it provably
    // cannot wrap: both terms are a uint32 widened to uint64 and scaled by 4, so each is at most
    // 2^34 and the sum at most 2^35. setUniforms needed the subtraction only because its `size`
    // is a caller-supplied uint64 that can sit near UINT64_MAX; no argument here can.
    LMX_ASSERT(offsetBytes + uint64_t{indexCount} * sizeof(uint32_t) <= lengthBytes,
               "drawIndexed: index range reads past the end of the index buffer");
    // The trailing argument is the length of the index *data* at the address passed -- i.e. the
    // bytes remaining after the offset, not the buffer's total length (signature checked against
    // MTL4RenderCommandEncoder.hpp:66). Passing lengthBytes would overstate the range by
    // offsetBytes. The subtraction is safe because the assert above, which is never compiled out,
    // establishes offsetBytes < lengthBytes whenever indexCount > 0.
    m_encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, indexCount, MTL::IndexTypeUInt32,
                                     mtlBuffer.handle()->gpuAddress() + offsetBytes,
                                     lengthBytes - offsetBytes);
}

void Metal4CommandList::endRenderPass() {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

    LMX_ASSERT(m_encoder, "endRenderPass: no render pass is open on this command list");
    m_encoder->endEncoding();
    m_encoder.reset();
}

void Metal4CommandList::textureBarrier(Texture& texture, TextureUse from, TextureUse to) {
    (void)texture; // stage-scoped in Metal 4; the parameter documents intent and feeds the
                   // future Vulkan backend's image transition.
    LMX_ASSERT(!m_encoder, "textureBarrier must be called between render passes, not inside one");
    // "Between passes" has to mean between passes *of an open frame*. resetForFrame deliberately
    // does not clear m_pendingBarrier (endFrameReset owns that, so a dropped edge aborts in the
    // frame that dropped it), which leaves one gap: a barrier recorded with no frame open would
    // sit in the flag and be silently applied to the *next* frame's first pass. The nulled
    // per-frame pointer is this class's established outside-a-frame tell, so it closes that gap.
    LMX_ASSERT(m_argumentTable != nullptr, "textureBarrier must be called inside a frame");
    LMX_ASSERT(from == TextureUse::RenderTarget && to == TextureUse::ShaderRead,
               "textureBarrier: only RenderTarget -> ShaderRead is implemented (grown per demand)");
    // Nothing is encoded here -- there is no encoder to encode onto between passes. The next
    // beginRenderPass emits the barrier as its first command; see the block there.
    m_pendingBarrier = true;
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
    // A barrier still pending at commit is a dropped dependency edge: the caller asked for one
    // and no later pass ever consumed it, so the read it was meant to order either never happened
    // or happened unsynchronised. Silently clearing it would turn that into an intermittent
    // wrong-pixels bug, so it is fatal here rather than swept up. Checked at *end* of frame, not
    // at the next resetForFrame, so the abort names the frame that dropped it.
    LMX_ASSERT(!m_pendingBarrier, "textureBarrier recorded but no later pass consumed it");
    // The device already asserted the pass was closed before it committed, so there is nothing
    // to tear down here -- only per-frame pointers to forget. Deliberately unconditional: this
    // is the state that makes "used outside a frame" detectable at all.
    m_argumentTable = nullptr;
    m_uniformRing = nullptr;
    m_uniformOffset = nullptr;
    // Redundant given the assert above (which never compiles out), and written anyway so that
    // "no per-frame state survives endFrameReset" holds by construction rather than by argument.
    m_pendingBarrier = false;
}

MTL4::CommandBuffer* Metal4CommandList::commandBuffer() const {
    LMX_ASSERT(m_encoder, "commandBuffer: no render pass is open -- the command buffer is only "
                          "open for encoding between beginRenderPass and endRenderPass");
    return m_commandBuffer;
}

MTL4::RenderCommandEncoder* Metal4CommandList::currentEncoder() const {
    LMX_ASSERT(m_encoder, "currentEncoder: no render pass is open -- call beginRenderPass first");
    return m_encoder.get();
}

} // namespace lmx::rhi::metal4
