//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraph.cpp
/// @brief Implements render-graph validation, scheduling, and execution.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/RenderGraph.h"

#include "Core/Assert.h"
#include "Render/GraphDump.h"

#include <algorithm>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lmx::render {
namespace {

//======================================================================================================================
bool isDepthFormat(rhi::Format format) {
    return format == rhi::Format::D32Float;
}

// A cubemap's faces are its array layers, which is what a declared range addresses.
constexpr uint32_t kCubeFaceCount = 6;

//======================================================================================================================
// The colour attachment role, on the same terms the RHI's own desc validation uses: 8-bit unorm
// targets, sRGB included, the single-channel unorm a mask target carries, plus the two float
// formats -- the half-float scene colour and the two-channel half-float a motion-vector target
// carries. It has to answer exactly as the RHI's predicate does, or the graph refuses a target
// the device would have accepted. A block-compressed format cannot be rendered into, and a depth
// format belongs in the other slot.
bool isColorRenderableFormat(rhi::Format format) {
    switch (format) {
    case rhi::Format::BGRA8Unorm:
    case rhi::Format::RGBA8Unorm:
    case rhi::Format::RGBA8Unorm_sRGB:
    case rhi::Format::RGBA16Float:
    case rhi::Format::RG16Float:
    case rhi::Format::R8Unorm:
        return true;
    case rhi::Format::R16Float:
    case rhi::Format::Unknown:
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
    case UseRole::ShaderRead:
    case UseRole::IndirectArgument:
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
    if (kind == PassKind::External) {
        return isWriteRole(role) ? rhi::TextureUse::ExternalWrite : rhi::TextureUse::ExternalRead;
    }
    switch (role) {
    case UseRole::Read:
        return kind == PassKind::Compute ? rhi::TextureUse::StorageRead
                                         : rhi::TextureUse::ShaderRead;
    case UseRole::ShaderRead:
        return rhi::TextureUse::ShaderRead;
    case UseRole::IndirectArgument:
        return rhi::TextureUse::ShaderRead;
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
    case UseRole::ShaderRead:
        return rhi::BufferUse::ShaderRead;
    case UseRole::IndirectArgument:
        return rhi::BufferUse::IndirectArgument;
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
//
// The counts are passed rather than read off an rhi::Texture because a transient has no texture
// until the frame is executed, and every rule below has to answer the same way for both kinds.
ResolvedRange resolveRange(const rhi::TextureSubresourceRange& range, uint32_t mipLevels,
                           uint32_t arrayLayers) {
    const auto lastOf = [](uint32_t base, uint32_t count, uint32_t available) {
        if (count == rhi::kAllMipLevels) {
            return available > base ? available - 1 : base;
        }
        if (count == 0) {
            return base;
        }
        const uint64_t last = static_cast<uint64_t>(base) + count - 1;
        return last > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                           : static_cast<uint32_t>(last);
    };
    return {.firstMip = range.baseMipLevel,
            .lastMip = lastOf(range.baseMipLevel, range.mipLevelCount, mipLevels),
            .firstLayer = range.baseArrayLayer,
            .lastLayer = lastOf(range.baseArrayLayer, range.arrayLayerCount, arrayLayers)};
}

//======================================================================================================================
bool validAxis(uint32_t base, uint32_t count, uint32_t available, uint32_t allSentinel) {
    if (base >= available) {
        return false;
    }
    return count == allSentinel || (count > 0 && count <= available - base);
}

//======================================================================================================================
// A range covers real subresources only when both axes do, so an empty count on either one is an
// empty range whatever the other says.
bool isEmptyRange(const rhi::TextureSubresourceRange& range) {
    return range.mipLevelCount == 0 || range.arrayLayerCount == 0;
}

//======================================================================================================================
// Whether every subresource of `inner` is one of `outer`'s. Ranges are rectangles, so this is exact
// on a single range and deliberately not extended to a union of several: two barriers whose ranges
// together cover a reader do not each order it, and treating them as if they did is the mistake
// this check exists to avoid.
bool enclosesRange(const ResolvedRange& outer, const ResolvedRange& inner) {
    return outer.firstMip <= inner.firstMip && inner.lastMip <= outer.lastMip &&
           outer.firstLayer <= inner.firstLayer && inner.lastLayer <= outer.lastLayer;
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
ResolvedRange intersectRange(const ResolvedRange& a, const ResolvedRange& b) {
    LMX_ASSERT(rangesOverlap(a, b), "only overlapping subresource ranges have an intersection");
    return {.firstMip = std::max(a.firstMip, b.firstMip),
            .lastMip = std::min(a.lastMip, b.lastMip),
            .firstLayer = std::max(a.firstLayer, b.firstLayer),
            .lastLayer = std::min(a.lastLayer, b.lastLayer)};
}

//======================================================================================================================
// `range` with `cut`'s overlap removed, as up to four axis-aligned mip x layer rectangles -- a
// write-after-read discharge must shrink a pending read to what the write did *not* touch rather
// than drop the whole entry, or a read of mips 0-3 discharged by a write of mip 0 alone would stop
// protecting mips 1-3 against a later write of just one of them. The four pieces are the two mip
// strips outside `cut`'s mip span (full layer range) plus the two layer strips inside it (only
// `cut`'s mip span, so the two families never overlap each other): a rectangle with a rectangular
// hole cut from it, split the same way any such shape decomposes into disjoint rectangles. Returns
// `range` unchanged when the two do not overlap at all, and nothing when `cut` encloses `range`.
std::vector<ResolvedRange> subtractRange(const ResolvedRange& range, const ResolvedRange& cut) {
    if (!rangesOverlap(range, cut)) {
        return {range};
    }
    const uint32_t cutFirstMip = std::max(range.firstMip, cut.firstMip);
    const uint32_t cutLastMip = std::min(range.lastMip, cut.lastMip);
    const uint32_t cutFirstLayer = std::max(range.firstLayer, cut.firstLayer);
    const uint32_t cutLastLayer = std::min(range.lastLayer, cut.lastLayer);

    std::vector<ResolvedRange> remainder;
    if (range.firstMip < cutFirstMip) {
        remainder.push_back({.firstMip = range.firstMip,
                             .lastMip = cutFirstMip - 1,
                             .firstLayer = range.firstLayer,
                             .lastLayer = range.lastLayer});
    }
    if (cutLastMip < range.lastMip) {
        remainder.push_back({.firstMip = cutLastMip + 1,
                             .lastMip = range.lastMip,
                             .firstLayer = range.firstLayer,
                             .lastLayer = range.lastLayer});
    }
    if (range.firstLayer < cutFirstLayer) {
        remainder.push_back({.firstMip = cutFirstMip,
                             .lastMip = cutLastMip,
                             .firstLayer = range.firstLayer,
                             .lastLayer = cutFirstLayer - 1});
    }
    if (cutLastLayer < range.lastLayer) {
        remainder.push_back({.firstMip = cutFirstMip,
                             .lastMip = cutLastMip,
                             .firstLayer = cutLastLayer + 1,
                             .lastLayer = range.lastLayer});
    }
    return remainder;
}

//======================================================================================================================
// The canonical range covering both, expressed the way a whole-resource declaration is: a union
// that reaches the end of the chain keeps the sentinel rather than a resolved count, so a barrier
// derived from whole-resource reads is indistinguishable from the declaration it came from.
rhi::TextureSubresourceRange unionRange(const ResolvedRange& a, const ResolvedRange& b,
                                        uint32_t mipLevels, uint32_t arrayLayers) {
    const uint32_t firstMip = std::min(a.firstMip, b.firstMip);
    const uint32_t lastMip = std::max(a.lastMip, b.lastMip);
    const uint32_t firstLayer = std::min(a.firstLayer, b.firstLayer);
    const uint32_t lastLayer = std::max(a.lastLayer, b.lastLayer);
    return {.baseMipLevel = firstMip,
            .mipLevelCount = lastMip + 1 >= mipLevels ? rhi::kAllMipLevels : lastMip - firstMip + 1,
            .baseArrayLayer = firstLayer,
            .arrayLayerCount =
                lastLayer + 1 >= arrayLayers ? rhi::kAllArrayLayers : lastLayer - firstLayer + 1};
}

//======================================================================================================================
// How a sink names itself in a validation message: "exported texture 'x' ... is not written by any
// pass" reads as the declaration the caller made.
std::string_view sinkVerb(SinkKind kind) {
    switch (kind) {
    case SinkKind::Export:
        return "exported";
    case SinkKind::Present:
        return "presented";
    case SinkKind::Readback:
        return "read-back";
    }
    return "rooted";
}

//======================================================================================================================
// The smallest multiple of `alignment` that is not below `value`. Alignments are powers of two in
// practice, but the arithmetic does not assume it -- the value comes from the backend.
uint64_t alignUp(uint64_t value, uint64_t alignment) {
    return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
}

// Everything two transients must agree on before one may take the other's bytes (spec 11).
//
// Equality on every axis rather than a subset is deliberate conservatism: a placement that reuses
// memory across differing layouts depends on how the driver tiles each one, which is exactly the
// thing a heap does not promise. Storage mode is absent because it is not a variable -- a transient
// is device-private by construction, since it can neither be uploaded to nor read back.
struct AliasClass {
    bool isTexture = true;
    rhi::Format format = rhi::Format::Unknown;
    rhi::TextureKind kind = rhi::TextureKind::Tex2D;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
    bool renderTarget = false;
    bool sampled = false;
    bool storageRead = false;
    bool storageWrite = false;
    uint64_t bufferSize = 0;
    // The backend's own answer for the descriptor, which is what the packing is actually built on.
    uint64_t size = 0;
    uint64_t alignment = 0;
    friend bool operator==(const AliasClass&, const AliasClass&) = default;
};

//======================================================================================================================
AliasClass aliasClassOf(bool isTexture, rhi::Format format, const TransientTextureDesc& texture,
                        const TransientBufferDesc& buffer, const rhi::SizeAlign& footprint) {
    if (!isTexture) {
        return {.isTexture = false,
                .storageRead = buffer.storageRead,
                .storageWrite = buffer.storageWrite,
                .bufferSize = buffer.size,
                .size = footprint.size,
                .alignment = footprint.alignment};
    }
    return {.isTexture = true,
            .format = format,
            .kind = texture.kind,
            .width = texture.width,
            .height = texture.height,
            .mipLevels = texture.mipLevels,
            .renderTarget = texture.renderTarget,
            .sampled = texture.sampled,
            .storageRead = texture.storageRead,
            .storageWrite = texture.storageWrite,
            .size = footprint.size,
            .alignment = footprint.alignment};
}

//======================================================================================================================
// A (resource, version) pair as one hashable key. Both halves are 32-bit, so the pair is lossless.
uint64_t versionKey(uint32_t resource, uint32_t version) {
    return (static_cast<uint64_t>(resource) << 32) | version;
}

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
GraphResult<Schedule> RenderGraph::compile() const {
    GraphResult<CompiledFrameRecord> record = compileFrame(0);
    if (!record) {
        return std::unexpected(record.error());
    }
    return std::move(record->debug.schedule);
}

//======================================================================================================================
GraphResult<void> RenderGraph::validateExtraColorAttachments(const Pass& pass) const {
    if (pass.extraColor.empty()) {
        return {};
    }
    // Extras are attachments 1 and up: the count is what the hardware can bind past attachment
    // zero, and attachment zero has to be there for any of them to mean anything.
    if (pass.extraColor.size() > rhi::kMaxExtraColorTargets) {
        return fail(std::format("pass '{}' declares {} extra color attachments, past the {} a "
                                "pass can bind beyond its primary one",
                                pass.label, pass.extraColor.size(), rhi::kMaxExtraColorTargets));
    }
    if (!pass.color) {
        const Resource& first = m_resources[pass.extraColor.front().handle.index];
        return fail(std::format("pass '{}' declares extra color attachment 0 '{}' and no color "
                                "attachment: extras are attachments 1 and up, so a pass "
                                "without attachment zero cannot have them",
                                pass.label, first.name));
    }

    const Resource& primary = m_resources[pass.color->handle.index];
    for (uint32_t index = 0; index < pass.extraColor.size(); ++index) {
        const Resource& extra = m_resources[pass.extraColor[index].handle.index];
        if (!isColorRenderableFormat(extra.format)) {
            return fail(std::format("pass '{}' extra color attachment {} '{}' declares format "
                                    "{}, which is not color-renderable",
                                    pass.label, index, extra.name, formatName(extra.format)));
        }
        // One rasterisation writes every attachment, so an extra of another extent has
        // fragments with nowhere to land.
        if (extra.width != primary.width || extra.height != primary.height) {
            return fail(std::format("pass '{}' attachment extent mismatch: color '{}' is {}x{} "
                                    "and extra color attachment {} '{}' is {}x{}",
                                    pass.label, primary.name, primary.width, primary.height, index,
                                    extra.name, extra.width, extra.height));
        }
        // Two attachments over one texture would have the pass write those texels twice in one
        // rasterisation, and no version says what they then hold.
        const bool repeatsPrimary = pass.extraColor[index].handle.index == pass.color->handle.index;
        const auto repeated = std::ranges::find(
            pass.extraColor.begin(), pass.extraColor.begin() + index,
            pass.extraColor[index].handle.index,
            [](const ColorAttachment& attachment) { return attachment.handle.index; });
        if (repeatsPrimary || repeated != pass.extraColor.begin() + index) {
            return fail(std::format("pass '{}' declares texture '{}' as two of its color "
                                    "attachments: a pass writes each of its attachments once, "
                                    "so it may not name one texture twice",
                                    pass.label, extra.name));
        }
    }
    return {};
}

//======================================================================================================================
GraphResult<void> RenderGraph::validateRenderArea(const Pass& pass) const {
    const uint32_t width = pass.renderAreaWidth;
    const uint32_t height = pass.renderAreaHeight;
    // Zero is the whole attachment, which is what every pass that never asked for a sub-region
    // carries.
    if (width == 0 && height == 0) {
        return {};
    }
    // A half-set pair reaches the backend as a viewport one of whose sides is zero, rasterising
    // nothing at all rather than the sub-rectangle the pass meant.
    if (width == 0 || height == 0) {
        return fail(std::format("pass '{}' declares render area {}x{}: both sides are zero -- the "
                                "whole attachment -- or both are non-zero",
                                pass.label, width, height));
    }
    // Every attachment is rasterised by the same fragments, so the area has to fit inside all of
    // them; the extras and the depth target are checked beside the primary rather than through it
    // so the message names the attachment that is actually too small.
    const auto fits = [&](const Resource& attachment) -> GraphResult<void> {
        if (width > attachment.width || height > attachment.height) {
            return fail(std::format("pass '{}' render area {}x{} exceeds attachment '{}' {}x{}",
                                    pass.label, width, height, attachment.name, attachment.width,
                                    attachment.height));
        }
        return {};
    };
    if (pass.color) {
        if (const GraphResult<void> primary = fits(m_resources[pass.color->handle.index]);
            !primary) {
            return primary;
        }
    }
    for (const ColorAttachment& extra : pass.extraColor) {
        if (const GraphResult<void> fitsExtra = fits(m_resources[extra.handle.index]); !fitsExtra) {
            return fitsExtra;
        }
    }
    if (pass.depth) {
        if (const GraphResult<void> fitsDepth = fits(m_resources[pass.depth->handle.index]);
            !fitsDepth) {
            return fitsDepth;
        }
    }
    return {};
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
            if (color.width != depth.width || color.height != depth.height) {
                return fail(std::format("pass '{}' attachment extent mismatch: color '{}' is {}x{} "
                                        "and depth '{}' is {}x{}",
                                        pass.label, color.name, color.width, color.height,
                                        depth.name, depth.width, depth.height));
            }
        }
        if (const GraphResult<void> extras = validateExtraColorAttachments(pass); !extras) {
            return std::unexpected(extras.error());
        }
        if (const GraphResult<void> area = validateRenderArea(pass); !area) {
            return std::unexpected(area.error());
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
            const bool validMips =
                validAxis(declaration.range.baseMipLevel, declaration.range.mipLevelCount,
                          resource.mipLevels, rhi::kAllMipLevels);
            const bool validLayers =
                validAxis(declaration.range.baseArrayLayer, declaration.range.arrayLayerCount,
                          resource.arrayLayers, rhi::kAllArrayLayers);
            if (!validMips || !validLayers) {
                return fail(std::format("pass '{}' declares a {} of texture '{}' over {}, which "
                                        "runs past its {} mip levels and {} array layers",
                                        pass.label, roleName(declaration.role), resource.name,
                                        describeRange(declaration.range), resource.mipLevels,
                                        resource.arrayLayers));
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
                if (!rangesOverlap(
                        resolveRange(read.range, resource.mipLevels, resource.arrayLayers),
                        resolveRange(write.range, resource.mipLevels, resource.arrayLayers))) {
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

    // Transient rules (spec 11). Both are what leaves a pooled frame indistinguishable from an
    // unpooled one. A transient's version 0 is uninitialised memory rather than contents --
    // whatever the previous occupant of those bytes left -- so consuming it is refused rather than
    // allowed to depend on the packing; and a transient stops existing with the frame, so no sink
    // can name one.
    for (const Pass& pass : m_passes) {
        for (const Declaration& declaration : pass.declarations) {
            const Resource& resource = m_resources[declaration.resource];
            if (!resource.transient || declaration.version > 0 || declaration.isWrite) {
                continue;
            }
            return fail(std::format(
                "pass '{}' declares a {} of transient {} '{}' version 0, whose contents no pass "
                "produced: a transient holds nothing until a pass writes it",
                pass.label, roleName(declaration.role),
                resource.kind == ResourceKind::Texture ? "texture" : "buffer", resource.name));
        }
        // An attachment that loads consumes the version it names as well as writing it, which the
        // flattened declaration above records as a write and so cannot catch.
        const auto loadsTransient = [&](const std::optional<GraphTexture>& handle, LoadOp load,
                                        std::string_view role) -> std::optional<std::string> {
            if (!handle || load != LoadOp::Load) {
                return std::nullopt;
            }
            const Resource& resource = m_resources[handle->index];
            if (!resource.transient || handle->version > 0) {
                return std::nullopt;
            }
            return std::format("pass '{}' loads transient texture '{}' version 0 as its {}, whose "
                               "contents no pass produced: a transient holds nothing until a pass "
                               "writes it",
                               pass.label, resource.name, role);
        };
        if (const auto message =
                loadsTransient(pass.color ? std::optional{pass.color->handle} : std::nullopt,
                               pass.color ? pass.color->load : LoadOp::Clear, "color attachment")) {
            return fail(*message);
        }
        for (uint32_t index = 0; index < pass.extraColor.size(); ++index) {
            if (const auto message =
                    loadsTransient(pass.extraColor[index].handle, pass.extraColor[index].load,
                                   std::format("extra color attachment {}", index))) {
                return fail(*message);
            }
        }
        if (const auto message =
                loadsTransient(pass.depth ? std::optional{pass.depth->handle} : std::nullopt,
                               pass.depth ? pass.depth->load : LoadOp::Clear, "depth attachment")) {
            return fail(*message);
        }
    }

    for (const Sink& sink : m_sinks) {
        const Resource& resource = m_resources[sink.resource];
        if (!resource.transient) {
            continue;
        }
        return fail(std::format("{} {} '{}' is transient: a transient lives for exactly one frame, "
                                "so nothing outside that frame can read it -- import a resource of "
                                "your own for a result that has to survive",
                                sinkVerb(sink.kind),
                                resource.kind == ResourceKind::Texture ? "texture" : "buffer",
                                resource.name));
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
            const bool discarded =
                (declaration.role == UseRole::ColorAttachment &&
                 colorAttachmentOf(m_passes[pass], declaration).store == StoreOp::Discard) ||
                (declaration.role == UseRole::DepthAttachment &&
                 m_passes[pass].depth->store == StoreOp::Discard);
            if (discarded) {
                continue;
            }
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

    for (const Sink& sink : m_sinks) {
        const Resource& resource = m_resources[sink.resource];
        if (!producerOfVersion.contains(versionKey(sink.resource, sink.version))) {
            return fail(
                std::format("{} {} '{}' version {} is not written by any pass", sinkVerb(sink.kind),
                            sink.resourceKind == ResourceKind::Texture ? "texture" : "buffer",
                            resource.name, sink.version));
        }
    }

    // Reverse reachability from the declared sinks, and from nothing else. A pass is live when a
    // sink names a version it produced, or when a live pass names one; a live pass's own reads pull
    // in whatever produced them, which is what carries liveness back down a chain.
    std::vector<bool> live(m_passes.size(), false);
    std::vector<uint32_t> reachable;
    const auto reach = [&](uint32_t resource, uint32_t version) {
        const auto producer = producerOfVersion.find(versionKey(resource, version));
        if (producer != producerOfVersion.end() && !live[producer->second]) {
            live[producer->second] = true;
            reachable.push_back(producer->second);
        }
    };
    for (const Sink& sink : m_sinks) {
        reach(sink.resource, sink.version);
    }
    while (!reachable.empty()) {
        const uint32_t pass = reachable.back();
        reachable.pop_back();
        for (const Declaration& declaration : m_passes[pass].declarations) {
            if (declaration.version > 0) {
                reach(declaration.resource, declaration.version);
            }
        }
    }

    // Kahn's algorithm, taking the lowest-numbered ready pass each round: that is the declaration
    // order tie-break, and it makes the schedule a function of the declarations alone. It runs over
    // every declared pass rather than the live ones, because a cycle is a property of the frame as
    // declared -- culling a cycle away would report a frame as valid that is not.
    Schedule schedule;
    std::vector<bool> scheduled(m_passes.size(), false);
    size_t ordered = 0;
    while (ordered < m_passes.size()) {
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
        ++ordered;
        if (live[ready]) {
            schedule.passes.push_back(ready);
        }
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
    record.debug.sinks.reserve(m_sinks.size());
    for (const Sink& sink : m_sinks) {
        record.debug.sinks.push_back({.kind = sink.kind,
                                      .resourceKind = sink.resourceKind == ResourceKind::Texture
                                                          ? GraphResourceKind::Texture
                                                          : GraphResourceKind::Buffer,
                                      .resource = sink.resource,
                                      .version = sink.version});
    }
    record.debug.passes.reserve(m_passes.size());
    for (uint32_t index = 0; index < m_passes.size(); ++index) {
        const Pass& pass = m_passes[index];
        DebugPass entry{.label = pass.label,
                        .kind = pass.kind,
                        .uses = {},
                        .cullReason = {},
                        .renderAreaWidth = pass.renderAreaWidth,
                        .renderAreaHeight = pass.renderAreaHeight};
        entry.uses.reserve(pass.declarations.size());
        bool writes = false;
        for (const Declaration& declaration : pass.declarations) {
            writes = writes || declaration.isWrite;
            entry.uses.push_back({.resource = declaration.resource,
                                  .version = declaration.version,
                                  .role = declaration.role,
                                  .range = declaration.range});
        }
        if (!live[index]) {
            entry.cullReason = writes ? CullReason::NoSinkReachesIt : CullReason::ProducesNothing;
        }
        record.debug.passes.push_back(std::move(entry));
    }
    const AliasPlan plan = planTransients(schedule);
    record.debug.transitions = deriveTransitions(schedule, plan);
    record.debug.transients.reserve(plan.transients.size());
    for (const TransientPlan& entry : plan.transients) {
        record.debug.transients.push_back(
            {.resource = entry.resource,
             .used = entry.used,
             // Positions are how lifetimes are compared; pass indices are how a reader names a
             // pass, so the record carries what the rest of it is written in.
             .firstPass = entry.used ? schedule.passes[entry.firstPosition] : 0,
             .lastPass = entry.used ? schedule.passes[entry.lastPosition] : 0,
             .offset = entry.offset,
             .size = entry.size,
             .alignment = entry.alignment,
             .aliases = entry.aliases});
    }
    record.debug.memory = plan.memory;
    record.debug.poolingEnabled = m_poolingEnabled;
    record.debug.schedule = std::move(schedule);
    return record;
}

//======================================================================================================================
RenderGraph::AliasPlan RenderGraph::planTransients(const Schedule& schedule) const {
    AliasPlan plan;

    // Lifetimes are intervals over execution order, not declaration order, so a pass's position in
    // the schedule is what an interval is measured in. A culled pass has no position at all, which
    // is exactly right: a transient only its culled readers named is one the frame does not
    // allocate.
    std::vector<AliasClass> classes;
    for (uint32_t index = 0; index < m_resources.size(); ++index) {
        const Resource& resource = m_resources[index];
        if (!resource.transient) {
            continue;
        }
        TransientPlan entry{.resource = index};
        for (uint32_t position = 0; position < schedule.passes.size(); ++position) {
            bool touches = false;
            for (const Declaration& declaration :
                 m_passes[schedule.passes[position]].declarations) {
                touches = touches || declaration.resource == index;
            }
            if (!touches) {
                continue;
            }
            entry.firstPosition = entry.used ? entry.firstPosition : position;
            entry.lastPosition = position;
            entry.used = true;
        }

        AliasClass klass;
        if (entry.used) {
            // The RHI is asked what the descriptor costs rather than the descriptor being measured
            // here: only the backend knows the layout it will choose, and a plan built on a guess
            // would place resources where they do not fit.
            rhi::Device& device = m_transients->device();
            const rhi::SizeAlign footprint = resource.kind == ResourceKind::Texture
                                                 ? device.textureSizeAlign(textureDescOf(resource))
                                                 : device.bufferSizeAlign(bufferDescOf(resource));
            entry.size = footprint.size;
            entry.alignment = footprint.alignment;
            klass = aliasClassOf(resource.kind == ResourceKind::Texture, resource.format,
                                 resource.textureDesc, resource.bufferDesc, footprint);
        }
        plan.transients.push_back(entry);
        classes.push_back(klass);
    }

    // First fit, walked in lifetime order so a placement's occupants are compared against a
    // candidate that starts no earlier than any of them: one high-water mark per placement is then
    // all the overlap test needs. Ties in lifetime start are broken by declaration order, which is
    // what makes the layout a function of the declarations alone.
    std::vector<uint32_t> order;
    for (uint32_t index = 0; index < plan.transients.size(); ++index) {
        if (plan.transients[index].used) {
            order.push_back(index);
        }
    }
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return plan.transients[a].firstPosition < plan.transients[b].firstPosition;
    });

    struct Placement {
        uint64_t offset = 0;
        AliasClass klass;
        // The last schedule position any occupant of these bytes reaches, and the occupant that
        // reaches it -- the resource a newcomer's reuse barrier has to order against.
        uint32_t lastPosition = 0;
        uint32_t lastOccupant = 0;
    };
    std::vector<Placement> placements;
    uint64_t highWater = 0;

    for (const uint32_t index : order) {
        TransientPlan& entry = plan.transients[index];
        plan.memory.requested += entry.size;

        bool reused = false;
        if (m_poolingEnabled) {
            for (Placement& placement : placements) {
                if (!(placement.klass == classes[index]) ||
                    placement.lastPosition >= entry.firstPosition) {
                    continue;
                }
                entry.offset = placement.offset;
                entry.aliases = true;
                entry.aliasedFrom = placement.lastOccupant;
                placement.lastPosition = entry.lastPosition;
                placement.lastOccupant = entry.resource;
                reused = true;
                break;
            }
        }
        if (!reused) {
            entry.offset = alignUp(highWater, entry.alignment);
            highWater = entry.offset + entry.size;
            placements.push_back({.offset = entry.offset,
                                  .klass = classes[index],
                                  .lastPosition = entry.lastPosition,
                                  .lastOccupant = entry.resource});
        }
    }

    plan.memory.highWater = highWater;
    plan.memory.aliasSavings =
        plan.memory.requested > highWater ? plan.memory.requested - highWater : 0;
    return plan;
}

//======================================================================================================================
std::vector<DebugTransition> RenderGraph::deriveTransitions(const Schedule& schedule,
                                                            const AliasPlan& plan) const {
    // What each resource was last written as, and which of its subresources a barrier has since
    // made visible to a reader. Writing a resource again puts it back in a producing state and
    // clears what was covered, so the transition is owed again.
    //
    // Coverage is per emitted range *and* per consuming stage class, because those are the two axes
    // a barrier is scoped on (rhi::CommandList::textureBarrier states the model). A barrier orders
    // the passes it sits between, so a reader of mip 1 is not ordered by a barrier that named mip 0
    // for an earlier reader; and a barrier consumed by a compute pass is scoped to that pass's
    // stages, so it orders nothing for a later raster reader of the same subresources. A pass's
    // kind is its stage class here, including opaque external operations. Passes of one kind are
    // ordered among themselves, so one barrier serves every later reader of that kind. Two kinds
    // whose stages happen to overlap
    // in a backend are still treated as distinct, which costs a redundant barrier rather than a
    // missed one. Whole-resource declarations -- what every raster pass here makes -- produce one
    // whole-resource range that encloses every later whole-resource reader of the same kind, so one
    // transition still serves them all.
    struct Covered {
        ResolvedRange range;
        PassKind consumer = PassKind::Raster;
    };
    struct TextureWriter {
        ResolvedRange range;
        rhi::TextureUse use = rhi::TextureUse::RenderTarget;
        bool writes = true;
        std::vector<Covered> covered;
    };
    struct WriteState {
        bool bufferWritten = false;
        rhi::BufferUse bufferUse = rhi::BufferUse::StorageWrite;
        std::vector<Covered> bufferCovered;
        std::vector<TextureWriter> textureWriters;
    };
    std::vector<WriteState> pending(m_resources.size());

    const auto textureUseWrites = [](rhi::TextureUse use) {
        return use == rhi::TextureUse::RenderTarget || use == rhi::TextureUse::StorageWrite ||
               use == rhi::TextureUse::CopyDestination || use == rhi::TextureUse::ExternalWrite;
    };
    const auto bufferUseWrites = [](rhi::BufferUse use) {
        return use == rhi::BufferUse::StorageWrite || use == rhi::BufferUse::CopyDestination;
    };

    // A texture accessed in an earlier frame starts with that whole-resource state. Prior writes
    // participate in RAW and WAW; prior reads skip RAW but remain available to the WAW loop below,
    // which emits the cross-frame WAR before this frame overwrites them.
    for (uint32_t index = 0; index < m_resources.size(); ++index) {
        if (const std::optional<rhi::TextureUse>& access = m_resources[index].priorTextureAccess) {
            const Resource& resource = m_resources[index];
            pending[index].textureWriters.push_back(
                {.range = {.firstMip = 0,
                           .lastMip = resource.mipLevels - 1,
                           .firstLayer = 0,
                           .lastLayer = resource.arrayLayers - 1},
                 .use = *access,
                 .writes = textureUseWrites(*access)});
        }
    }

    // Subresources read since the last write that touched them, not yet ordered against a future
    // write. A write-after-read hazard is the mirror of the read-after-write one above: a pass that
    // writes a version some earlier pass already read needs a barrier ordering it after that read,
    // or the write could retire before the read that depends on the prior contents does. A write
    // discharges only the ranges it actually overlaps -- a write of mip 3 says nothing about a read
    // of mip 0, so mip 0's read stays owed to whichever later write does overlap it. Tracking is
    // still per-resource rather than per-version, because whole-resource writes (every raster pass
    // here) discharge everything in one step and are the common case; ranged writes discharge in
    // parts instead of all at once, which is the property a full per-resource reset would lose.
    //
    // The use that made each read is carried per entry rather than once per resource, because the
    // producing side of a write-after-read barrier has to cover *every* reader the write is being
    // ordered after. Keeping only the last reader's use would let one reader's stage class stand in
    // for another's -- a scene pass's sampled read and a histogram dispatch's storage read of one
    // buffer, with the resolve dispatch that overwrites it waiting on the dispatch alone. A write
    // therefore emits one barrier per *distinct* use among the readers it overlaps, in first-read
    // order: separate barriers between the same two passes are how the RHI expresses a producing
    // side that spans several uses, and a backend accumulates them into the one dependency the
    // consuming pass emits.
    struct PendingRead {
        ResolvedRange range;
        rhi::TextureUse use = rhi::TextureUse::ShaderRead;
    };
    struct ReadState {
        std::vector<PendingRead> textureReads;
        // Buffers carry no subresource ranges, so a pending buffer read is always the whole
        // resource and only the distinct uses are worth keeping, in first-read order.
        std::vector<rhi::BufferUse> bufferReads;
    };
    std::vector<ReadState> pendingReads(m_resources.size());
    std::vector<DebugTransition> transitions;

    // Buffers split their earlier-frame terminal access between the same two states used for
    // accesses declared in this frame. This preserves the real use in debug records and lets a
    // prior read followed by another read remain barrier-free.
    for (uint32_t index = 0; index < m_resources.size(); ++index) {
        const std::optional<rhi::BufferUse>& access = m_resources[index].priorBufferAccess;
        if (!access) {
            continue;
        }
        if (bufferUseWrites(*access)) {
            pending[index].bufferWritten = true;
            pending[index].bufferUse = *access;
        } else {
            pendingReads[index].bufferReads.push_back(*access);
        }
    }

    // Every distinct use a resource makes at one lifetime boundary. The closing pass may read and
    // write disjoint subresources through different stages, and alias reuse has to wait on all of
    // them before the next logical resource takes those bytes.
    const auto usesAt = [&](uint32_t resource, uint32_t position) {
        const Pass& pass = m_passes[schedule.passes[position]];
        std::vector<UseRole> roles;
        for (const Declaration& declaration : pass.declarations) {
            if (declaration.resource != resource ||
                std::ranges::find(roles, declaration.role) != roles.end()) {
                continue;
            }
            roles.push_back(declaration.role);
        }
        LMX_ASSERT(!roles.empty(),
                   "a transient's lifetime bound must name a pass that declares it");
        return std::pair{pass.kind, std::move(roles)};
    };

    for (uint32_t position = 0; position < schedule.passes.size(); ++position) {
        const uint32_t passIndex = schedule.passes[position];
        const Pass& pass = m_passes[passIndex];
        const auto loadsAttachment = [&](const Declaration& declaration) {
            return (declaration.role == UseRole::ColorAttachment &&
                    colorAttachmentOf(pass, declaration).load == LoadOp::Load) ||
                   (declaration.role == UseRole::DepthAttachment &&
                    pass.depth->load == LoadOp::Load);
        };

        // Reuse boundaries come first: they make the memory this pass's transients sit in available
        // before anything else about the pass is ordered. Whole-resource whatever either side
        // declared, because the hazard is over shared bytes rather than over subresources, and
        // listed in declaration order so the sequence is a function of the declarations.
        for (const TransientPlan& entry : plan.transients) {
            if (!entry.aliasedFrom || entry.firstPosition != position) {
                continue;
            }
            const TransientPlan& previous =
                *std::ranges::find(plan.transients, *entry.aliasedFrom, &TransientPlan::resource);
            const auto [fromKind, fromRoles] = usesAt(previous.resource, previous.lastPosition);
            const auto [toKind, toRoles] = usesAt(entry.resource, entry.firstPosition);
            const UseRole toRole = toRoles.front();
            if (m_resources[entry.resource].kind == ResourceKind::Buffer) {
                std::vector<rhi::BufferUse> emitted;
                for (const UseRole fromRole : fromRoles) {
                    const rhi::BufferUse from = bufferUseOf(fromKind, fromRole);
                    if (std::ranges::find(emitted, from) != emitted.end()) {
                        continue;
                    }
                    emitted.push_back(from);
                    transitions.push_back({.beforePass = passIndex,
                                           .resource = entry.resource,
                                           .kind = GraphResourceKind::Buffer,
                                           .bufferFrom = from,
                                           .bufferTo = bufferUseOf(toKind, toRole),
                                           .aliasedFrom = previous.resource});
                }
            } else {
                std::vector<rhi::TextureUse> emitted;
                for (const UseRole fromRole : fromRoles) {
                    const rhi::TextureUse from = textureUseOf(fromKind, fromRole);
                    if (std::ranges::find(emitted, from) != emitted.end()) {
                        continue;
                    }
                    emitted.push_back(from);
                    transitions.push_back({.beforePass = passIndex,
                                           .resource = entry.resource,
                                           .kind = GraphResourceKind::Texture,
                                           .textureFrom = from,
                                           .textureTo = textureUseOf(toKind, toRole),
                                           .aliasedFrom = previous.resource});
                }
            }
        }

        // A read is ordered against every writer segment it overlaps. Several reads made through
        // the same use in one pass collapse into one range before that comparison. A loaded
        // attachment participates here too: it is a read and a write through RenderTarget.
        for (uint32_t index = 0; index < pass.declarations.size(); ++index) {
            const Declaration& read = pass.declarations[index];
            const bool reads = !read.isWrite || loadsAttachment(read);
            if (!reads) {
                continue;
            }
            const Resource& resource = m_resources[read.resource];
            bool declaredEarlier = false;
            for (uint32_t earlier = 0; earlier < index; ++earlier) {
                const Declaration& other = pass.declarations[earlier];
                const bool otherReads = !other.isWrite || loadsAttachment(other);
                declaredEarlier =
                    declaredEarlier || (otherReads && other.resource == read.resource &&
                                        (resource.kind == ResourceKind::Texture
                                             ? textureUseOf(pass.kind, other.role) ==
                                                   textureUseOf(pass.kind, read.role)
                                             : bufferUseOf(pass.kind, other.role) ==
                                                   bufferUseOf(pass.kind, read.role)));
            }
            if (declaredEarlier) {
                continue;
            }

            if (resource.kind == ResourceKind::Buffer) {
                WriteState& state = pending[read.resource];
                if (!state.bufferWritten) {
                    continue;
                }
                bool alreadyOrdered = false;
                for (const Covered& emitted : state.bufferCovered) {
                    alreadyOrdered = alreadyOrdered || emitted.consumer == pass.kind;
                }
                if (alreadyOrdered) {
                    continue;
                }
                state.bufferCovered.push_back({.consumer = pass.kind});
                transitions.push_back({.beforePass = passIndex,
                                       .resource = read.resource,
                                       .kind = GraphResourceKind::Buffer,
                                       .bufferFrom = state.bufferUse,
                                       .bufferTo = bufferUseOf(pass.kind, read.role)});
            } else {
                rhi::TextureSubresourceRange covered = read.range;
                for (uint32_t later = index + 1; later < pass.declarations.size(); ++later) {
                    const Declaration& other = pass.declarations[later];
                    const bool otherReads = !other.isWrite || loadsAttachment(other);
                    if (!otherReads || other.resource != read.resource ||
                        textureUseOf(pass.kind, other.role) != textureUseOf(pass.kind, read.role)) {
                        continue;
                    }
                    covered = unionRange(
                        resolveRange(covered, resource.mipLevels, resource.arrayLayers),
                        resolveRange(other.range, resource.mipLevels, resource.arrayLayers),
                        resource.mipLevels, resource.arrayLayers);
                }
                const ResolvedRange resolved =
                    resolveRange(covered, resource.mipLevels, resource.arrayLayers);
                struct NeededBarrier {
                    ResolvedRange range;
                    rhi::TextureUse from = rhi::TextureUse::RenderTarget;
                };
                std::vector<NeededBarrier> needed;
                for (TextureWriter& writer : pending[read.resource].textureWriters) {
                    if (!writer.writes || !rangesOverlap(writer.range, resolved)) {
                        continue;
                    }
                    const ResolvedRange overlap = intersectRange(writer.range, resolved);
                    bool alreadyOrdered = false;
                    for (const Covered& emitted : writer.covered) {
                        alreadyOrdered = alreadyOrdered || (emitted.consumer == pass.kind &&
                                                            enclosesRange(emitted.range, overlap));
                    }
                    if (alreadyOrdered) {
                        continue;
                    }
                    writer.covered.push_back({.range = overlap, .consumer = pass.kind});
                    const auto sameUse =
                        std::ranges::find(needed, writer.use, &NeededBarrier::from);
                    if (sameUse == needed.end()) {
                        needed.push_back({.range = overlap, .from = writer.use});
                    } else {
                        sameUse->range =
                            resolveRange(unionRange(sameUse->range, overlap, resource.mipLevels,
                                                    resource.arrayLayers),
                                         resource.mipLevels, resource.arrayLayers);
                    }
                }
                for (const NeededBarrier& barrier : needed) {
                    transitions.push_back(
                        {.beforePass = passIndex,
                         .resource = read.resource,
                         .kind = GraphResourceKind::Texture,
                         .range = unionRange(barrier.range, barrier.range, resource.mipLevels,
                                             resource.arrayLayers),
                         .textureFrom = barrier.from,
                         .textureTo = textureUseOf(pass.kind, read.role)});
                }
            }
        }

        // Every overlapping write is an access conflict on Metal 4's untracked resources. A loaded
        // attachment was already ordered as a read above, so that one dependency also orders its
        // write and need not be duplicated here.
        for (const Declaration& write : pass.declarations) {
            if (!write.isWrite) {
                continue;
            }
            const Resource& resource = m_resources[write.resource];
            WriteState& state = pending[write.resource];
            if (resource.kind == ResourceKind::Buffer) {
                if (state.bufferWritten) {
                    transitions.push_back({.beforePass = passIndex,
                                           .resource = write.resource,
                                           .kind = GraphResourceKind::Buffer,
                                           .bufferFrom = state.bufferUse,
                                           .bufferTo = bufferUseOf(pass.kind, write.role)});
                }
                continue;
            }
            const ResolvedRange writeRange =
                resolveRange(write.range, resource.mipLevels, resource.arrayLayers);
            for (const TextureWriter& writer : state.textureWriters) {
                // Loading an attachment already took the RAW path above for a prior write. A
                // prior read is different: the attachment's write still owes it a WAR barrier.
                if ((loadsAttachment(write) && writer.writes) ||
                    !rangesOverlap(writer.range, writeRange)) {
                    continue;
                }
                const ResolvedRange overlap = intersectRange(writer.range, writeRange);
                transitions.push_back({.beforePass = passIndex,
                                       .resource = write.resource,
                                       .kind = GraphResourceKind::Texture,
                                       .range = unionRange(overlap, overlap, resource.mipLevels,
                                                           resource.arrayLayers),
                                       .textureFrom = writer.use,
                                       .textureTo = textureUseOf(pass.kind, write.role)});
            }
        }

        // A write-after-read barrier per write declaration that overlaps a pending read -- not once
        // per prior reader, since those readers needed no ordering among themselves and the write
        // is what has to wait for the last of them. This checks reads recorded by *earlier* passes
        // only: this pass's own reads (if any) are not recorded into `pendingReads` until the loop
        // below runs, which is what lets a pass read and write one resource through disjoint ranges
        // (bloom's downsample step, one buffer accumulate dispatch) without owing a barrier against
        // itself. Discharge is at *subresource* granularity, not whole-entry: a write shrinks each
        // overlapping pending-read entry to the subresources it did not touch (`subtractRange`)
        // rather than dropping the entry outright, and two hazards motivate that precision. First,
        // one write must not discharge a disjoint pending read sharing only the resource, not any
        // subresources: A reads mip 0, B writes mip 3 -- disjoint, no barrier, but clearing mip 0's
        // whole entry anyway would leave C's later write of mip 0 wrongly finding nothing owed.
        // Second, a write covering *part* of one read entry must not discharge the rest of that
        // same entry: A reads mips 0-3 in one declaration, D writes mip 0 alone (the two overlap,
        // so a barrier is owed before D) -- but erasing the whole 0-3 entry on that overlap would
        // leave a later write of mip 2 by E wrongly finding nothing owed either, though A's read of
        // mip 2 was never ordered against it.
        for (const Declaration& declaration : pass.declarations) {
            if (!declaration.isWrite) {
                continue;
            }
            ReadState& reads = pendingReads[declaration.resource];
            const Resource& resource = m_resources[declaration.resource];
            if (resource.kind == ResourceKind::Buffer) {
                // Buffers carry no subresource ranges, so any pending read is the whole resource
                // and every write discharges it completely -- there is no partial case to preserve.
                // One barrier per distinct reading use, so the producing side covers every reader.
                for (const rhi::BufferUse readUse : reads.bufferReads) {
                    transitions.push_back({.beforePass = passIndex,
                                           .resource = declaration.resource,
                                           .kind = GraphResourceKind::Buffer,
                                           .bufferFrom = readUse,
                                           .bufferTo = bufferUseOf(pass.kind, declaration.role)});
                }
                reads.bufferReads.clear();
            } else if (!reads.textureReads.empty()) {
                const ResolvedRange writeRange =
                    resolveRange(declaration.range, resource.mipLevels, resource.arrayLayers);
                // Shrink each pending entry to what this write did not touch, rather than dropping
                // an entry outright the moment any part of it overlaps -- see the comment above.
                // Each surviving piece keeps the use that read it, so a later write over it names
                // that reader too.
                std::vector<PendingRead> remaining;
                remaining.reserve(reads.textureReads.size());
                std::vector<rhi::TextureUse> overlappedUses;
                for (const PendingRead& pendingRead : reads.textureReads) {
                    if (!rangesOverlap(pendingRead.range, writeRange)) {
                        remaining.push_back(pendingRead);
                        continue;
                    }
                    if (std::ranges::find(overlappedUses, pendingRead.use) ==
                        overlappedUses.end()) {
                        overlappedUses.push_back(pendingRead.use);
                    }
                    for (const ResolvedRange& piece :
                         subtractRange(pendingRead.range, writeRange)) {
                        remaining.push_back({.range = piece, .use = pendingRead.use});
                    }
                }
                for (const rhi::TextureUse readUse : overlappedUses) {
                    transitions.push_back({.beforePass = passIndex,
                                           .resource = declaration.resource,
                                           .kind = GraphResourceKind::Texture,
                                           .range = declaration.range,
                                           .textureFrom = readUse,
                                           .textureTo = textureUseOf(pass.kind, declaration.role)});
                }
                reads.textureReads = std::move(remaining);
            }
        }

        // Record every read this pass makes, whatever wrote the version it names, so a later write
        // to the same resource knows what it must be ordered after. Appended rather than
        // deduplicated against what RAW already covered above: RAW orders a reader against its
        // producer, this orders a future writer against the reader, and the two barriers answer
        // different questions even when they happen to share a `from` use. Deliberately after the
        // write-after-read check above, not before: this pass's own reads must not count as a prior
        // reader of themselves.
        for (const Declaration& read : pass.declarations) {
            if (read.isWrite) {
                continue;
            }
            const Resource& resource = m_resources[read.resource];
            ReadState& reads = pendingReads[read.resource];
            if (resource.kind == ResourceKind::Buffer) {
                const rhi::BufferUse use = bufferUseOf(pass.kind, read.role);
                if (std::ranges::find(reads.bufferReads, use) == reads.bufferReads.end()) {
                    reads.bufferReads.push_back(use);
                }
            } else {
                reads.textureReads.push_back(
                    {.range = resolveRange(read.range, resource.mipLevels, resource.arrayLayers),
                     .use = textureUseOf(pass.kind, read.role)});
            }
        }

        for (const Declaration& declaration : pass.declarations) {
            if (!declaration.isWrite) {
                continue;
            }
            const Resource& resource = m_resources[declaration.resource];
            WriteState& state = pending[declaration.resource];
            if (resource.kind == ResourceKind::Buffer) {
                state.bufferWritten = true;
                state.bufferUse = bufferUseOf(pass.kind, declaration.role);
                state.bufferCovered.clear();
                continue;
            }

            const ResolvedRange writeRange =
                resolveRange(declaration.range, resource.mipLevels, resource.arrayLayers);
            std::vector<TextureWriter> inherited;
            for (const TextureWriter& writer : state.textureWriters) {
                for (const ResolvedRange& piece : subtractRange(writer.range, writeRange)) {
                    inherited.push_back({.range = piece,
                                         .use = writer.use,
                                         .writes = writer.writes,
                                         .covered = writer.covered});
                }
            }
            inherited.push_back({.range = writeRange,
                                 .use = textureUseOf(pass.kind, declaration.role),
                                 .writes = true,
                                 .covered = {}});
            state.textureWriters = std::move(inherited);
        }
    }
    return transitions;
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
