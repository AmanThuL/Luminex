#pragma once
#include "RHI/Metal4/Metal4Common.h"

#include <cstdint>

namespace lmx::rhi::metal4 {

// The recording half of the frame protocol. One instance is owned by Metal4Device and handed
// back from every beginFrame(); it records into the device's single MTL4::CommandBuffer, which
// beginFrame has already re-opened against this frame's allocator.
//
// It owns nothing that outlives a frame. The command buffer and the argument table belong to
// the device (which outlives every command list it hands out), so both are held as raw
// pointers; only the render command encoder -- created and destroyed inside a single
// beginRenderPass/endRenderPass pair -- is reference-counted here, because
// renderCommandEncoder() returns an autoreleased (+0) object that must survive the local
// autorelease pool it was created in.
//
// The argument table pointer is *not* fixed at construction: the device owns one table per
// frame in flight and hands this list the current frame's through resetForFrame, so a table is
// only ever written while its frame is the open one.
//
// Encoder-scoped calls (bindPipeline/bindVertexBuffer/draw) assert rather than return errors:
// calling them outside a pass is a caller sequencing bug, and the RHI CommandList methods
// return void. They also hold no autorelease pool of their own -- none of them invokes an
// autoreleasing selector (they are direct setters and draws on an already-retained encoder), so
// a per-call pool bought nothing and cost a create/drain on what becomes the hottest path in
// the backend. The pools stay where objects are actually autoreleased: around the encoder's
// creation and teardown.
class Metal4CommandList final : public CommandList {
public:
    explicit Metal4CommandList(MTL4::CommandBuffer* commandBuffer)
        : m_commandBuffer(commandBuffer) {}

    Metal4CommandList(const Metal4CommandList&) = delete;
    Metal4CommandList& operator=(const Metal4CommandList&) = delete;

    void beginRenderPass(const RenderPassDesc& desc) override;
    void bindPipeline(GraphicsPipeline& pipeline) override;
    void bindVertexBuffer(uint32_t slot, Buffer& buffer) override;
    void draw(uint32_t vertexCount, uint32_t firstVertex) override;
    void endRenderPass() override;

    // beginFrame's half of the per-frame rotation: point this command list at the frame's
    // argument table. Must be called before any encoding in the frame; asserts no pass is open.
    void resetForFrame(MTL4::ArgumentTable* argumentTable);

    // True between beginRenderPass and endRenderPass. Metal4Device checks it so that ending a
    // frame with an open encoder is reported here rather than as a Metal abort at commit time.
    bool inRenderPass() const { return static_cast<bool>(m_encoder); }

private:
    MTL4::CommandBuffer* m_commandBuffer = nullptr;
    MTL4::ArgumentTable* m_argumentTable = nullptr;
    NS::SharedPtr<MTL4::RenderCommandEncoder> m_encoder;
};

} // namespace lmx::rhi::metal4
