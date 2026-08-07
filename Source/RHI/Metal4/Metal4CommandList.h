#pragma once
#include "RHI/Metal4/Metal4Common.h"

#include <cstdint>

namespace lmx::rhi::metal4 {

// The recording half of the frame protocol. One instance is owned by Metal4Device and handed
// back from every beginFrame(); it records into the device's single MTL4::CommandBuffer, which
// beginFrame has already re-opened against this frame's allocator.
//
// It owns nothing that outlives a frame. The command buffer, the argument table and the uniform
// ring belong to the device (which outlives every command list it hands out), so all three are
// held as raw pointers; only the render command encoder -- created and destroyed inside a single
// beginRenderPass/endRenderPass pair -- is reference-counted here, because
// renderCommandEncoder() returns an autoreleased (+0) object that must survive the local
// autorelease pool it was created in.
//
// Those per-frame pointers are *not* fixed at construction: the device owns one argument table
// and one uniform ring per frame in flight and hands this list the current frame's pair through
// resetForFrame, so neither is ever written outside the frame that owns it. endFrameReset nulls
// them again at commit, which is what lets beginRenderPass's assert distinguish "no frame is
// open" from "a frame is open" at all -- without it a stale pointer would keep every check
// passing while the writes landed in a slot the GPU was still reading.
//
// Encoder-scoped calls (bindPipeline/bindVertexBuffer/bindTexture/setUniforms/draw/drawIndexed)
// assert rather than return errors: calling them outside a pass is a caller sequencing bug, and
// the RHI CommandList methods return void. They also hold no autorelease pool of their own -- none
// of them invokes an autoreleasing selector, so a per-call pool bought nothing and cost a
// create/drain on what becomes the hottest path in the backend. Verified against the vendored
// headers rather than assumed: bindPipeline/bindVertexBuffer/draw are direct setters and draws
// on an already-retained encoder; setUniforms' whole call path -- MTL::Buffer::contents()/
// length()/gpuAddress() and MTL4::ArgumentTable::setAddress() -- is scalar- and void-returning
// sendMessage; drawIndexed adds only MTL::Buffer::length()/gpuAddress() (scalar sends) plus
// drawIndexedPrimitives (a void send) on top of a plain C++ Metal4Buffer::handle() getter; and
// bindTexture is MTL::Texture::usage() and gpuResourceID() (scalar sends, MTLTexture.hpp:293 and
// :241) into MTL4::ArgumentTable::setTexture() (a void send, MTL4ArgumentTable.hpp:80). So no
// path produces
// a +0 object. textureBarrier records a flag and touches Metal not at all; the barrier it defers
// is MTL4::CommandEncoder::barrierAfterQueueStages(), also a void send, encoded inside
// beginRenderPass's existing pool.
//
// Exactly one pool is load-bearing, in beginRenderPass: renderCommandEncoder() is the only
// selector here that returns +0. endRenderPass keeps a pool too (symmetry, and cheap insurance
// against a teardown path that starts autoreleasing) but it is *not* currently carrying
// anything -- endEncoding() is a void sendMessage and the encoder reset is a plain release.
class Metal4CommandList final : public CommandList {
public:
    explicit Metal4CommandList(MTL4::CommandBuffer* commandBuffer)
        : m_commandBuffer(commandBuffer) {}

    Metal4CommandList(const Metal4CommandList&) = delete;
    Metal4CommandList& operator=(const Metal4CommandList&) = delete;

    void beginRenderPass(const RenderPassDesc& desc) override;
    void bindPipeline(GraphicsPipeline& pipeline) override;
    void bindVertexBuffer(uint32_t slot, Buffer& buffer) override;
    void bindTexture(uint32_t slot, Texture& texture) override;
    void setUniforms(uint32_t slot, const void* data, uint64_t size) override;
    void draw(uint32_t vertexCount, uint32_t firstVertex) override;
    void drawIndexed(Buffer& indexBuffer, uint32_t indexCount, uint32_t firstIndex) override;
    void endRenderPass() override;
    void textureBarrier(Texture& texture, TextureUse from, TextureUse to) override;

    // beginFrame's half of the per-frame rotation: point this command list at the frame's
    // argument table and uniform ring. `uniformOffset` is the device's bump cursor for that
    // ring, already rewound to zero. Must be called before any encoding in the frame; asserts
    // no pass is open.
    void resetForFrame(MTL4::ArgumentTable* argumentTable, MTL::Buffer* uniformRing,
                       uint64_t* uniformOffset);

    // endFrame's half: forget the frame's table and ring once its work is committed, so that
    // encoding after endFrame fails our assert rather than quietly writing a slot the GPU owns.
    // Also where an unconsumed textureBarrier is caught -- see the .cpp.
    void endFrameReset();

    // True between beginRenderPass and endRenderPass. Metal4Device checks it so that ending a
    // frame with an open encoder is reported here rather than as a Metal abort at commit time.
    bool inRenderPass() const { return static_cast<bool>(m_encoder); }

private:
    MTL4::CommandBuffer* m_commandBuffer = nullptr;
    MTL4::ArgumentTable* m_argumentTable = nullptr;
    MTL::Buffer* m_uniformRing = nullptr;
    uint64_t* m_uniformOffset = nullptr;
    NS::SharedPtr<MTL4::RenderCommandEncoder> m_encoder;
    // Set by textureBarrier (which runs between passes, where there is no encoder to record on)
    // and consumed by the next beginRenderPass. See textureBarrier's note in the .cpp.
    //
    // A bool is sufficient *only* because exactly one edge exists (RenderTarget -> ShaderRead), so
    // "a barrier is pending" fully determines the stage pair to encode. The second edge -- M3's
    // shadow map is the likely first -- must carry the stage pair here instead of widening this
    // flag, or two different edges pending at once will silently collapse into one.
    bool m_pendingBarrier = false;
};

} // namespace lmx::rhi::metal4
