//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolveInternal.h
/// @brief Shares temporal resampling uniform layouts and binding slots across kernels.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/TemporalResolve.h"

namespace lmx::render::temporal_detail {

// Mirrors Shaders/Passes/Temporal/SpatialUpscale.slang's SpatialUpscaleParams.
struct SpatialUpscaleParams {
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    uint32_t allocatedWidth = 0;
    uint32_t allocatedHeight = 0;
    glm::vec2 jitterOffset{0.0f};
};
static_assert(sizeof(SpatialUpscaleParams) == 32,
              "must match SpatialUpscale.slang's SpatialUpscaleParams");

// Mirrors Shaders/Passes/Temporal/TemporalUpscale.slang's TemporalUpscaleParams:
// TemporalResolveParams' fields, in its order and with its trailing pad, then the four the
// upscaling kernel appends. Appending is what keeps the two blocks comparable field for field.
struct TemporalUpscaleParams {
    uint32_t width = 0; // The active render extent, which the render-extent inputs are read within.
    uint32_t height = 0;
    uint32_t historyValid = 0;
    uint32_t writeDiagnostics = 0; // Bit 0: rejection; bit 1: reprojected history.
    glm::mat4 inverseViewProjection{1.0f};
    glm::mat4 previousViewProjection{1.0f};
    float previousNearZ = 0.0f;
    float pad[3] = {0.0f, 0.0f, 0.0f};
    uint32_t outputWidth = 0; // The dispatch bound: one invocation per output pixel.
    uint32_t outputHeight = 0;
    uint32_t allocatedWidth = 0; // The allocation every render-extent UV is taken over.
    uint32_t allocatedHeight = 0;
    uint32_t previousRenderWidth = 0; // The extent the previous depth slot was rendered at.
    uint32_t previousRenderHeight = 0;
    glm::vec2 jitterOffset{0.0f};
};
static_assert(sizeof(TemporalUpscaleParams) == 192,
              "must match TemporalUpscale.slang's TemporalUpscaleParams");

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

constexpr uint32_t kComputeThreadsPerGroup2D = 8;

SpatialUpscaleParams spatialUpscaleParams(const TemporalInputs& inputs);
TemporalUpscaleParams temporalUpscaleParams(const TemporalInputs& inputs, bool historyValid,
                                            uint32_t writeDiagnostics);
bool viewReadsRejection(TemporalDebugView view);
bool viewReadsReprojected(TemporalDebugView view);

} // namespace lmx::render::temporal_detail
