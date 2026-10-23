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
std::string describeRange(const rojoRHI::SubresourceRange& range) {
    const auto axis = [](std::string_view name, uint32_t base, uint32_t count, uint32_t sentinel) {
        if (count == sentinel) {
            return std::format("{}[{}..]", name, base);
        }
        const uint64_t last = count == 0 ? base : static_cast<uint64_t>(base) + count - 1;
        return std::format("{}[{}..{}]", name, base, last);
    };
    return std::format("{} {}", axis("mips", range.baseMip, range.mipCount, rojoRHI::kAllMips),
                       axis("layers", range.baseLayer, range.layerCount, rojoRHI::kAllLayers));
}

//======================================================================================================================
const LoweredResource& PassResources::textureResource(GraphTexture handle) const {
    LMX_ASSERT(handle.index < m_frame->record.debug.resources.size(),
               "GraphTexture names no resource");
    const auto& resource = m_frame->record.debug.resources[handle.index];
    LMX_ASSERT(resource.kind == GraphResourceKind::Texture,
               "GraphTexture names an imported buffer");
    const auto& pass = m_frame->record.debug.passes[m_passIndex];
    LMX_ASSERT(std::ranges::any_of(pass.uses,
                                   [&](const DebugUse& use) {
                                       return use.resource == handle.index &&
                                              use.version == handle.version;
                                   }),
               std::format("pass '{}' resolves texture '{}' version {}, which it did not declare",
                           pass.label, resource.name, handle.version));
    LMX_ASSERT(handle.index < m_frame->resources.size() &&
                   m_frame->resources[handle.index].texture.texture.bits != 0,
               std::format("transient texture '{}' is not placed: a transient exists only while "
                           "the graph that declared it is executing",
                           resource.name));
    return m_frame->resources[handle.index];
}

//======================================================================================================================
const LoweredResource& PassResources::bufferResource(GraphBuffer handle) const {
    LMX_ASSERT(handle.index < m_frame->record.debug.resources.size(),
               "GraphBuffer names no resource");
    const auto& resource = m_frame->record.debug.resources[handle.index];
    LMX_ASSERT(resource.kind == GraphResourceKind::Buffer, "GraphBuffer names an imported texture");
    const auto& pass = m_frame->record.debug.passes[m_passIndex];
    LMX_ASSERT(std::ranges::any_of(pass.uses,
                                   [&](const DebugUse& use) {
                                       return use.resource == handle.index &&
                                              use.version == handle.version;
                                   }),
               std::format("pass '{}' resolves buffer '{}' version {}, which it did not declare",
                           pass.label, resource.name, handle.version));
    LMX_ASSERT(handle.index < m_frame->resources.size() &&
                   m_frame->resources[handle.index].buffer.buffer.bits != 0,
               std::format("transient buffer '{}' is not placed: a transient exists only while "
                           "the graph that declared it is executing",
                           resource.name));
    return m_frame->resources[handle.index];
}

//======================================================================================================================
engine::TextureRef PassResources::texture(GraphTexture handle) const {
    return textureResource(handle).texture;
}

//======================================================================================================================
rojoRHI::ViewIndex PassResources::view(GraphTexture handle) const {
    const auto result = textureResource(handle).texture.view;
    LMX_ASSERT(result != engine::kNoView,
               std::format("pass '{}' resolves unavailable sampled view of texture '{}'",
                           m_frame->record.debug.passes[m_passIndex].label,
                           m_frame->record.debug.resources[handle.index].name));
    return result;
}

//======================================================================================================================
rojoRHI::ViewIndex PassResources::storageView(GraphTexture handle, uint32_t mip) const {
    const auto& resource = textureResource(handle);
    LMX_ASSERT(mip < resource.storageViews.size() && resource.storageViews[mip] != engine::kNoView,
               std::format("pass '{}' resolves unavailable storage mip {} of texture '{}'",
                           m_frame->record.debug.passes[m_passIndex].label, mip,
                           m_frame->record.debug.resources[handle.index].name));
    return resource.storageViews[mip];
}

//======================================================================================================================
rojoRHI::Buffer PassResources::buffer(GraphBuffer handle) const {
    return bufferResource(handle).buffer.buffer;
}

//======================================================================================================================
rojoRHI::Addr PassResources::address(GraphBuffer handle, uint64_t offset) const {
    const auto& resource = bufferResource(handle).buffer;
    LMX_ASSERT(resource.address && offset < resource.size,
               std::format("pass '{}' resolves unavailable address at offset {} of buffer '{}'",
                           m_frame->record.debug.passes[m_passIndex].label, offset,
                           m_frame->record.debug.resources[handle.index].name));
    return *resource.address + offset;
}

//======================================================================================================================
GraphTexture RenderGraph::importTexture(const engine::TextureRef& texture, std::string_view name,
                                        std::span<const rojoRHI::ViewIndex> storageViews) {
    LMX_ASSERT(texture.texture.bits != 0 && texture.extent.width > 0 && texture.extent.height > 0 &&
                   texture.mips > 0 && texture.layers > 0,
               "texture import requires valid identity and shape");
    LMX_ASSERT(storageViews.empty() || storageViews.size() == texture.mips,
               "texture import storage views must cover every mip or be absent");
    m_resources.push_back({.kind = ResourceKind::Texture,
                           .name = std::string(name),
                           .texture = texture,
                           .format = texture.format,
                           .width = texture.extent.width,
                           .height = texture.extent.height,
                           .mipLevels = texture.mips,
                           .arrayLayers = texture.layers,
                           .storageViews = {storageViews.begin(), storageViews.end()}});
    return {static_cast<uint32_t>(m_resources.size() - 1), 0};
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
GraphTexture
RenderGraph::importPersistentTexture(const engine::TextureRef& texture, std::string_view name,
                                     std::span<const rojoRHI::ViewIndex> storageViews) {
    LMX_ASSERT(
        m_textureStates != nullptr,
        std::format(
            "persistent texture '{}' is imported on a graph with no texture state table bound",
            name));
    LMX_ASSERT(std::ranges::none_of(m_resources,
                                    [&](const Resource& resource) {
                                        return resource.tracked &&
                                               resource.texture.texture == texture.texture;
                                    }),
               std::format("persistent texture '{}' is already imported in this graph", name));
    const auto handle = importTexture(texture, name, storageViews);
    auto& resource = m_resources[handle.index];
    resource.tracked = true;
    resource.priorTextureAccess = m_textureStates->logicalUse(texture.texture);
    resource.priorPhysical = m_textureStates->physicalRanges(texture.texture, {});
    return handle;
}

//======================================================================================================================
GraphTexture RenderGraph::importPersistentTexture(const engine::GpuTexture& texture,
                                                  std::string_view name) {
    std::vector<rojoRHI::ViewIndex> views;
    for (uint32_t mip = 0; mip < texture.storageViewCount(); ++mip) {
        views.push_back(texture.storageView(mip));
    }
    return importPersistentTexture(texture.ref(), name, views);
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
GraphBuffer RenderGraph::importBuffer(const BufferRef& buffer, std::string_view name) {
    LMX_ASSERT(buffer.buffer.bits != 0 && buffer.size > 0,
               "buffer import requires valid identity and bounds");
    m_resources.push_back(
        {.kind = ResourceKind::Buffer, .name = std::string(name), .buffer = buffer});
    return {static_cast<uint32_t>(m_resources.size() - 1), 0};
}

//======================================================================================================================
GraphBuffer RenderGraph::importBuffer(const BufferRef& buffer, std::string_view name,
                                      BufferUse previousUse) {
    const auto handle = importBuffer(buffer, name);
    m_resources[handle.index].priorBufferAccess = previousUse;
    return handle;
}

//======================================================================================================================
GraphBuffer RenderGraph::importBuffer(const engine::GpuBuffer& buffer, std::string_view name) {
    BufferRef ref{buffer.rhi(), buffer.desc().size, std::nullopt};
    if (buffer.desc().memory != rojoRHI::Memory::Readback) {
        ref.address = buffer.address();
    }
    return importBuffer(ref, name);
}

//======================================================================================================================
GraphBuffer RenderGraph::importBuffer(const engine::GpuBuffer& buffer, std::string_view name,
                                      BufferUse previousUse) {
    const auto handle = importBuffer(buffer, name);
    m_resources[handle.index].priorBufferAccess = previousUse;
    return handle;
}

//======================================================================================================================
GraphTexture RenderGraph::createTexture(const TransientTextureDesc& desc, std::string_view name) {
    LMX_ASSERT(desc.kind == rojoRHI::TextureType::Tex2D || desc.kind == rojoRHI::TextureType::Cube,
               "transient textures must be 2D textures or cubes");
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
         .arrayLayers = desc.kind == rojoRHI::TextureType::Cube ? kCubeFaceCount : 1,
         .transient = true,
         .textureDesc = desc,
         .textureFootprint = m_transients->footprint(desc)});
    return {.index = static_cast<uint32_t>(m_resources.size() - 1), .version = 0};
}

//======================================================================================================================
GraphBuffer RenderGraph::createBuffer(const TransientBufferDesc& desc, std::string_view name) {
    LMX_ASSERT(m_transients != nullptr,
               std::format("transient buffer '{}' is declared on a graph with no TransientPool: "
                           "a graph that creates resources needs somewhere to place them",
                           name));
    m_resources.push_back(
        {.kind = ResourceKind::Buffer,
         .name = std::string(name),
         .transient = true,
         .bufferDesc = desc,
         .bufferFootprints = Resource::BufferFootprints{m_transients->footprint(desc, false),
                                                        m_transients->footprint(desc, true)}});
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
GraphResult<LoweredFrame> RenderGraph::lower(uint64_t frameId) const {
    auto record = compileFrame(frameId);
    if (!record) {
        return std::unexpected(std::move(record.error()));
    }
    LoweredFrame frame{.record = std::move(*record)};
    for (const Resource& resource : m_resources) {
        frame.resources.push_back(
            resource.transient
                ? LoweredResource{}
                : LoweredResource{resource.texture, resource.buffer, resource.storageViews});
    }
    const auto plan = planTransients(frame.record.debug.schedule);
    frame.transientLayout.bytes = plan.memory.highWater;
    for (const auto& entry : plan.transients) {
        if (!entry.used) {
            continue;
        }
        const auto& resource = m_resources[entry.resource];
        TransientPlacementDesc placement{.resource = entry.resource,
                                         .offset = entry.offset,
                                         .footprint = {entry.size, entry.alignment},
                                         .label = resource.name};
        if (resource.kind == ResourceKind::Texture) {
            placement.desc = resource.textureDesc;
        } else {
            placement.desc = TransientBufferPlacementDesc{resource.bufferDesc, entry.indirectArgs};
        }
        frame.transientLayout.placements.push_back(std::move(placement));
    }
    auto physical = lowerPhysical(frame, plan);
    if (!physical) {
        return std::unexpected(std::move(physical.error()));
    }
    frame.finalStates = finalStatesOf(frame.record.debug.schedule);
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
    LMX_ASSERT(!m_executed, "a render graph executes only once");
    auto frame = lower(frameId);
    LMX_ASSERT(frame.has_value(), frame.error().message);
    placeTransients(*frame);
    dumpCompiledFrameIfRequested(frame->record);
    emit(*frame, commands, [this, &frame](uint32_t passIndex) {
        m_passes[passIndex].execute(passResources(*frame, passIndex));
    });
    for (const auto& state : frame->finalStates) {
        const auto texture = frame->resources[state.resource].texture;
        m_textureStates->setLogicalUse(texture.texture, state.use);
        m_textureStates->seedPhysical(texture.texture, texture.mips, texture.layers,
                                      texture.initialLayout);
    }
    for (const auto& state : frame->physicalStates) {
        m_textureStates->set(frame->resources[state.resource].texture.texture, state.state.range,
                             state.state.state);
    }
    m_executed = true;
    return std::move(frame->record);
}

//======================================================================================================================
void RenderGraph::placeTransients(LoweredFrame& frame) {
    if (m_transients == nullptr) {
        return;
    }
    const auto reserved = m_transients->reserve(frame.transientLayout);
    LMX_ASSERT(reserved.has_value(), reserved.error().message);
    for (const auto& entry : frame.transientLayout.placements) {
        auto& resolved = frame.resources[entry.resource];
        if (const auto* desc = std::get_if<TransientTextureDesc>(&entry.desc)) {
            auto texture = m_transients->placeTexture(*desc, entry.offset, entry.label);
            LMX_ASSERT(texture.has_value(), texture.error().message);
            resolved.texture = *texture;
            if (desc->storageRead || desc->storageWrite) {
                for (uint32_t mip = 0; mip < desc->mipLevels; ++mip) {
                    resolved.storageViews.push_back(
                        m_transients->storageView(texture->texture, mip));
                }
            }
        } else {
            const auto& bufferDesc = std::get<TransientBufferPlacementDesc>(entry.desc);
            auto buffer = m_transients->placeBuffer(bufferDesc.desc, entry.offset, entry.label,
                                                    bufferDesc.indirectArgs);
            LMX_ASSERT(buffer.has_value(), buffer.error().message);
            resolved.buffer = {*buffer, bufferDesc.desc.size, m_transients->address(*buffer)};
        }
    }
    m_transients->finishPlacement();
}

//======================================================================================================================
PassResources RenderGraph::passResources(const LoweredFrame& frame, uint32_t passIndex) const {
    LMX_ASSERT(passIndex < frame.record.debug.passes.size(), "pass index names no declared pass");
    return PassResources(frame, passIndex);
}

} // namespace lmx::render
