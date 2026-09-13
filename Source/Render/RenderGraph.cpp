//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraph.cpp
/// @brief Implements render-graph declarations, resource access and execution.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/RenderGraph.h"
#include "Render/RenderGraphInternal.h"

#include "Core/Assert.h"
#include "Render/GraphDump.h"

#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lmx::render {
using graph_detail::fail;
using graph_detail::isWriteRole;

namespace {

// A cubemap face is one declared array layer.
constexpr uint32_t kCubeFaceCount = 6;

//======================================================================================================================
// One colour attachment slot of an RHI render-pass descriptor, filled from a declared attachment.
// The primary and every extra go through it, so the store rule and its message are written once
// and a slot cannot be filled two subtly different ways. `what` names the attachment in that
// message -- "its colour attachment", "extra color attachment 1".
void fillColorTarget(const ColorAttachment& attachment, rhi::Texture* texture,
                     std::string_view passLabel, std::string_view what, rhi::Texture*& target,
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

} // namespace

//======================================================================================================================
std::string_view roleName(UseRole role) {
    switch (role) {
    case UseRole::Read:
        return "read";
    case UseRole::ShaderRead:
        return "shader read";
    case UseRole::IndirectArgument:
        return "indirect argument";
    case UseRole::Write:
        return "write";
    case UseRole::ColorAttachment:
        return "color attachment";
    case UseRole::DepthAttachment:
        return "depth attachment";
    case UseRole::CopySource:
        return "copy source";
    case UseRole::CopyDestination:
        return "copy destination";
    }
    return "use";
}

//======================================================================================================================
// Omitting default lets -Wswitch catch a newly added format.
std::string_view formatName(rhi::Format format) {
    switch (format) {
    case rhi::Format::Unknown:
        return "Unknown";
    case rhi::Format::BGRA8Unorm:
        return "BGRA8Unorm";
    case rhi::Format::RGBA8Unorm:
        return "RGBA8Unorm";
    case rhi::Format::RGBA8Unorm_sRGB:
        return "RGBA8Unorm_sRGB";
    case rhi::Format::RGBA16Float:
        return "RGBA16Float";
    case rhi::Format::R16Float:
        return "R16Float";
    case rhi::Format::RG16Float:
        return "RG16Float";
    case rhi::Format::R8Unorm:
        return "R8Unorm";
    case rhi::Format::BC1Unorm:
        return "BC1Unorm";
    case rhi::Format::BC1Unorm_sRGB:
        return "BC1Unorm_sRGB";
    case rhi::Format::D32Float:
        return "D32Float";
    }
    return "Unknown";
}

//======================================================================================================================
std::string describeRange(const rhi::TextureSubresourceRange& range) {
    const auto axis = [](std::string_view name, uint32_t base, uint32_t count, uint32_t sentinel) {
        if (count == sentinel) {
            return std::format("{}[{}..]", name, base);
        }
        const uint64_t last = count == 0 ? base : static_cast<uint64_t>(base) + count - 1;
        return std::format("{}[{}..{}]", name, base, last);
    };
    return std::format(
        "{} {}", axis("mips", range.baseMipLevel, range.mipLevelCount, rhi::kAllMipLevels),
        axis("layers", range.baseArrayLayer, range.arrayLayerCount, rhi::kAllArrayLayers));
}

//======================================================================================================================
GraphResult<rhi::Texture*> PassResources::texture(GraphTexture handle) const {
    LMX_ASSERT(handle.index < m_graph->m_resources.size(), "GraphTexture names no resource");
    const RenderGraph::Resource& resource = m_graph->m_resources[handle.index];
    LMX_ASSERT(resource.kind == RenderGraph::ResourceKind::Texture,
               "GraphTexture names an imported buffer");

    if (!m_graph->passDeclares(m_passIndex, handle.index, handle.version)) {
        return fail(std::format("pass '{}' resolves texture '{}' version {}, which it did not "
                                "declare",
                                m_graph->m_passes[m_passIndex].label, resource.name,
                                handle.version));
    }
    // A transient exists only while its frame is being executed, so resolving one outside execute()
    // is a caller reaching for memory that was never asked for rather than a mis-declaration.
    LMX_ASSERT(resource.texture != nullptr,
               std::format("transient texture '{}' is not placed: a transient exists only while "
                           "the graph that declared it is executing",
                           resource.name));
    return resource.texture;
}

//======================================================================================================================
GraphResult<rhi::Buffer*> PassResources::buffer(GraphBuffer handle) const {
    LMX_ASSERT(handle.index < m_graph->m_resources.size(), "GraphBuffer names no resource");
    const RenderGraph::Resource& resource = m_graph->m_resources[handle.index];
    LMX_ASSERT(resource.kind == RenderGraph::ResourceKind::Buffer,
               "GraphBuffer names an imported texture");

    if (!m_graph->passDeclares(m_passIndex, handle.index, handle.version)) {
        return fail(std::format("pass '{}' resolves buffer '{}' version {}, which it did not "
                                "declare",
                                m_graph->m_passes[m_passIndex].label, resource.name,
                                handle.version));
    }
    LMX_ASSERT(resource.buffer != nullptr,
               std::format("transient buffer '{}' is not placed: a transient exists only while "
                           "the graph that declared it is executing",
                           resource.name));
    return resource.buffer;
}

//======================================================================================================================
GraphTexture RenderGraph::importTexture(rhi::Texture& texture, rhi::Format format,
                                        std::string_view name) {
    LMX_ASSERT(texture.format() == rhi::Format::Unknown || texture.format() == format,
               std::format("imported texture '{}' reports format {}, not the declared {}", name,
                           formatName(texture.format()), formatName(format)));
    // Shape is read once, here, because a texture's extent and chain are fixed for its lifetime and
    // every later rule has to consult them the same way it consults a transient's descriptor.
    m_resources.push_back({.kind = ResourceKind::Texture,
                           .name = std::string(name),
                           .texture = &texture,
                           .format = format,
                           .width = texture.width(),
                           .height = texture.height(),
                           .mipLevels = texture.mipLevels(),
                           .arrayLayers = texture.arrayLayers()});
    return {.index = static_cast<uint32_t>(m_resources.size() - 1), .version = 0};
}

//======================================================================================================================
GraphTexture RenderGraph::importTexture(rhi::Texture& texture, rhi::Format format,
                                        std::string_view name, rhi::TextureUse previousUse) {
    LMX_ASSERT(texture.format() == rhi::Format::Unknown || texture.format() == format,
               std::format("imported texture '{}' reports format {}, not the declared {}", name,
                           formatName(texture.format()), formatName(format)));
    m_resources.push_back({.kind = ResourceKind::Texture,
                           .name = std::string(name),
                           .texture = &texture,
                           .format = format,
                           .width = texture.width(),
                           .height = texture.height(),
                           .mipLevels = texture.mipLevels(),
                           .arrayLayers = texture.arrayLayers(),
                           .priorTextureAccess = previousUse});
    return {.index = static_cast<uint32_t>(m_resources.size() - 1), .version = 0};
}

//======================================================================================================================
GraphBuffer RenderGraph::importBuffer(rhi::Buffer& buffer, std::string_view name) {
    m_resources.push_back(
        {.kind = ResourceKind::Buffer, .name = std::string(name), .buffer = &buffer});
    return {.index = static_cast<uint32_t>(m_resources.size() - 1), .version = 0};
}

//======================================================================================================================
GraphBuffer RenderGraph::importBuffer(rhi::Buffer& buffer, std::string_view name,
                                      rhi::BufferUse previousUse) {
    m_resources.push_back({.kind = ResourceKind::Buffer,
                           .name = std::string(name),
                           .buffer = &buffer,
                           .priorBufferAccess = previousUse});
    return {.index = static_cast<uint32_t>(m_resources.size() - 1), .version = 0};
}

//======================================================================================================================
GraphTexture RenderGraph::createTexture(const TransientTextureDesc& desc, std::string_view name) {
    LMX_ASSERT(m_transients != nullptr,
               std::format("transient texture '{}' is declared on a graph with no TransientPool: "
                           "a graph that creates resources needs somewhere to place them",
                           name));
    m_resources.push_back({.kind = ResourceKind::Texture,
                           .name = std::string(name),
                           .format = desc.format,
                           .width = desc.width,
                           .height = desc.height,
                           .mipLevels = desc.mipLevels,
                           .arrayLayers = desc.kind == rhi::TextureKind::Cube ? kCubeFaceCount : 1,
                           .transient = true,
                           .textureDesc = desc});
    return {.index = static_cast<uint32_t>(m_resources.size() - 1), .version = 0};
}

//======================================================================================================================
GraphBuffer RenderGraph::createBuffer(const TransientBufferDesc& desc, std::string_view name) {
    LMX_ASSERT(m_transients != nullptr,
               std::format("transient buffer '{}' is declared on a graph with no TransientPool: "
                           "a graph that creates resources needs somewhere to place them",
                           name));
    m_resources.push_back({.kind = ResourceKind::Buffer,
                           .name = std::string(name),
                           .transient = true,
                           .bufferDesc = desc});
    return {.index = static_cast<uint32_t>(m_resources.size() - 1), .version = 0};
}

//======================================================================================================================
rhi::TextureDesc RenderGraph::textureDescOf(const Resource& resource) const {
    const TransientTextureDesc& desc = resource.textureDesc;
    return {.width = desc.width,
            .height = desc.height,
            .format = desc.format,
            .kind = desc.kind,
            .mipLevels = desc.mipLevels,
            .renderTarget = desc.renderTarget,
            .sampled = desc.sampled,
            .storageRead = desc.storageRead,
            .storageWrite = desc.storageWrite,
            .cpuReadback = false,
            .label = resource.name};
}

//======================================================================================================================
rhi::BufferDesc RenderGraph::bufferDescOf(const Resource& resource) const {
    return {.size = resource.bufferDesc.size,
            .storageRead = resource.bufferDesc.storageRead,
            .storageWrite = resource.bufferDesc.storageWrite,
            .cpuReadback = false,
            .label = resource.name};
}

//======================================================================================================================
void RenderGraph::checkTexture(GraphTexture handle) const {
    LMX_ASSERT(handle.index < m_resources.size(), "GraphTexture names no resource");
    LMX_ASSERT(m_resources[handle.index].kind == ResourceKind::Texture,
               "GraphTexture names an imported buffer");
}

//======================================================================================================================
void RenderGraph::checkBuffer(GraphBuffer handle) const {
    LMX_ASSERT(handle.index < m_resources.size(), "GraphBuffer names no resource");
    LMX_ASSERT(m_resources[handle.index].kind == ResourceKind::Buffer,
               "GraphBuffer names an imported texture");
}

//======================================================================================================================
void RenderGraph::flattenTextures(std::vector<Declaration>& into,
                                  const std::vector<TextureUseDesc>& uses, UseRole role) const {
    for (const TextureUseDesc& use : uses) {
        checkTexture(use.handle);
        into.push_back({.resource = use.handle.index,
                        .version = use.handle.version,
                        .role = role,
                        .range = use.range,
                        .isWrite = isWriteRole(role)});
    }
}

//======================================================================================================================
void RenderGraph::flattenBuffers(std::vector<Declaration>& into,
                                 const std::vector<GraphBuffer>& handles, UseRole role) const {
    for (const GraphBuffer& handle : handles) {
        checkBuffer(handle);
        into.push_back({.resource = handle.index,
                        .version = handle.version,
                        .role = role,
                        .range = {},
                        .isWrite = isWriteRole(role)});
    }
}

//======================================================================================================================
void RenderGraph::addPass(std::string_view label, PassDesc desc, ExecuteFn execute) {
    LMX_ASSERT(static_cast<bool>(execute), "a declared pass must carry a body");

    std::vector<Declaration> declarations;
    flattenTextures(declarations, desc.textureReads, UseRole::Read);
    flattenBuffers(declarations, desc.bufferReads, UseRole::Read);
    flattenBuffers(declarations, desc.indirectBufferReads, UseRole::IndirectArgument);
    // Every attachment is declared the same way -- a whole-resource write of the version it names
    // -- so only the handle and the role differ between them.
    const auto declareAttachment = [&](GraphTexture handle, UseRole role) {
        checkTexture(handle);
        declarations.push_back({.resource = handle.index,
                                .version = handle.version,
                                .role = role,
                                .range = {},
                                .isWrite = true});
    };
    if (desc.color) {
        declareAttachment(desc.color->handle, UseRole::ColorAttachment);
    }
    // Extras follow the primary, in attachment order, so the flattened list reads in the order the
    // hardware binds them.
    for (const ColorAttachment& extra : desc.extraColor) {
        declareAttachment(extra.handle, UseRole::ColorAttachment);
    }
    if (desc.depth) {
        declareAttachment(desc.depth->handle, UseRole::DepthAttachment);
    }
    flattenTextures(declarations, desc.textureWrites, UseRole::Write);
    flattenBuffers(declarations, desc.bufferWrites, UseRole::Write);

    m_passes.push_back({.label = std::string(label),
                        .kind = PassKind::Raster,
                        .color = desc.color,
                        .extraColor = std::move(desc.extraColor),
                        .depth = desc.depth,
                        .renderAreaWidth = desc.renderAreaWidth,
                        .renderAreaHeight = desc.renderAreaHeight,
                        .execute = std::move(execute),
                        .declarations = std::move(declarations)});
}

//======================================================================================================================
void RenderGraph::addComputePass(std::string_view label, ComputePassDesc desc, ExecuteFn execute) {
    LMX_ASSERT(static_cast<bool>(execute), "a declared pass must carry a body");

    std::vector<Declaration> declarations;
    flattenTextures(declarations, desc.textureReads, UseRole::Read);
    flattenTextures(declarations, desc.shaderTextureReads, UseRole::ShaderRead);
    flattenBuffers(declarations, desc.bufferReads, UseRole::Read);
    flattenBuffers(declarations, desc.shaderBufferReads, UseRole::ShaderRead);
    flattenBuffers(declarations, desc.indirectBufferReads, UseRole::IndirectArgument);
    flattenTextures(declarations, desc.textureWrites, UseRole::Write);
    flattenBuffers(declarations, desc.bufferWrites, UseRole::Write);

    m_passes.push_back({.label = std::string(label),
                        .kind = PassKind::Compute,
                        .execute = std::move(execute),
                        .declarations = std::move(declarations)});
}

//======================================================================================================================
void RenderGraph::addCopyPass(std::string_view label, CopyPassDesc desc, ExecuteFn execute) {
    LMX_ASSERT(static_cast<bool>(execute), "a declared pass must carry a body");

    std::vector<Declaration> declarations;
    flattenTextures(declarations, desc.textureSources, UseRole::CopySource);
    flattenBuffers(declarations, desc.bufferSources, UseRole::CopySource);
    flattenTextures(declarations, desc.textureDestinations, UseRole::CopyDestination);
    flattenBuffers(declarations, desc.bufferDestinations, UseRole::CopyDestination);

    m_passes.push_back({.label = std::string(label),
                        .kind = PassKind::Copy,
                        .execute = std::move(execute),
                        .declarations = std::move(declarations)});
}

//======================================================================================================================
void RenderGraph::addExternalPass(std::string_view label, ExternalPassDesc desc,
                                  ExecuteFn execute) {
    LMX_ASSERT(static_cast<bool>(execute), "a declared pass must carry a body");

    std::vector<Declaration> declarations;
    flattenTextures(declarations, desc.textureReads, UseRole::Read);
    flattenTextures(declarations, desc.textureWrites, UseRole::Write);

    m_passes.push_back({.label = std::string(label),
                        .kind = PassKind::External,
                        .execute = std::move(execute),
                        .declarations = std::move(declarations)});
}

//======================================================================================================================
void RenderGraph::addSink(SinkKind kind, ResourceKind resourceKind, uint32_t resource,
                          uint32_t version) {
    m_sinks.push_back(
        {.kind = kind, .resourceKind = resourceKind, .resource = resource, .version = version});
}

//======================================================================================================================
void RenderGraph::exportTexture(GraphTexture handle) {
    checkTexture(handle);
    addSink(SinkKind::Export, ResourceKind::Texture, handle.index, handle.version);
}

//======================================================================================================================
void RenderGraph::exportBuffer(GraphBuffer handle) {
    checkBuffer(handle);
    addSink(SinkKind::Export, ResourceKind::Buffer, handle.index, handle.version);
}

//======================================================================================================================
void RenderGraph::presentTexture(GraphTexture handle) {
    checkTexture(handle);
    addSink(SinkKind::Present, ResourceKind::Texture, handle.index, handle.version);
}

//======================================================================================================================
void RenderGraph::readbackTexture(GraphTexture handle) {
    checkTexture(handle);
    addSink(SinkKind::Readback, ResourceKind::Texture, handle.index, handle.version);
}

//======================================================================================================================
void RenderGraph::readbackBuffer(GraphBuffer handle) {
    checkBuffer(handle);
    addSink(SinkKind::Readback, ResourceKind::Buffer, handle.index, handle.version);
}

//======================================================================================================================
const ColorAttachment& RenderGraph::colorAttachmentOf(const Pass& pass,
                                                      const Declaration& declaration) const {
    LMX_ASSERT(declaration.role == UseRole::ColorAttachment,
               "only a colour-attachment declaration comes from a ColorAttachment");
    if (pass.color && pass.color->handle.index == declaration.resource) {
        return *pass.color;
    }
    const auto extra = std::ranges::find(
        pass.extraColor, declaration.resource,
        [](const ColorAttachment& attachment) { return attachment.handle.index; });
    LMX_ASSERT(extra != pass.extraColor.end(),
               "a colour-attachment declaration must come from an attachment of its pass");
    return *extra;
}

//======================================================================================================================
bool RenderGraph::passDeclares(uint32_t passIndex, uint32_t resourceIndex, uint32_t version) const {
    LMX_ASSERT(passIndex < m_passes.size(), "pass index names no declared pass");
    for (const Declaration& declaration : m_passes[passIndex].declarations) {
        if (declaration.resource == resourceIndex && declaration.version == version) {
            return true;
        }
    }
    return false;
}

//======================================================================================================================
CompiledFrameRecord RenderGraph::execute(rhi::CommandList& commands, uint64_t frameId) {
    GraphResult<CompiledFrameRecord> record = compileFrame(frameId);
    LMX_ASSERT(record.has_value(), record.error().message);
    placeTransients(record->debug);
    dumpCompiledFrameIfRequested(*record);

    // Transitions are recorded in schedule order and a pass's own are contiguous, so one cursor
    // emits each exactly where compilation placed it.
    size_t nextTransition = 0;
    for (const uint32_t passIndex : record->debug.schedule.passes) {
        const Pass& pass = m_passes[passIndex];

        while (nextTransition < record->debug.transitions.size() &&
               record->debug.transitions[nextTransition].beforePass == passIndex) {
            const DebugTransition& transition = record->debug.transitions[nextTransition];
            const Resource& resource = m_resources[transition.resource];
            const rhi::BarrierOptions options = transition.aliasedFrom
                                                    ? rhi::BarrierOptions::ResourceAlias
                                                    : rhi::BarrierOptions::None;
            if (transition.kind == GraphResourceKind::Buffer) {
                commands.bufferBarrier(*resource.buffer, rhi::BufferRange{}, transition.bufferFrom,
                                       transition.bufferTo, options);
            } else {
                commands.textureBarrier(*resource.texture, transition.range, transition.textureFrom,
                                        transition.textureTo, options);
            }
            ++nextTransition;
        }

        switch (pass.kind) {
        case PassKind::Raster: {
            LMX_ASSERT(pass.color.has_value() || pass.depth.has_value(),
                       std::format("pass '{}' is a raster pass that declares no attachment: a pass "
                                   "with nothing to render into is a compute or copy pass",
                                   pass.label));
            rhi::RenderPassDesc desc;
            desc.label = pass.label;
            if (pass.color) {
                fillColorTarget(*pass.color, m_resources[pass.color->handle.index].texture,
                                pass.label, "its colour attachment", desc.colorTarget, desc.clear,
                                desc.clearColor);
            }
            // Each extra carries its own load action and clear value, so a pass may clear one
            // attachment while loading another.
            for (uint32_t index = 0; index < pass.extraColor.size(); ++index) {
                const ColorAttachment& extra = pass.extraColor[index];
                fillColorTarget(extra, m_resources[extra.handle.index].texture, pass.label,
                                std::format("extra color attachment {}", index),
                                desc.extraColor[index].target, desc.extraColor[index].clear,
                                desc.extraColor[index].clearColor);
            }
            desc.extraColorCount = static_cast<uint32_t>(pass.extraColor.size());
            desc.renderAreaWidth = pass.renderAreaWidth;
            desc.renderAreaHeight = pass.renderAreaHeight;
            if (pass.depth) {
                const DepthAttachment& depth = *pass.depth;
                LMX_ASSERT(
                    depth.load == LoadOp::Clear,
                    std::format("pass '{}' loads its depth attachment, which this RHI cannot "
                                "express -- no pass owns depth contents to load",
                                pass.label));
                // One load action covers both attachments in this RHI, so a depth clear beside a
                // colour load would silently clear the colour target too.
                LMX_ASSERT(
                    desc.clear,
                    std::format("pass '{}' loads its colour attachment while clearing depth: "
                                "this RHI carries one load action for the whole pass",
                                pass.label));
                LMX_ASSERT(pass.color || depth.store == StoreOp::Store,
                           std::format("pass '{}' is depth-only and discards its depth, leaving it "
                                       "no output at all",
                                       pass.label));
                desc.depthTarget = m_resources[depth.handle.index].texture;
                desc.clearDepth = depth.clearDepth;
                desc.storeDepth = depth.store == StoreOp::Store;
            }
            commands.beginRenderPass(desc);
            pass.execute(passResources(passIndex));
            commands.endRenderPass();
            break;
        }
        case PassKind::Compute:
            commands.beginComputePass(pass.label);
            pass.execute(passResources(passIndex));
            commands.endComputePass();
            break;
        case PassKind::External:
            pass.execute(passResources(passIndex));
            break;
        case PassKind::Copy:
            commands.beginCopyPass(pass.label);
            pass.execute(passResources(passIndex));
            commands.endCopyPass();
            break;
        }
    }
    return std::move(*record);
}

//======================================================================================================================
void RenderGraph::placeTransients(const CompiledFrameDebug& debug) {
    if (debug.transients.empty()) {
        return;
    }
    // Reserving before placing anything is what lets the pool decide in one step whether the frame
    // fits the generation it holds -- and a failure here is a device that could not give the frame
    // its memory, which no declaration can recover from.
    const rhi::Result<void> reserved = m_transients->reserve(debug.memory.highWater);
    LMX_ASSERT(reserved.has_value(), reserved.error().message);
    if (debug.memory.highWater == 0) {
        return;
    }

    for (const DebugTransient& entry : debug.transients) {
        if (!entry.used) {
            continue;
        }
        Resource& resource = m_resources[entry.resource];
        if (resource.kind == ResourceKind::Texture) {
            const rhi::Result<rhi::Texture*> texture =
                m_transients->placeTexture(textureDescOf(resource), entry.offset);
            LMX_ASSERT(texture.has_value(), texture.error().message);
            resource.texture = *texture;
        } else {
            const rhi::Result<rhi::Buffer*> buffer =
                m_transients->placeBuffer(bufferDescOf(resource), entry.offset);
            LMX_ASSERT(buffer.has_value(), buffer.error().message);
            resource.buffer = *buffer;
        }
    }
}

//======================================================================================================================
PassResources RenderGraph::passResources(uint32_t passIndex) const {
    LMX_ASSERT(passIndex < m_passes.size(), "pass index names no declared pass");
    return PassResources(*this, passIndex);
}

} // namespace lmx::render
