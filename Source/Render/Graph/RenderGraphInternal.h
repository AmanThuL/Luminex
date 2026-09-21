//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphInternal.h
/// @brief Shares private graph range and declaration helpers across compilation units.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Core/Containers/Interval.h"
#include "Render/Graph/RenderGraph.h"

namespace lmx::render::graph_detail {

// Inclusive, resolved mip/layer bounds shared by validation and transition derivation.
struct ResolvedRange {
    Interval mips{0, 1};
    Interval layers{0, 1};
};

bool isWriteRole(UseRole role);
ResolvedRange resolveRange(const rojoRHI::TextureSubresourceRange& range, uint32_t mipLevels,
                           uint32_t arrayLayers);
bool rangesOverlap(const ResolvedRange& a, const ResolvedRange& b);
std::string_view sinkVerb(SinkKind kind);
std::unexpected<GraphError> fail(std::string message);

} // namespace lmx::render::graph_detail
