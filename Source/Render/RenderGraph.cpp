//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraph.cpp
/// @brief Implements render-graph validation, scheduling, and execution.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/RenderGraph.h"

#include "Core/Assert.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lmx::render {
namespace {

//======================================================================================================================
// Enumerator names verbatim, so a message says D32Float rather than an integer. Omitting default
// lets -Wswitch catch a newly added format.
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
    case rhi::Format::RG16Float:
        return "RG16Float";
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
bool isDepthFormat(rhi::Format format) {
    return format == rhi::Format::D32Float;
}

//======================================================================================================================
// The colour attachment role, on the same terms the RHI's own desc validation uses: 8-bit unorm
// targets, sRGB included, plus the half-float scene-colour format. A block-compressed format
// cannot be rendered into, RG16Float carries lookup data and is sampled rather than rendered
// into, and a depth format belongs in the other slot.
bool isColorRenderableFormat(rhi::Format format) {
    switch (format) {
    case rhi::Format::BGRA8Unorm:
    case rhi::Format::RGBA8Unorm:
    case rhi::Format::RGBA8Unorm_sRGB:
    case rhi::Format::RGBA16Float:
        return true;
    case rhi::Format::Unknown:
    case rhi::Format::RG16Float:
    case rhi::Format::BC1Unorm:
    case rhi::Format::BC1Unorm_sRGB:
    case rhi::Format::D32Float:
        return false;
    }
    return false;
}

//======================================================================================================================
bool isWriteRole(UseRole role) {
    switch (role) {
    case UseRole::Read:
    case UseRole::CopySource:
        return false;
    case UseRole::Write:
    case UseRole::ColorAttachment:
    case UseRole::DepthAttachment:
    case UseRole::CopyDestination:
        return true;
    }
    return false;
}

//======================================================================================================================
// The RHI use a declaration stands for on either side of a derived barrier. The role decides it
// almost alone; only a plain read has to ask the pass kind, because a compute pass reads through a
// storage binding where a raster pass reads through a sampled one.
rhi::TextureUse textureUseOf(PassKind kind, UseRole role) {
    switch (role) {
    case UseRole::Read:
        return kind == PassKind::Compute ? rhi::TextureUse::StorageRead
                                         : rhi::TextureUse::ShaderRead;
    case UseRole::Write:
        return rhi::TextureUse::StorageWrite;
    case UseRole::ColorAttachment:
    case UseRole::DepthAttachment:
        return rhi::TextureUse::RenderTarget;
    case UseRole::CopySource:
        return rhi::TextureUse::CopySource;
    case UseRole::CopyDestination:
        return rhi::TextureUse::CopyDestination;
    }
    return rhi::TextureUse::ShaderRead;
}

//======================================================================================================================
rhi::BufferUse bufferUseOf(PassKind kind, UseRole role) {
    switch (role) {
    case UseRole::Read:
        return kind == PassKind::Compute ? rhi::BufferUse::StorageRead : rhi::BufferUse::ShaderRead;
    case UseRole::Write:
    case UseRole::ColorAttachment:
    case UseRole::DepthAttachment:
        return rhi::BufferUse::StorageWrite;
    case UseRole::CopySource:
        return rhi::BufferUse::CopySource;
    case UseRole::CopyDestination:
        return rhi::BufferUse::CopyDestination;
    }
    return rhi::BufferUse::ShaderRead;
}

// A subresource range with both sentinels resolved against the texture, as inclusive bounds. This
// is the only form the overlap and containment rules are stated in, so no rule has to reason about
// kAllMipLevels twice.
struct ResolvedRange {
    uint32_t firstMip = 0;
    uint32_t lastMip = 0;
    uint32_t firstLayer = 0;
    uint32_t lastLayer = 0;
};

//======================================================================================================================
// Resolution saturates rather than wrapping: a count past the end of the chain is caught by the
// containment check below, and this must produce a comparable bound for that message to name.
ResolvedRange resolveRange(const rhi::TextureSubresourceRange& range, const rhi::Texture& texture) {
    const auto lastOf = [](uint32_t base, uint32_t count, uint32_t available) {
        if (count == rhi::kAllMipLevels) {
            return available > base ? available - 1 : base;
        }
        return count == 0 ? base : base + count - 1;
    };
    return {.firstMip = range.baseMipLevel,
            .lastMip = lastOf(range.baseMipLevel, range.mipLevelCount, texture.mipLevels()),
            .firstLayer = range.baseArrayLayer,
            .lastLayer =
                lastOf(range.baseArrayLayer, range.arrayLayerCount, texture.arrayLayers())};
}

//======================================================================================================================
// A range covers real subresources only when both axes do, so an empty count on either one is an
// empty range whatever the other says.
bool isEmptyRange(const rhi::TextureSubresourceRange& range) {
    return range.mipLevelCount == 0 || range.arrayLayerCount == 0;
}

//======================================================================================================================
bool containsRange(const ResolvedRange& resolved, const rhi::Texture& texture) {
    return resolved.lastMip < texture.mipLevels() && resolved.lastLayer < texture.arrayLayers();
}

//======================================================================================================================
// Two ranges overlap only when they share a subresource, which takes both axes intersecting: mip 1
// of layer 0 and mip 1 of layer 1 are different subresources.
bool rangesOverlap(const ResolvedRange& a, const ResolvedRange& b) {
    const bool mips = a.firstMip <= b.lastMip && b.firstMip <= a.lastMip;
    const bool layers = a.firstLayer <= b.lastLayer && b.firstLayer <= a.lastLayer;
    return mips && layers;
}

//======================================================================================================================
// The canonical range covering both, expressed the way a whole-resource declaration is: a union
// that reaches the end of the chain keeps the sentinel rather than a resolved count, so a barrier
// derived from whole-resource reads is indistinguishable from the declaration it came from.
rhi::TextureSubresourceRange unionRange(const ResolvedRange& a, const ResolvedRange& b,
                                        const rhi::Texture& texture) {
    const uint32_t firstMip = std::min(a.firstMip, b.firstMip);
    const uint32_t lastMip = std::max(a.lastMip, b.lastMip);
    const uint32_t firstLayer = std::min(a.firstLayer, b.firstLayer);
    const uint32_t lastLayer = std::max(a.lastLayer, b.lastLayer);
    return {.baseMipLevel = firstMip,
            .mipLevelCount =
                lastMip + 1 >= texture.mipLevels() ? rhi::kAllMipLevels : lastMip - firstMip + 1,
            .baseArrayLayer = firstLayer,
            .arrayLayerCount = lastLayer + 1 >= texture.arrayLayers() ? rhi::kAllArrayLayers
                                                                      : lastLayer - firstLayer + 1};
}

//======================================================================================================================
// A (resource, version) pair as one hashable key. Both halves are 32-bit, so the pair is lossless.
uint64_t versionKey(uint32_t resource, uint32_t version) {
    return (static_cast<uint64_t>(resource) << 32) | version;
}

//======================================================================================================================
std::unexpected<GraphError> fail(std::string message) {
    return std::unexpected(GraphError{.message = std::move(message)});
}

} // namespace

//======================================================================================================================
std::string_view roleName(UseRole role) {
    switch (role) {
    case UseRole::Read:
        return "read";
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
std::string describeRange(const rhi::TextureSubresourceRange& range) {
    const auto axis = [](std::string_view name, uint32_t base, uint32_t count, uint32_t sentinel) {
        if (count == sentinel) {
            return std::format("{}[{}..]", name, base);
        }
        return std::format("{}[{}..{}]", name, base, count == 0 ? base : base + count - 1);
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
    return resource.buffer;
}

//======================================================================================================================
GraphTexture RenderGraph::importTexture(rhi::Texture& texture, rhi::Format format,
                                        std::string_view name) {
    m_resources.push_back({.kind = ResourceKind::Texture,
                           .name = std::string(name),
                           .texture = &texture,
                           .format = format});
    return {.index = static_cast<uint32_t>(m_resources.size() - 1), .version = 0};
}

//======================================================================================================================
GraphBuffer RenderGraph::importBuffer(rhi::Buffer& buffer, std::string_view name) {
    m_resources.push_back(
        {.kind = ResourceKind::Buffer, .name = std::string(name), .buffer = &buffer});
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
    if (desc.color) {
        checkTexture(desc.color->handle);
        declarations.push_back({.resource = desc.color->handle.index,
                                .version = desc.color->handle.version,
                                .role = UseRole::ColorAttachment,
                                .range = {},
                                .isWrite = true});
    }
    if (desc.depth) {
        checkTexture(desc.depth->handle);
        declarations.push_back({.resource = desc.depth->handle.index,
                                .version = desc.depth->handle.version,
                                .role = UseRole::DepthAttachment,
                                .range = {},
                                .isWrite = true});
    }
    flattenTextures(declarations, desc.textureWrites, UseRole::Write);
    flattenBuffers(declarations, desc.bufferWrites, UseRole::Write);

    m_passes.push_back({.label = std::string(label),
                        .kind = PassKind::Raster,
                        .color = desc.color,
                        .depth = desc.depth,
                        .execute = std::move(execute),
                        .declarations = std::move(declarations)});
}

//======================================================================================================================
void RenderGraph::addComputePass(std::string_view label, ComputePassDesc desc, ExecuteFn execute) {
    LMX_ASSERT(static_cast<bool>(execute), "a declared pass must carry a body");

    std::vector<Declaration> declarations;
    flattenTextures(declarations, desc.textureReads, UseRole::Read);
    flattenBuffers(declarations, desc.bufferReads, UseRole::Read);
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
void RenderGraph::exportTexture(GraphTexture handle) {
    checkTexture(handle);
    m_exports.push_back(handle);
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
GraphResult<Schedule> RenderGraph::compile() const {
    GraphResult<CompiledFrameRecord> record = compileFrame(0);
    if (!record) {
        return std::unexpected(record.error());
    }
    return std::move(record->debug.schedule);
}

//======================================================================================================================
GraphResult<CompiledFrameRecord> RenderGraph::compileFrame(uint64_t frameId) const {
    // Attachment roles and extents. These depend on one pass alone, so they are answered before any
    // cross-pass structure is built and cannot be masked by an ordering failure.
    for (const Pass& pass : m_passes) {
        if (pass.color) {
            const Resource& color = m_resources[pass.color->handle.index];
            if (!isColorRenderableFormat(color.format)) {
                return fail(std::format("pass '{}' color attachment '{}' declares format {}, which "
                                        "is not color-renderable",
                                        pass.label, color.name, formatName(color.format)));
            }
        }
        if (pass.depth) {
            const Resource& depth = m_resources[pass.depth->handle.index];
            if (!isDepthFormat(depth.format)) {
                return fail(std::format("pass '{}' depth attachment '{}' declares format {}, which "
                                        "is not a depth format",
                                        pass.label, depth.name, formatName(depth.format)));
            }
        }
        if (pass.color && pass.depth) {
            const Resource& color = m_resources[pass.color->handle.index];
            const Resource& depth = m_resources[pass.depth->handle.index];
            if (color.texture->width() != depth.texture->width() ||
                color.texture->height() != depth.texture->height()) {
                return fail(std::format("pass '{}' attachment extent mismatch: color '{}' is {}x{} "
                                        "and depth '{}' is {}x{}",
                                        pass.label, color.name, color.texture->width(),
                                        color.texture->height(), depth.name, depth.texture->width(),
                                        depth.texture->height()));
            }
        }
    }

    // Subresource rules (spec §6). Both depend on one pass alone as well: a range that names no
    // subresource of its texture is a broken declaration whatever the schedule, and a pass reading
    // and writing one subresource has an intra-pass hazard no ordering between passes can fix.
    for (const Pass& pass : m_passes) {
        for (const Declaration& declaration : pass.declarations) {
            const Resource& resource = m_resources[declaration.resource];
            if (resource.kind != ResourceKind::Texture) {
                continue;
            }
            if (isEmptyRange(declaration.range)) {
                return fail(std::format("pass '{}' declares a {} of texture '{}' over {}, which "
                                        "covers no subresource",
                                        pass.label, roleName(declaration.role), resource.name,
                                        describeRange(declaration.range)));
            }
            const ResolvedRange resolved = resolveRange(declaration.range, *resource.texture);
            if (!containsRange(resolved, *resource.texture)) {
                return fail(std::format("pass '{}' declares a {} of texture '{}' over {}, which "
                                        "runs past its {} mip levels and {} array layers",
                                        pass.label, roleName(declaration.role), resource.name,
                                        describeRange(declaration.range),
                                        resource.texture->mipLevels(),
                                        resource.texture->arrayLayers()));
            }
        }

        for (const Declaration& read : pass.declarations) {
            if (read.isWrite || m_resources[read.resource].kind != ResourceKind::Texture) {
                continue;
            }
            for (const Declaration& write : pass.declarations) {
                if (!write.isWrite || write.resource != read.resource) {
                    continue;
                }
                const Resource& resource = m_resources[read.resource];
                if (!rangesOverlap(resolveRange(read.range, *resource.texture),
                                   resolveRange(write.range, *resource.texture))) {
                    continue;
                }
                return fail(std::format(
                    "pass '{}' declares a {} of texture '{}' over {} and a {} of it over {}, which "
                    "overlap: one pass may read and write a texture only through disjoint ranges",
                    pass.label, roleName(read.role), resource.name, describeRange(read.range),
                    roleName(write.role), describeRange(write.range)));
            }
        }
    }

    // Who writes which version, and therefore who produces the version after it. One writer per
    // version is what makes "version v + 1" name a single set of contents -- including when the two
    // writers name disjoint subresources, because the second one still has to say which contents it
    // starts from, and only the version says that.
    std::unordered_map<uint64_t, uint32_t> writerOfVersion;
    std::unordered_map<uint64_t, uint32_t> producerOfVersion;
    for (uint32_t pass = 0; pass < m_passes.size(); ++pass) {
        for (const Declaration& declaration : m_passes[pass].declarations) {
            if (!declaration.isWrite) {
                continue;
            }
            const Resource& resource = m_resources[declaration.resource];
            const auto existing =
                writerOfVersion.find(versionKey(declaration.resource, declaration.version));
            if (existing != writerOfVersion.end()) {
                return fail(
                    std::format("passes '{}' and '{}' both write {} '{}' version {}",
                                m_passes[existing->second].label, m_passes[pass].label,
                                resource.kind == ResourceKind::Texture ? "texture" : "buffer",
                                resource.name, declaration.version));
            }
            writerOfVersion.emplace(versionKey(declaration.resource, declaration.version), pass);
            producerOfVersion.emplace(versionKey(declaration.resource, declaration.version + 1),
                                      pass);
        }
    }

    // Every version a pass names must exist, and naming it is the dependency edge. Version 0 is the
    // imported contents and needs no producer; a write is an edge too, because taking a resource to
    // its next version has to happen after whatever put it in the state being taken.
    std::vector<std::vector<uint32_t>> consumers(m_passes.size());
    std::vector<uint32_t> pendingDependencies(m_passes.size(), 0);
    for (uint32_t pass = 0; pass < m_passes.size(); ++pass) {
        for (const Declaration& declaration : m_passes[pass].declarations) {
            if (declaration.version == 0) {
                continue;
            }
            const Resource& resource = m_resources[declaration.resource];
            const auto producer =
                producerOfVersion.find(versionKey(declaration.resource, declaration.version));
            if (producer == producerOfVersion.end()) {
                return fail(
                    std::format("pass '{}' declares a {} of {} '{}' version {}, which no "
                                "pass writes",
                                m_passes[pass].label, roleName(declaration.role),
                                resource.kind == ResourceKind::Texture ? "texture" : "buffer",
                                resource.name, declaration.version));
            }
            consumers[producer->second].push_back(pass);
            ++pendingDependencies[pass];
        }
    }

    for (const GraphTexture& exported : m_exports) {
        const Resource& resource = m_resources[exported.index];
        if (!producerOfVersion.contains(versionKey(exported.index, exported.version))) {
            return fail(std::format("exported texture '{}' version {} is not written by any pass",
                                    resource.name, exported.version));
        }
    }

    // Kahn's algorithm, taking the lowest-numbered ready pass each round: that is the declaration
    // order tie-break, and it makes the schedule a function of the declarations alone.
    Schedule schedule;
    schedule.passes.reserve(m_passes.size());
    std::vector<bool> scheduled(m_passes.size(), false);
    while (schedule.passes.size() < m_passes.size()) {
        uint32_t ready = static_cast<uint32_t>(m_passes.size());
        for (uint32_t pass = 0; pass < m_passes.size(); ++pass) {
            if (!scheduled[pass] && pendingDependencies[pass] == 0) {
                ready = pass;
                break;
            }
        }
        if (ready == m_passes.size()) {
            std::string involved;
            for (uint32_t pass = 0; pass < m_passes.size(); ++pass) {
                if (scheduled[pass]) {
                    continue;
                }
                involved += involved.empty() ? "" : ", ";
                involved += std::format("'{}'", m_passes[pass].label);
            }
            return fail(std::format("render graph contains a cycle involving passes {}", involved));
        }
        scheduled[ready] = true;
        schedule.passes.push_back(ready);
        for (uint32_t consumer : consumers[ready]) {
            --pendingDependencies[consumer];
        }
    }

    CompiledFrameRecord record{.frameId = frameId, .debug = {}};
    record.debug.resources.reserve(m_resources.size());
    for (const Resource& resource : m_resources) {
        record.debug.resources.push_back({.name = resource.name,
                                          .kind = resource.kind == ResourceKind::Texture
                                                      ? GraphResourceKind::Texture
                                                      : GraphResourceKind::Buffer,
                                          .format = resource.format});
    }
    record.debug.passes.reserve(m_passes.size());
    for (const Pass& pass : m_passes) {
        DebugPass entry{.label = pass.label, .kind = pass.kind, .uses = {}};
        entry.uses.reserve(pass.declarations.size());
        for (const Declaration& declaration : pass.declarations) {
            entry.uses.push_back({.resource = declaration.resource,
                                  .version = declaration.version,
                                  .role = declaration.role,
                                  .range = declaration.range});
        }
        record.debug.passes.push_back(std::move(entry));
    }
    record.debug.transitions = deriveTransitions(schedule);
    record.debug.schedule = std::move(schedule);
    return record;
}

//======================================================================================================================
std::vector<DebugTransition> RenderGraph::deriveTransitions(const Schedule& schedule) const {
    // What each resource was last written as, until a barrier makes that write visible. One
    // transition serves every later reader; writing a resource again puts it back in a producing
    // state and so needs the transition again.
    struct PendingWrite {
        bool active = false;
        rhi::TextureUse textureUse = rhi::TextureUse::RenderTarget;
        rhi::BufferUse bufferUse = rhi::BufferUse::StorageWrite;
    };
    std::vector<PendingWrite> pending(m_resources.size());
    std::vector<DebugTransition> transitions;

    for (const uint32_t passIndex : schedule.passes) {
        const Pass& pass = m_passes[passIndex];

        // One barrier per resource the pass reads, from the use that last wrote it. Several reads
        // of one resource collapse into the range that covers them all, because the transition
        // belongs to the resource rather than to any single binding.
        for (uint32_t index = 0; index < pass.declarations.size(); ++index) {
            const Declaration& read = pass.declarations[index];
            if (read.isWrite || !pending[read.resource].active) {
                continue;
            }
            bool alreadyCovered = false;
            for (uint32_t earlier = 0; earlier < index; ++earlier) {
                alreadyCovered =
                    alreadyCovered || (!pass.declarations[earlier].isWrite &&
                                       pass.declarations[earlier].resource == read.resource);
            }
            if (alreadyCovered) {
                continue;
            }

            const Resource& resource = m_resources[read.resource];
            DebugTransition transition{.beforePass = passIndex, .resource = read.resource};
            if (resource.kind == ResourceKind::Buffer) {
                transition.kind = GraphResourceKind::Buffer;
                transition.bufferFrom = pending[read.resource].bufferUse;
                transition.bufferTo = bufferUseOf(pass.kind, read.role);
            } else {
                rhi::TextureSubresourceRange covered = read.range;
                for (uint32_t later = index + 1; later < pass.declarations.size(); ++later) {
                    const Declaration& other = pass.declarations[later];
                    if (other.isWrite || other.resource != read.resource) {
                        continue;
                    }
                    covered =
                        unionRange(resolveRange(covered, *resource.texture),
                                   resolveRange(other.range, *resource.texture), *resource.texture);
                }
                transition.kind = GraphResourceKind::Texture;
                transition.range = covered;
                transition.textureFrom = pending[read.resource].textureUse;
                transition.textureTo = textureUseOf(pass.kind, read.role);
            }
            transitions.push_back(transition);
            pending[read.resource].active = false;
        }

        for (const Declaration& declaration : pass.declarations) {
            if (!declaration.isWrite) {
                continue;
            }
            pending[declaration.resource] = {.active = true,
                                             .textureUse =
                                                 textureUseOf(pass.kind, declaration.role),
                                             .bufferUse = bufferUseOf(pass.kind, declaration.role)};
        }
    }
    return transitions;
}

//======================================================================================================================
CompiledFrameRecord RenderGraph::execute(rhi::CommandList& commands, uint64_t frameId) {
    GraphResult<CompiledFrameRecord> record = compileFrame(frameId);
    LMX_ASSERT(record.has_value(), record.error().message);

    // Transitions are recorded in schedule order and a pass's own are contiguous, so one cursor
    // emits each exactly where compilation placed it.
    size_t nextTransition = 0;
    for (const uint32_t passIndex : record->debug.schedule.passes) {
        const Pass& pass = m_passes[passIndex];

        while (nextTransition < record->debug.transitions.size() &&
               record->debug.transitions[nextTransition].beforePass == passIndex) {
            const DebugTransition& transition = record->debug.transitions[nextTransition];
            const Resource& resource = m_resources[transition.resource];
            if (transition.kind == GraphResourceKind::Buffer) {
                commands.bufferBarrier(*resource.buffer, rhi::BufferRange{}, transition.bufferFrom,
                                       transition.bufferTo);
            } else {
                commands.textureBarrier(*resource.texture, transition.range, transition.textureFrom,
                                        transition.textureTo);
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
                const ColorAttachment& color = *pass.color;
                LMX_ASSERT(color.store == StoreOp::Store,
                           std::format("pass '{}' discards its colour attachment, which this RHI "
                                       "cannot express -- a colour attachment is always stored",
                                       pass.label));
                desc.colorTarget = m_resources[color.handle.index].texture;
                desc.clear = color.load == LoadOp::Clear;
                for (size_t channel = 0; channel < 4; ++channel) {
                    desc.clearColor[channel] = color.clearColor[channel];
                }
            }
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
PassResources RenderGraph::passResources(uint32_t passIndex) const {
    LMX_ASSERT(passIndex < m_passes.size(), "pass index names no declared pass");
    return PassResources(*this, passIndex);
}

} // namespace lmx::render
