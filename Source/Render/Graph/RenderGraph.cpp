//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraph.cpp
/// @brief Implements render-graph declarations, resource access and execution.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/RenderGraph.h"
#include "Render/Graph/RenderGraphInternal.h"

#include "Core/Diagnostics/Assert.h"
#include "Render/Graph/FrameEmitter.h"
#include "Render/Graph/GraphDump.h"

#include <algorithm>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace lmx::render {
using graph_detail::fail;
using graph_detail::isWriteRole;
using graph_detail::textureUseOf;

namespace {

// A cubemap face is one declared array layer.
constexpr uint32_t kCubeFaceCount = 6;

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
std::string_view formatName(rojoRHI::Format format) {
    switch (format) {
    case rojoRHI::Format::Unknown:
        return "Unknown";
    case rojoRHI::Format::BGRA8Unorm:
        return "BGRA8Unorm";
    case rojoRHI::Format::RGBA8Unorm:
        return "RGBA8Unorm";
    case rojoRHI::Format::RGBA8Unorm_sRGB:
        return "RGBA8Unorm_sRGB";
    case rojoRHI::Format::RGBA16Float:
        return "RGBA16Float";
    case rojoRHI::Format::R16Float:
        return "R16Float";
    case rojoRHI::Format::R32Float:
        return "R32Float";
    case rojoRHI::Format::RG16Float:
        return "RG16Float";
    case rojoRHI::Format::R8Unorm:
        return "R8Unorm";
    case rojoRHI::Format::BC1Unorm:
        return "BC1Unorm";
    case rojoRHI::Format::BC1Unorm_sRGB:
        return "BC1Unorm_sRGB";
    case rojoRHI::Format::D32Float:
        return "D32Float";
    }
    return "Unknown";
}

//======================================================================================================================
std::string describeRange(const rojoRHI::TextureSubresourceRange& range) {
    const auto axis = [](std::string_view name, uint32_t base, uint32_t count, uint32_t sentinel) {
        if (count == sentinel) {
            return std::format("{}[{}..]", name, base);
        }
        const uint64_t last = count == 0 ? base : static_cast<uint64_t>(base) + count - 1;
        return std::format("{}[{}..{}]", name, base, last);
    };
    return std::format(
        "{} {}", axis("mips", range.baseMipLevel, range.mipLevelCount, rojoRHI::kAllMipLevels),
        axis("layers", range.baseArrayLayer, range.arrayLayerCount, rojoRHI::kAllArrayLayers));
}

//======================================================================================================================
GraphResult<rojoRHI::Texture*> PassResources::texture(GraphTexture handle) const {
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
GraphResult<rojoRHI::Buffer*> PassResources::buffer(GraphBuffer handle) const {
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
GraphTexture RenderGraph::importTexture(rojoRHI::Texture& texture, rojoRHI::Format format,
                                        std::string_view name) {
    LMX_ASSERT(texture.format() == rojoRHI::Format::Unknown || texture.format() == format,
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
void RenderGraph::bindTextureStates(TextureStateTable& states) {
    LMX_ASSERT(m_textureStates == nullptr || m_textureStates == &states,
               "the graph is already bound to a different texture state table");
    m_textureStates = &states;
}

//======================================================================================================================
TextureStateTable& RenderGraph::textureStates() {
    LMX_ASSERT(m_textureStates != nullptr, "the graph has no texture state table bound");
    return *m_textureStates;
}

//======================================================================================================================
GraphTexture RenderGraph::importPersistentTexture(rojoRHI::Texture& texture, rojoRHI::Format format,
                                                  std::string_view name) {
    LMX_ASSERT(m_textureStates != nullptr,
               std::format("persistent texture '{}' is imported on a graph with no texture state "
                           "table bound",
                           name));
    LMX_ASSERT(texture.format() == rojoRHI::Format::Unknown || texture.format() == format,
               std::format("imported texture '{}' reports format {}, not the declared {}", name,
                           formatName(texture.format()), formatName(format)));
    // Two table imports of one texture would each write a terminal use back, and the table keeps
    // one use per texture.
    LMX_ASSERT(std::ranges::none_of(m_resources,
                                    [&](const Resource& resource) {
                                        return resource.tracked && resource.texture == &texture;
                                    }),
               std::format("persistent texture '{}' is already imported in this graph", name));
    m_resources.push_back({.kind = ResourceKind::Texture,
                           .name = std::string(name),
                           .texture = &texture,
                           .format = format,
                           .width = texture.width(),
                           .height = texture.height(),
                           .mipLevels = texture.mipLevels(),
                           .arrayLayers = texture.arrayLayers(),
                           .priorTextureAccess = m_textureStates->find(texture),
                           .tracked = true});
    return {.index = static_cast<uint32_t>(m_resources.size() - 1), .version = 0};
}

//======================================================================================================================
void RenderGraph::setFinalUse(GraphTexture handle, TextureUse use) {
    checkTexture(handle);
    Resource& resource = m_resources[handle.index];
    LMX_ASSERT(resource.tracked,
               std::format("setFinalUse names texture '{}', which was not imported through "
                           "importPersistentTexture",
                           resource.name));
    resource.finalUse = use;
}

//======================================================================================================================
GraphBuffer RenderGraph::importBuffer(rojoRHI::Buffer& buffer, std::string_view name) {
    m_resources.push_back(
        {.kind = ResourceKind::Buffer, .name = std::string(name), .buffer = &buffer});
    return {.index = static_cast<uint32_t>(m_resources.size() - 1), .version = 0};
}

//======================================================================================================================
GraphBuffer RenderGraph::importBuffer(rojoRHI::Buffer& buffer, std::string_view name,
                                      BufferUse previousUse) {
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
    m_resources.push_back(
        {.kind = ResourceKind::Texture,
         .name = std::string(name),
         .format = desc.format,
         .width = desc.width,
         .height = desc.height,
         .mipLevels = desc.mipLevels,
         .arrayLayers = desc.kind == rojoRHI::TextureKind::Cube ? kCubeFaceCount : 1,
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
GraphResult<LoweredFrame> RenderGraph::lower(uint64_t frameId) const {
    GraphResult<CompiledFrameRecord> record = compileFrame(frameId);
    if (!record) {
        return std::unexpected(std::move(record.error()));
    }

    LoweredFrame frame{.record = std::move(*record)};
    // A transient keeps the pointer its last placement left behind; lowering reports it unplaced,
    // because only execute() knows the frame it is placed for.
    frame.resources.reserve(m_resources.size());
    for (const Resource& resource : m_resources) {
        frame.resources.push_back({.texture = resource.transient ? nullptr : resource.texture,
                                   .buffer = resource.transient ? nullptr : resource.buffer});
    }

    const CompiledFrameDebug& debug = frame.record.debug;
    frame.ops.emplace_back(BeginScopeOp{.label = "lmx.frame"});
    // Transitions are recorded in schedule order and a pass's own are contiguous, so one cursor
    // gathers each exactly where compilation placed it.
    size_t nextTransition = 0;
    for (const uint32_t passIndex : debug.schedule.passes) {
        const Pass& pass = m_passes[passIndex];

        BarriersOp barriers;
        while (nextTransition < debug.transitions.size() &&
               debug.transitions[nextTransition].beforePass == passIndex) {
            const DebugTransition& transition = debug.transitions[nextTransition];
            const bool alias = transition.aliasedFrom.has_value();
            if (transition.kind == GraphResourceKind::Buffer) {
                barriers.barriers.emplace_back(LoweredBufferBarrier{.resource = transition.resource,
                                                                    .from = transition.bufferFrom,
                                                                    .to = transition.bufferTo,
                                                                    .alias = alias});
            } else {
                barriers.barriers.emplace_back(
                    LoweredTextureBarrier{.resource = transition.resource,
                                          .range = transition.range,
                                          .from = transition.textureFrom,
                                          .to = transition.textureTo,
                                          .alias = alias});
            }
            ++nextTransition;
        }
        if (!barriers.barriers.empty()) {
            frame.ops.emplace_back(std::move(barriers));
        }

        const RunOp run{.pass = passIndex, .kind = pass.kind, .label = pass.label};
        switch (pass.kind) {
        case PassKind::Raster:
            frame.ops.emplace_back(beginRenderPassOf(pass));
            frame.ops.emplace_back(run);
            frame.ops.emplace_back(EndRenderPassOp{});
            break;
        case PassKind::Compute:
        case PassKind::Copy:
            frame.ops.emplace_back(BeginScopeOp{.label = pass.label});
            frame.ops.emplace_back(run);
            frame.ops.emplace_back(EndScopeOp{});
            break;
        case PassKind::External:
            frame.ops.emplace_back(run);
            break;
        }
    }
    frame.ops.emplace_back(EndScopeOp{});
    frame.finalStates = finalStatesOf(debug.schedule);
    return frame;
}

//======================================================================================================================
// A culled pass never reaches the GPU, so only scheduled passes count, and the last one naming the
// texture decides. Within that pass a write wins over a read of another range: it is the use that
// leaves the texture in a producing state, which the next frame's first access must wait on.
std::vector<TextureFinalState> RenderGraph::finalStatesOf(const Schedule& schedule) const {
    std::vector<TextureFinalState> states;
    for (uint32_t index = 0; index < m_resources.size(); ++index) {
        const Resource& resource = m_resources[index];
        if (!resource.tracked) {
            continue;
        }
        if (resource.finalUse) {
            states.push_back({.resource = index, .use = *resource.finalUse});
            continue;
        }
        for (const uint32_t passIndex : std::views::reverse(schedule.passes)) {
            const Pass& pass = m_passes[passIndex];
            const Declaration* last = nullptr;
            for (const Declaration& declaration : pass.declarations) {
                if (declaration.resource == index && (last == nullptr || declaration.isWrite)) {
                    last = &declaration;
                    if (declaration.isWrite) {
                        break;
                    }
                }
            }
            if (last != nullptr) {
                states.push_back({.resource = index, .use = textureUseOf(pass.kind, last->role)});
                break;
            }
        }
    }
    return states;
}

//======================================================================================================================
BeginRenderPassOp RenderGraph::beginRenderPassOf(const Pass& pass) const {
    LMX_ASSERT(pass.color.has_value() || pass.depth.has_value(),
               std::format("pass '{}' is a raster pass that declares no attachment: a pass "
                           "with nothing to render into is a compute or copy pass",
                           pass.label));
    const auto colorOf = [](const ColorAttachment& attachment) {
        LoweredColorAttachment lowered{.resource = attachment.handle.index,
                                       .load = attachment.load,
                                       .store = attachment.store};
        std::ranges::copy(attachment.clearColor, lowered.clearColor.begin());
        return lowered;
    };

    BeginRenderPassOp op{.label = pass.label,
                         .renderAreaWidth = pass.renderAreaWidth,
                         .renderAreaHeight = pass.renderAreaHeight};
    if (pass.color) {
        op.color = colorOf(*pass.color);
    }
    for (const ColorAttachment& extra : pass.extraColor) {
        op.extraColor.push_back(colorOf(extra));
    }
    if (pass.depth) {
        op.depth = LoweredDepthAttachment{.resource = pass.depth->handle.index,
                                          .load = pass.depth->load,
                                          .store = pass.depth->store,
                                          .clearDepth = pass.depth->clearDepth};
    }
    return op;
}

//======================================================================================================================
CompiledFrameRecord RenderGraph::execute(rojoRHI::CommandList& commands, uint64_t frameId) {
    GraphResult<LoweredFrame> frame = lower(frameId);
    LMX_ASSERT(frame.has_value(), frame.error().message);
    placeTransients(frame->record.debug);
    // Placement filled the transients' pointers in the resource list; the emitter reads them from
    // the lowered frame alone.
    for (const DebugTransient& entry : frame->record.debug.transients) {
        if (!entry.used) {
            continue;
        }
        const Resource& resource = m_resources[entry.resource];
        frame->resources[entry.resource] = {.texture = resource.texture, .buffer = resource.buffer};
    }
    dumpCompiledFrameIfRequested(frame->record);

    emit(*frame, commands,
         [this](uint32_t passIndex) { m_passes[passIndex].execute(passResources(passIndex)); });
    // The terminal uses are written back only once the frame is recorded, so the next frame's
    // imports see them; a tracked import exists only on a graph with a bound table.
    for (const TextureFinalState& state : frame->finalStates) {
        m_textureStates->set(*frame->resources[state.resource].texture, state.use);
    }
    return std::move(frame->record);
}

//======================================================================================================================
void RenderGraph::placeTransients(const CompiledFrameDebug& debug) {
    if (debug.transients.empty()) {
        return;
    }
    // Reserving before placing anything is what lets the pool decide in one step whether the frame
    // fits the generation it holds -- and a failure here is a device that could not give the frame
    // its memory, which no declaration can recover from.
    const lmx::Result<void> reserved = m_transients->reserve(debug.memory.highWater);
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
            const lmx::Result<rojoRHI::Texture*> texture =
                m_transients->placeTexture(resource.textureDesc, entry.offset, resource.name);
            LMX_ASSERT(texture.has_value(), texture.error().message);
            resource.texture = *texture;
        } else {
            const lmx::Result<rojoRHI::Buffer*> buffer =
                m_transients->placeBuffer(resource.bufferDesc, entry.offset, resource.name);
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
