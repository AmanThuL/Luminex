//----------------------------------------------------------------------------------------------------------------------
/// @file FrameEmitter.cpp
/// @brief Records physical barrier batches, render passes and scopes.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/FrameEmitter.h"
#include "Core/Diagnostics/Assert.h"
#include <array>
#include <format>
#include <variant>
#include <vector>

namespace lmx::render {
namespace {

//======================================================================================================================
std::string resourceName(const LoweredFrame& frame, uint32_t resource) {
    return resource < frame.record.debug.resources.size()
               ? frame.record.debug.resources[resource].name
               : std::format("#{}", resource);
}

//======================================================================================================================
rojoRHI::Texture textureOf(const LoweredFrame& frame, uint32_t resource) {
    LMX_ASSERT(resource < frame.resources.size(),
               std::format("lowered op names resource {}, past the frame's {} resources", resource,
                           frame.resources.size()));
    const auto texture = frame.resources[resource].texture.texture;
    LMX_ASSERT(texture.bits != 0,
               std::format("texture '{}' has no device object -- a transient must be placed "
                           "before its frame is emitted",
                           resourceName(frame, resource)));
    return texture;
}

//======================================================================================================================
rojoRHI::Buffer bufferOf(const LoweredFrame& frame, uint32_t resource) {
    LMX_ASSERT(resource < frame.resources.size(),
               std::format("lowered op names resource {}, past the frame's {} resources", resource,
                           frame.resources.size()));
    const auto buffer = frame.resources[resource].buffer.buffer;
    LMX_ASSERT(buffer.bits != 0,
               std::format("buffer '{}' has no device object -- a transient must be placed "
                           "before its frame is emitted",
                           resourceName(frame, resource)));
    return buffer;
}

//======================================================================================================================
void emitBarriers(const LoweredFrame& frame, const BarriersOp& op, rojoRHI::CommandList& commands) {
    std::vector<rojoRHI::GlobalBarrier> globals;
    std::vector<rojoRHI::BufferBarrier> buffers;
    std::vector<rojoRHI::TextureBarrier> textures;
    for (const auto& barrier : op.barriers) {
        if (const auto* texture = std::get_if<LoweredTextureBarrier>(&barrier)) {
            textures.push_back({textureOf(frame, texture->resource), texture->range,
                                texture->from.stage, texture->from.access, texture->to.stage,
                                texture->to.access, texture->from.layout, texture->to.layout});
        } else {
            const auto& buffer = std::get<LoweredBufferBarrier>(barrier);
            if (buffer.alias) {
                globals.push_back(
                    {buffer.from.stage, buffer.from.access, buffer.to.stage, buffer.to.access});
            } else {
                buffers.push_back({bufferOf(frame, buffer.resource), 0, rojoRHI::kWholeSize,
                                   buffer.from.stage, buffer.from.access, buffer.to.stage,
                                   buffer.to.access});
            }
        }
    }
    if (!globals.empty() || !buffers.empty() || !textures.empty()) {
        commands.barrier(globals, buffers, textures);
    }
}

//======================================================================================================================
rojoRHI::LoadOp loadOp(LoadOp load) {
    return load == LoadOp::Clear ? rojoRHI::LoadOp::Clear : rojoRHI::LoadOp::Load;
}

//======================================================================================================================
rojoRHI::StoreOp storeOp(StoreOp store) {
    return store == StoreOp::Store ? rojoRHI::StoreOp::Store : rojoRHI::StoreOp::DontCare;
}

//======================================================================================================================
void beginRenderPass(const LoweredFrame& frame, const BeginRenderPassOp& op,
                     rojoRHI::CommandList& commands) {
    std::vector<rojoRHI::ColorAttachment> colors;
    const auto addColor = [&](const LoweredColorAttachment& attachment) {
        const auto& c = attachment.clearColor;
        colors.push_back({.texture = textureOf(frame, attachment.resource),
                          .load = loadOp(attachment.load),
                          .store = storeOp(attachment.store),
                          .clear = {c[0], c[1], c[2], c[3]}});
    };
    if (op.color) {
        addColor(*op.color);
    }
    for (const auto& color : op.extraColor) {
        addColor(color);
    }
    rojoRHI::DepthAttachment depth;
    if (op.depth) {
        depth = {.texture = textureOf(frame, op.depth->resource),
                 .load = loadOp(op.depth->load),
                 .store = storeOp(op.depth->store),
                 .clearDepth = op.depth->clearDepth};
    }
    const uint32_t first = op.color ? op.color->resource : op.depth->resource;
    const auto extent = frame.resources[first].texture.extent;
    const uint32_t width = op.renderAreaWidth == 0 ? extent.width : op.renderAreaWidth;
    const uint32_t height = op.renderAreaHeight == 0 ? extent.height : op.renderAreaHeight;
    commands.beginRenderPass({colors, depth, {0, 0, width, height}, op.label});
    commands.setViewport({0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1});
    commands.setScissor({0, 0, width, height});
}
} // namespace

//======================================================================================================================
void emit(const LoweredFrame& frame, rojoRHI::CommandList& commands, const PassRunner& run) {
    for (const auto& op : frame.ops) {
        if (const auto* barriers = std::get_if<BarriersOp>(&op)) {
            emitBarriers(frame, *barriers, commands);
        } else if (const auto* pass = std::get_if<BeginRenderPassOp>(&op)) {
            beginRenderPass(frame, *pass, commands);
        } else if (std::holds_alternative<EndRenderPassOp>(op)) {
            commands.endRenderPass();
        } else if (const auto* scope = std::get_if<BeginScopeOp>(&op)) {
            commands.beginScope(scope->label);
        } else if (std::holds_alternative<EndScopeOp>(op)) {
            commands.endScope();
        } else if (const auto* body = std::get_if<RunOp>(&op)) {
            run(body->pass);
        }
    }
}
} // namespace lmx::render
