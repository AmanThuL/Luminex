//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphRanges.cpp
/// @brief Implements shared graph range resolution and declaration predicates.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Graph/RenderGraph.h"
#include "Render/Graph/RenderGraphInternal.h"

#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace lmx::render::graph_detail {

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
// Resolution saturates rather than wrapping: a count past the end of the chain is caught by the
// containment check below, and this must produce a comparable bound for that message to name.
//
// The counts are passed rather than read off an rojoRHI::Texture because a transient has no texture
// until the frame is executed, and every rule below has to answer the same way for both kinds.
ResolvedRange resolveRange(const rojoRHI::TextureSubresourceRange& range, uint32_t mipLevels,
                           uint32_t arrayLayers) {
    const auto lastOf = [](uint32_t base, uint32_t count, uint32_t available) {
        if (count == rojoRHI::kAllMipLevels) {
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
// Two ranges overlap only when they share a subresource, which takes both axes intersecting: mip 1
// of layer 0 and mip 1 of layer 1 are different subresources.
bool rangesOverlap(const ResolvedRange& a, const ResolvedRange& b) {
    const bool mips = a.firstMip <= b.lastMip && b.firstMip <= a.lastMip;
    const bool layers = a.firstLayer <= b.lastLayer && b.firstLayer <= a.lastLayer;
    return mips && layers;
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
std::unexpected<GraphError> fail(std::string message) {
    return std::unexpected(GraphError{.message = std::move(message)});
}

} // namespace lmx::render::graph_detail
