#include "Render/RenderGraph.h"

#include "Core/Assert.h"

#include <format>
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

// One resource version, flattened out of whichever PassDesc field named it. Validation, the
// dependency edges, and PassResources all read this single flattening, so no rule can quietly
// overlook one of the declaration forms.
struct Declaration {
    uint32_t resource = 0;
    uint32_t version = 0;
    std::string_view role;
    bool isWrite = false;
};

//======================================================================================================================
std::vector<Declaration> declarationsOf(const PassDesc& desc) {
    std::vector<Declaration> declarations;
    declarations.reserve(desc.textureReads.size() + desc.bufferReads.size() +
                         desc.textureWrites.size() + desc.bufferWrites.size() + 2);
    for (const GraphTexture& handle : desc.textureReads) {
        declarations.push_back({handle.index, handle.version, "read", false});
    }
    for (const GraphBuffer& handle : desc.bufferReads) {
        declarations.push_back({handle.index, handle.version, "read", false});
    }
    if (desc.color) {
        declarations.push_back(
            {desc.color->handle.index, desc.color->handle.version, "color attachment", true});
    }
    if (desc.depth) {
        declarations.push_back(
            {desc.depth->handle.index, desc.depth->handle.version, "depth attachment", true});
    }
    for (const GraphTexture& handle : desc.textureWrites) {
        declarations.push_back({handle.index, handle.version, "write", true});
    }
    for (const GraphBuffer& handle : desc.bufferWrites) {
        declarations.push_back({handle.index, handle.version, "write", true});
    }
    return declarations;
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
void RenderGraph::addPass(std::string_view label, PassDesc desc, ExecuteFn execute) {
    LMX_ASSERT(static_cast<bool>(execute), "a declared pass must carry a body");

    const auto checkTexture = [this](GraphTexture handle) {
        LMX_ASSERT(handle.index < m_resources.size(), "GraphTexture names no resource");
        LMX_ASSERT(m_resources[handle.index].kind == ResourceKind::Texture,
                   "GraphTexture names an imported buffer");
    };
    const auto checkBuffer = [this](GraphBuffer handle) {
        LMX_ASSERT(handle.index < m_resources.size(), "GraphBuffer names no resource");
        LMX_ASSERT(m_resources[handle.index].kind == ResourceKind::Buffer,
                   "GraphBuffer names an imported texture");
    };
    for (const GraphTexture& handle : desc.textureReads) {
        checkTexture(handle);
    }
    for (const GraphTexture& handle : desc.textureWrites) {
        checkTexture(handle);
    }
    for (const GraphBuffer& handle : desc.bufferReads) {
        checkBuffer(handle);
    }
    for (const GraphBuffer& handle : desc.bufferWrites) {
        checkBuffer(handle);
    }
    if (desc.color) {
        checkTexture(desc.color->handle);
    }
    if (desc.depth) {
        checkTexture(desc.depth->handle);
    }

    m_passes.push_back(
        {.label = std::string(label), .desc = std::move(desc), .execute = std::move(execute)});
}

//======================================================================================================================
void RenderGraph::exportTexture(GraphTexture handle) {
    LMX_ASSERT(handle.index < m_resources.size(), "GraphTexture names no resource");
    LMX_ASSERT(m_resources[handle.index].kind == ResourceKind::Texture,
               "GraphTexture names an imported buffer");
    m_exports.push_back(handle);
}

//======================================================================================================================
bool RenderGraph::passDeclares(uint32_t passIndex, uint32_t resourceIndex, uint32_t version) const {
    LMX_ASSERT(passIndex < m_passes.size(), "pass index names no declared pass");
    for (const Declaration& declaration : declarationsOf(m_passes[passIndex].desc)) {
        if (declaration.resource == resourceIndex && declaration.version == version) {
            return true;
        }
    }
    return false;
}

//======================================================================================================================
GraphResult<Schedule> RenderGraph::compile() const {
    // Attachment roles and extents. These depend on one pass alone, so they are answered before any
    // cross-pass structure is built and cannot be masked by an ordering failure.
    for (const Pass& pass : m_passes) {
        if (pass.desc.color) {
            const Resource& color = m_resources[pass.desc.color->handle.index];
            if (!isColorRenderableFormat(color.format)) {
                return fail(std::format("pass '{}' color attachment '{}' declares format {}, which "
                                        "is not color-renderable",
                                        pass.label, color.name, formatName(color.format)));
            }
        }
        if (pass.desc.depth) {
            const Resource& depth = m_resources[pass.desc.depth->handle.index];
            if (!isDepthFormat(depth.format)) {
                return fail(std::format("pass '{}' depth attachment '{}' declares format {}, which "
                                        "is not a depth format",
                                        pass.label, depth.name, formatName(depth.format)));
            }
        }
        if (pass.desc.color && pass.desc.depth) {
            const Resource& color = m_resources[pass.desc.color->handle.index];
            const Resource& depth = m_resources[pass.desc.depth->handle.index];
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

    std::vector<std::vector<Declaration>> declarations(m_passes.size());
    for (uint32_t pass = 0; pass < m_passes.size(); ++pass) {
        declarations[pass] = declarationsOf(m_passes[pass].desc);
    }

    // Who writes which version, and therefore who produces the version after it. One writer per
    // version is what makes "version v + 1" name a single set of contents.
    std::unordered_map<uint64_t, uint32_t> writerOfVersion;
    std::unordered_map<uint64_t, uint32_t> producerOfVersion;
    for (uint32_t pass = 0; pass < m_passes.size(); ++pass) {
        for (const Declaration& declaration : declarations[pass]) {
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
        for (const Declaration& declaration : declarations[pass]) {
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
                                m_passes[pass].label, declaration.role,
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
    return schedule;
}

//======================================================================================================================
void RenderGraph::execute(rhi::CommandList& commands) {
    const GraphResult<Schedule> schedule = compile();
    LMX_ASSERT(schedule.has_value(), schedule.error().message);

    // Which resources hold contents a pass rendered, and which of those a barrier has already made
    // readable. One transition serves every later reader; rendering into a resource again puts it
    // back in the attachment state and so needs the transition again.
    std::vector<bool> rendered(m_resources.size(), false);
    std::vector<bool> readable(m_resources.size(), false);

    for (const uint32_t passIndex : schedule->passes) {
        const Pass& pass = m_passes[passIndex];
        LMX_ASSERT(pass.desc.color.has_value() || pass.desc.depth.has_value(),
                   std::format("pass '{}' declares no attachment: this graph encodes render "
                               "passes, so a pass with nothing to render into cannot be run",
                               pass.label));

        for (const GraphTexture& read : pass.desc.textureReads) {
            if (!rendered[read.index] || readable[read.index]) {
                continue;
            }
            readable[read.index] = true;
            commands.textureBarrier(*m_resources[read.index].texture, rhi::TextureUse::RenderTarget,
                                    rhi::TextureUse::ShaderRead);
        }

        rhi::RenderPassDesc desc;
        desc.label = pass.label;
        if (pass.desc.color) {
            const ColorAttachment& color = *pass.desc.color;
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
        if (pass.desc.depth) {
            const DepthAttachment& depth = *pass.desc.depth;
            LMX_ASSERT(depth.load == LoadOp::Clear,
                       std::format("pass '{}' loads its depth attachment, which this RHI cannot "
                                   "express -- no pass owns depth contents to load",
                                   pass.label));
            // One load action covers both attachments in this RHI, so a depth clear beside a
            // colour load would silently clear the colour target too.
            LMX_ASSERT(desc.clear,
                       std::format("pass '{}' loads its colour attachment while clearing depth: "
                                   "this RHI carries one load action for the whole pass",
                                   pass.label));
            LMX_ASSERT(pass.desc.color || depth.store == StoreOp::Store,
                       std::format("pass '{}' is depth-only and discards its depth, leaving it no "
                                   "output at all",
                                   pass.label));
            desc.depthTarget = m_resources[depth.handle.index].texture;
            desc.clearDepth = depth.clearDepth;
            desc.storeDepth = depth.store == StoreOp::Store;
        }

        commands.beginRenderPass(desc);
        pass.execute(passResources(passIndex));
        commands.endRenderPass();

        const auto markRendered = [&](uint32_t resource) {
            rendered[resource] = true;
            readable[resource] = false;
        };
        if (pass.desc.color) {
            markRendered(pass.desc.color->handle.index);
        }
        if (pass.desc.depth) {
            markRendered(pass.desc.depth->handle.index);
        }
    }
}

//======================================================================================================================
PassResources RenderGraph::passResources(uint32_t passIndex) const {
    LMX_ASSERT(passIndex < m_passes.size(), "pass index names no declared pass");
    return PassResources(*this, passIndex);
}

} // namespace lmx::render
