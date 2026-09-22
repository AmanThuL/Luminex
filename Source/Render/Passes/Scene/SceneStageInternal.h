//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStageInternal.h
/// @brief Shares scene shader mirror layouts and binding slots across stage units.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Passes/LocalLights/LightClusters.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

namespace lmx::render::scene_detail {

// Mirrors Shaders/Common/Lighting.slang's DirLight.
struct DirLightUniform {
    glm::vec3 strength;     // 0
    float strengthPadding;  // 12
    glm::vec3 direction;    // 16
    float directionPadding; // 28
};
static_assert(sizeof(DirLightUniform) == 32, "must match Lighting.slang's DirLight");

// Mirrors Shaders/Passes/Scene/ScenePass.slang's PassUniforms.
struct PassUniforms {
    glm::mat4 viewProj;        // 0
    glm::mat4 shadowTransform; // 64
    glm::vec3 eyePos;          // 128
    float eyePadding;          // 140 -- the float3's tail
    float time;                // 144
    float preExposure;         // 148
    float alignmentPadding[2]; // 152 -- lights[] carries float3s and realigns to 16
    DirLightUniform lights[3]; // 160
    int32_t shadowFilter;      // 256
    int32_t tailPadding[3];    // 260
    // The motion pair, unjittered: rasterisation carries the jitter in PassUniforms.viewProj, and
    // motion must not, or a still scene would move by the jitter delta every frame.
    glm::mat4 viewProjUnjittered;         // 272
    glm::mat4 previousViewProjUnjittered; // 336
};
static_assert(sizeof(PassUniforms) == 400, "must match ScenePass.slang's PassUniforms");

// Mirrors Shaders/Passes/Scene/Sky.slang's SkyUniforms.
struct SkyUniforms {
    glm::mat4 viewProj;  // 0 -- unjittered on the temporal path; jitterNdc offsets the raster
    glm::vec3 eyePos;    // 64
    float eyePadding;    // 76 -- the float3's tail
    float preExposure;   // 80
    float jitterNdcX;    // 84
    float jitterNdcY;    // 88
    float jitterPadding; // 92 -- rounds the pair up to the matrix's 16-byte alignment
    // The previous frame's camera. Drawing the sphere at the previous eye is what leaves the sky's
    // motion carrying the camera's rotation and nothing else.
    glm::mat4 previousViewProj;  // 96
    glm::vec3 previousEyePos;    // 160
    float previousEyePosPadding; // 172 -- the float3's tail, rounds the struct to 176
};
static_assert(sizeof(SkyUniforms) == 176, "must match Sky.slang's SkyUniforms");

// Mirrors the scalar-packed LocalLightParams at b11 in LocalLights.slang.
struct LocalLightParams {
    uint32_t mode;                                // 0 -- LocalLightMode
    uint32_t rowCount;                            // 4
    uint32_t gridX;                               // 8
    uint32_t gridY;                               // 12
    uint32_t gridZ;                               // 16
    uint32_t activeOriginX;                       // 20
    uint32_t activeOriginY;                       // 24
    uint32_t activeWidth;                         // 28
    uint32_t activeHeight;                        // 32
    float sliceDepth[kClusterSliceBoundaryCount]; // 36
};
static_assert(sizeof(LocalLightParams) == 136, "must match LocalLights.slang's LocalLightParams");
static_assert(offsetof(LocalLightParams, rowCount) == 4);
static_assert(offsetof(LocalLightParams, gridX) == 8);
static_assert(offsetof(LocalLightParams, activeWidth) == 28);
static_assert(offsetof(LocalLightParams, sliceDepth) == 36);

// Shaders/Common/Shadow.slang's kShadowFilterPcf / kShadowFilterPcss.
constexpr int32_t kShadowFilterPcf = 0;
constexpr int32_t kShadowFilterPcss = 1;

constexpr uint32_t kVertexBufferSlot = 0;
constexpr uint32_t kPassUniformsSlot = 2;
// The persistent exposure buffer (spec 9), read by ScenePassAuto.slang/SkyAuto.slang's fragment
// shaders -- the pipelines SceneStage selects only while auto-exposure is on. Bound via
// bindBuffer (a plain buffer read, not a storage binding) so a *raster* pass may read it --
// bindStorageBuffer is compute-pass-only.
constexpr uint32_t kExposureOverrideSlot = 3;
// The scene pass's texture slot map, which Shaders/Passes/Scene/ScenePass.slang's header documents
// in full. Two groups share one index space: a per-draw material set rebound for every DrawItem,
// and a per-pass shared set bound once before the draw loop.
//
// Slot 2 is the sky cubemap. Only Shaders/Passes/Scene/Sky.slang reads it, and the sky draws at the
// end of this same render pass, so it is bound beside that draw rather than with the shared set --
// the scene fragment stopped sampling the sky when the prefiltered environment (slot 8) replaced
// its ad-hoc mirror reflection.
constexpr uint32_t kDiffuseTextureSlot = 0;
constexpr uint32_t kNormalTextureSlot = 1;
constexpr uint32_t kSkyTextureSlot = 2;
constexpr uint32_t kShadowTextureSlot = 3;
constexpr uint32_t kMetallicRoughnessTextureSlot = 4;
constexpr uint32_t kOcclusionTextureSlot = 5;
constexpr uint32_t kEmissiveTextureSlot = 6;
constexpr uint32_t kIrradianceTextureSlot = 7;
constexpr uint32_t kPrefilteredEnvTextureSlot = 8;
constexpr uint32_t kDfgLutTextureSlot = 9;
constexpr uint32_t kLinearSamplerSlot = 0;
constexpr uint32_t kShadowSamplerSlot = 1;
constexpr uint32_t kIblSamplerSlot = 2;

} // namespace lmx::render::scene_detail
