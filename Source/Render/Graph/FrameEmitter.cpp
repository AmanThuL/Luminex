//----------------------------------------------------------------------------------------------------------------------
/// @file FrameEmitter.cpp
/// @brief Implements recording a lowered frame on a v1 command list.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/FrameEmitter.h"

#include "Core/Diagnostics/Assert.h"

#include <format>
#include <string>
#include <string_view>
#include <variant>

namespace lmx::render {

namespace {

// The enumerators keep v1's values, so a conversion that drifts fails to compile rather than
// recording the wrong barrier.
static_assert(static_cast<int>(TextureUse::RenderTarget) ==
              static_cast<int>(rojoRHI::TextureUse::RenderTarget));
static_assert(static_cast<int>(TextureUse::ShaderRead) ==
              static_cast<int>(rojoRHI::TextureUse::ShaderRead));
static_assert(static_cast<int>(TextureUse::StorageRead) ==
              static_cast<int>(rojoRHI::TextureUse::StorageRead));
static_assert(static_cast<int>(TextureUse::StorageWrite) ==
              static_cast<int>(rojoRHI::TextureUse::StorageWrite));
static_assert(static_cast<int>(TextureUse::CopySource) ==
              static_cast<int>(rojoRHI::TextureUse::CopySource));
static_assert(static_cast<int>(TextureUse::CopyDestination) ==
              static_cast<int>(rojoRHI::TextureUse::CopyDestination));
static_assert(static_cast<int>(TextureUse::ExternalRead) ==
              static_cast<int>(rojoRHI::TextureUse::ExternalRead));
static_assert(static_cast<int>(TextureUse::ExternalWrite) ==
              static_cast<int>(rojoRHI::TextureUse::ExternalWrite));
static_assert(static_cast<int>(BufferUse::ShaderRead) ==
              static_cast<int>(rojoRHI::BufferUse::ShaderRead));
static_assert(static_cast<int>(BufferUse::StorageRead) ==
              static_cast<int>(rojoRHI::BufferUse::StorageRead));
static_assert(static_cast<int>(BufferUse::StorageWrite) ==
              static_cast<int>(rojoRHI::BufferUse::StorageWrite));
static_assert(static_cast<int>(BufferUse::CopySource) ==
              static_cast<int>(rojoRHI::BufferUse::CopySource));
static_assert(static_cast<int>(BufferUse::CopyDestination) ==
              static_cast<int>(rojoRHI::BufferUse::CopyDestination));
static_assert(static_cast<int>(BufferUse::IndirectArgument) ==
              static_cast<int>(rojoRHI::BufferUse::IndirectArgument));

//======================================================================================================================
rojoRHI::TextureUse toRhi(TextureUse use) {
    switch (use) {
    case TextureUse::RenderTarget:
        return rojoRHI::TextureUse::RenderTarget;
    case TextureUse::ShaderRead:
        return rojoRHI::TextureUse::ShaderRead;
    case TextureUse::StorageRead:
        return rojoRHI::TextureUse::StorageRead;
    case TextureUse::StorageWrite:
        return rojoRHI::TextureUse::StorageWrite;
    case TextureUse::CopySource:
        return rojoRHI::TextureUse::CopySource;
    case TextureUse::CopyDestination:
        return rojoRHI::TextureUse::CopyDestination;
    case TextureUse::ExternalRead:
        return rojoRHI::TextureUse::ExternalRead;
    case TextureUse::ExternalWrite:
        return rojoRHI::TextureUse::ExternalWrite;
    }
    return rojoRHI::TextureUse::ShaderRead;
}

//======================================================================================================================
rojoRHI::BufferUse toRhi(BufferUse use) {
    switch (use) {
    case BufferUse::ShaderRead:
        return rojoRHI::BufferUse::ShaderRead;
    case BufferUse::StorageRead:
        return rojoRHI::BufferUse::StorageRead;
    case BufferUse::StorageWrite:
        return rojoRHI::BufferUse::StorageWrite;
    case BufferUse::CopySource:
        return rojoRHI::BufferUse::CopySource;
    case BufferUse::CopyDestination:
        return rojoRHI::BufferUse::CopyDestination;
    case BufferUse::IndirectArgument:
        return rojoRHI::BufferUse::IndirectArgument;
    }
    return rojoRHI::BufferUse::ShaderRead;
}

//======================================================================================================================
// The name a resource was declared under, for a message about it; a hand-built frame may carry no
// record entry for it.
std::string resourceName(const LoweredFrame& frame, uint32_t resource) {
    return resource < frame.record.debug.resources.size()
               ? frame.record.debug.resources[resource].name
               : std::format("#{}", resource);
}

//======================================================================================================================
// The texture behind a resource index. A transient lowers unplaced, so a frame emitted before
// execute() placed it would otherwise dereference null.
rojoRHI::Texture* textureOf(const LoweredFrame& frame, uint32_t resource) {
    LMX_ASSERT(resource < frame.resources.size(),
               std::format("lowered op names resource {}, past the frame's {} resources", resource,
                           frame.resources.size()));
    rojoRHI::Texture* texture = frame.resources[resource].texture;
    LMX_ASSERT(texture != nullptr,
               std::format("texture '{}' has no device object -- a transient must be placed "
                           "before its frame is emitted",
                           resourceName(frame, resource)));
    return texture;
}

//======================================================================================================================
// The buffer counterpart of textureOf.
rojoRHI::Buffer* bufferOf(const LoweredFrame& frame, uint32_t resource) {
    LMX_ASSERT(resource < frame.resources.size(),
               std::format("lowered op names resource {}, past the frame's {} resources", resource,
                           frame.resources.size()));
    rojoRHI::Buffer* buffer = frame.resources[resource].buffer;
    LMX_ASSERT(buffer != nullptr,
               std::format("buffer '{}' has no device object -- a transient must be placed "
                           "before its frame is emitted",
                           resourceName(frame, resource)));
    return buffer;
}

//======================================================================================================================
void emitBarriers(const LoweredFrame& frame, const BarriersOp& op, rojoRHI::CommandList& commands) {
    for (const LoweredBarrier& barrier : op.barriers) {
        if (const auto* texture = std::get_if<LoweredTextureBarrier>(&barrier)) {
            const rojoRHI::BarrierOptions options = texture->alias
                                                        ? rojoRHI::BarrierOptions::ResourceAlias
                                                        : rojoRHI::BarrierOptions::None;
            commands.textureBarrier(*textureOf(frame, texture->resource), texture->range,
                                    toRhi(texture->from), toRhi(texture->to), options);
        } else {
            const auto& buffer = std::get<LoweredBufferBarrier>(barrier);
            const rojoRHI::BarrierOptions options = buffer.alias
                                                        ? rojoRHI::BarrierOptions::ResourceAlias
                                                        : rojoRHI::BarrierOptions::None;
            commands.bufferBarrier(*bufferOf(frame, buffer.resource), rojoRHI::BufferRange{},
                                   toRhi(buffer.from), toRhi(buffer.to), options);
        }
    }
}

//======================================================================================================================
// One colour attachment slot of an RHI render-pass descriptor, filled from a lowered attachment.
// The primary and every extra go through it, so the store rule and its message are written once
// and a slot cannot be filled two subtly different ways. `what` names the attachment in that
// message -- "its colour attachment", "extra color attachment 1".
void fillColorTarget(const LoweredColorAttachment& attachment, rojoRHI::Texture* texture,
                     std::string_view passLabel, std::string_view what, rojoRHI::Texture*& target,
                     bool& clear, float (&clearColor)[4]) {
    LMX_ASSERT(attachment.store == StoreOp::Store,
               std::format("pass '{}' discards {}, which this RHI cannot express -- a colour "
                           "attachment is always stored",
                           passLabel, what));
    target = texture;
    clear = attachment.load == LoadOp::Clear;
    for (size_t channel = 0; channel < 4; ++channel) {
        clearColor[channel] = attachment.clearColor[channel];
    }
}

//======================================================================================================================
void beginRenderPass(const LoweredFrame& frame, const BeginRenderPassOp& op,
                     rojoRHI::CommandList& commands) {
    rojoRHI::RenderPassDesc desc;
    desc.label = op.label;
    if (op.color) {
        fillColorTarget(*op.color, textureOf(frame, op.color->resource), op.label,
                        "its colour attachment", desc.colorTarget, desc.clear, desc.clearColor);
    }
    // Each extra carries its own load action and clear value, so a pass may clear one attachment
    // while loading another.
    LMX_ASSERT(op.extraColor.size() <= rojoRHI::kMaxExtraColorTargets,
               std::format("pass '{}' has {} extra colour attachments; this RHI carries at most {}",
                           op.label, op.extraColor.size(), rojoRHI::kMaxExtraColorTargets));
    for (uint32_t index = 0; index < op.extraColor.size(); ++index) {
        const LoweredColorAttachment& extra = op.extraColor[index];
        fillColorTarget(extra, textureOf(frame, extra.resource), op.label,
                        std::format("extra color attachment {}", index),
                        desc.extraColor[index].target, desc.extraColor[index].clear,
                        desc.extraColor[index].clearColor);
    }
    desc.extraColorCount = static_cast<uint32_t>(op.extraColor.size());
    desc.renderAreaWidth = op.renderAreaWidth;
    desc.renderAreaHeight = op.renderAreaHeight;
    if (op.depth) {
        const LoweredDepthAttachment& depth = *op.depth;
        LMX_ASSERT(depth.load == LoadOp::Clear,
                   std::format("pass '{}' loads its depth attachment, which this RHI cannot "
                               "express -- no pass owns depth contents to load",
                               op.label));
        // One load action covers both attachments in this RHI, so a depth clear beside a colour
        // load would silently clear the colour target too.
        LMX_ASSERT(desc.clear,
                   std::format("pass '{}' loads its colour attachment while clearing depth: this "
                               "RHI carries one load action for the whole pass",
                               op.label));
        LMX_ASSERT(op.color || depth.store == StoreOp::Store,
                   std::format("pass '{}' is depth-only and discards its depth, leaving it no "
                               "output at all",
                               op.label));
        desc.depthTarget = textureOf(frame, depth.resource);
        desc.clearDepth = depth.clearDepth;
        desc.storeDepth = depth.store == StoreOp::Store;
    }
    commands.beginRenderPass(desc);
}

//======================================================================================================================
// A compute or copy body records inside an encoder of its kind; a raster body is already inside its
// render pass and an external one owns its own encoding.
void runPass(const RunOp& op, rojoRHI::CommandList& commands, const PassRunner& run) {
    switch (op.kind) {
    case PassKind::Raster:
    case PassKind::External:
        run(op.pass);
        break;
    case PassKind::Compute:
        commands.beginComputePass(op.label);
        run(op.pass);
        commands.endComputePass();
        break;
    case PassKind::Copy:
        commands.beginCopyPass(op.label);
        run(op.pass);
        commands.endCopyPass();
        break;
    }
}

} // namespace

//======================================================================================================================
void emit(const LoweredFrame& frame, rojoRHI::CommandList& commands, const PassRunner& run) {
    for (const LoweredOp& op : frame.ops) {
        if (const auto* barriers = std::get_if<BarriersOp>(&op)) {
            emitBarriers(frame, *barriers, commands);
        } else if (const auto* begin = std::get_if<BeginRenderPassOp>(&op)) {
            beginRenderPass(frame, *begin, commands);
        } else if (std::holds_alternative<EndRenderPassOp>(op)) {
            commands.endRenderPass();
        } else if (const auto* body = std::get_if<RunOp>(&op)) {
            runPass(*body, commands, run);
        }
        // Scopes record nothing here: this command list times every pass itself.
    }
}

} // namespace lmx::render
