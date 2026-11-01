//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveInternal.h
/// @brief Shares the accumulation declaration and debug-view predicates across temporal sources.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Passes/Temporal/TemporalFrameData.h"
#include "Render/Passes/Temporal/TemporalResolve.h"

#include <span>
#include <string_view>

namespace lmx::render::temporal_detail {

// Borrowed declaration facts consumed immediately; parameter bytes are copied for execution.
// TemporalResolve.slang and TemporalUpscale.slang share TemporalResolveRoot, which is what lets
// one declaration serve both kernels.
struct ReconstructionDeclaration {
    std::span<const std::byte> parameters;
    uint64_t parameterAlignment;
    rojoRHI::Pipeline pipeline;
    std::string_view passName;
    uint32_t width;
    uint32_t height;
    bool rejectionWanted;
    bool reprojectedWanted;
};

bool viewReadsRejection(TemporalDebugView view);
bool viewReadsReprojected(TemporalDebugView view);

} // namespace lmx::render::temporal_detail
