//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveInternal.h
/// @brief Shares temporal resampling uniform layouts and binding slots across kernels.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#ifndef LMX_PENDING_TEMPORAL
#include "Render/Passes/Temporal/TemporalFrameData.h"
#include "Render/Passes/Temporal/TemporalResolve.h"

#include <span>
#include <string_view>

namespace lmx::render::temporal_detail {

// Borrowed declaration facts consumed immediately; parameter bytes are copied for execution.
struct ReconstructionDeclaration {
    std::span<const std::byte> parameters;
    uint64_t parameterAlignment;
    rojoRHI::ComputePipeline& pipeline;
    std::string_view passName;
    uint32_t width;
    uint32_t height;
    bool rejectionWanted;
    bool reprojectedWanted;
};

// TemporalResolve.slang's slot map.
constexpr uint32_t kResolveSceneColorSlot = 0;    // texture
constexpr uint32_t kResolveDepthSlot = 1;         // texture
constexpr uint32_t kResolvePreviousDepthSlot = 2; // texture
constexpr uint32_t kResolveMotionSlot = 3;        // texture
constexpr uint32_t kResolveReactiveSlot = 4;      // texture
constexpr uint32_t kResolveHistorySlot = 5;       // texture
constexpr uint32_t kResolveOutputSlot = 6;        // storage texture
constexpr uint32_t kResolveRejectionSlot = 7;     // storage texture
constexpr uint32_t kResolveReprojectedSlot = 8;   // storage texture
constexpr uint32_t kResolveSamplerSlot = 0;       // sampler
constexpr uint32_t kResolveExposureSlot = 0;      // buffer
constexpr uint32_t kResolveParamsSlot = 1;        // buffer

// Shaders/Passes/Temporal/TemporalUpscale.slang's slot map is the resolve's above, kResolve* for
// kResolve*, which is what lets one declaration serve both kernels.

bool viewReadsRejection(TemporalDebugView view);
bool viewReadsReprojected(TemporalDebugView view);

} // namespace lmx::render::temporal_detail

#endif
