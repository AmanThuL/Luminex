//----------------------------------------------------------------------------------------------------------------------
/// @file SceneFrameData.h
/// @brief Declares the scene, sky and local-light frame data and the pure functions that build it.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Lights/DirectionalLight.h"
#include "Engine/Lights/LocalLight.h"
#include "Render/Passes/LocalLights/LightClusters.h"
#include "Render/Passes/Temporal/Temporal.h"
#include "Render/Renderer/SceneView.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace lmx::render::scene_detail {

/// Mirrors Shaders/Common/Lighting.slang's DirLight.
struct DirLightUniform {
    glm::vec3 strength;     ///< 0 -- scene-linear RGB radiance.
    float strengthPadding;  ///< 12 -- the float3's tail.
    glm::vec3 direction;    ///< 16 -- the way the rays travel, world space.
    float directionPadding; ///< 28 -- the float3's tail.
};
static_assert(sizeof(DirLightUniform) == 32, "must match Lighting.slang's DirLight");

/// Mirrors Shaders/Passes/Scene/ScenePass.slang's PassUniforms.
struct PassUniforms {
    glm::mat4 viewProj;        ///< 0 -- the raster matrix, jittered on the temporal path.
    glm::mat4 shadowTransform; ///< 64 -- world to shadow texture space.
    glm::vec3 eyePos;          ///< 128 -- world-space eye.
    float eyePadding;          ///< 140 -- the float3's tail.
    float time;                ///< 144 -- frame clock, seconds.
    float preExposure;         ///< 148 -- exp2 of the manual exposure, a multiplier.
    float alignmentPadding[2]; ///< 152 -- lights[] carries float3s and realigns to 16.
    DirLightUniform lights[3]; ///< 160 -- key, fill, rim.
    int32_t shadowFilter;      ///< 256 -- kShadowFilterPcf or kShadowFilterPcss.
    int32_t shadowCaster;      ///< 260 -- role index, -1 disables contribution without new passes.
    int32_t tailPadding[2];    ///< 264 -- rounds the block up to the matrix alignment.
    /// The motion pair, unjittered: rasterisation carries the jitter in viewProj, and motion must
    /// not, or a still scene would move by the jitter delta every frame.
    glm::mat4 viewProjUnjittered;         ///< 272 -- this frame's unjittered view-projection.
    glm::mat4 previousViewProjUnjittered; ///< 336 -- the previous camera's, unjittered.
};
static_assert(sizeof(PassUniforms) == 400, "must match ScenePass.slang's PassUniforms");

/// Mirrors Shaders/Passes/Scene/Sky.slang's SkyUniforms.
struct SkyUniforms {
    glm::mat4 viewProj;  ///< 0 -- unjittered on the temporal path; jitterNdc offsets the raster.
    glm::vec3 eyePos;    ///< 64 -- world-space eye.
    float eyePadding;    ///< 76 -- the float3's tail.
    float preExposure;   ///< 80 -- exp2 of the manual exposure, a multiplier.
    float jitterNdcX;    ///< 84 -- raster jitter in NDC.
    float jitterNdcY;    ///< 88 -- raster jitter in NDC.
    float jitterPadding; ///< 92 -- rounds the pair up to the matrix's 16-byte alignment.
    /// The previous frame's camera. Drawing the sphere at the previous eye is what leaves the sky's
    /// motion carrying the camera's rotation and nothing else.
    glm::mat4 previousViewProj;  ///< 96 -- the previous camera's unjittered view-projection.
    glm::vec3 previousEyePos;    ///< 160 -- the previous camera's world position.
    float previousEyePosPadding; ///< 172 -- the float3's tail, rounds the struct to 176.
};
static_assert(sizeof(SkyUniforms) == 176, "must match Sky.slang's SkyUniforms");

/// Mirrors the scalar-packed LocalLightParams at b11 in LocalLights.slang.
struct LocalLightParams {
    uint32_t mode;                                ///< 0 -- engine::LocalLightMode's value.
    uint32_t rowCount;                            ///< 4 -- addressable light rows; 0 when Off.
    uint32_t gridX;                               ///< 8 -- froxel columns.
    uint32_t gridY;                               ///< 12 -- froxel rows.
    uint32_t gridZ;                               ///< 16 -- depth slices.
    uint32_t activeOriginX;                       ///< 20 -- the active rectangle's left, pixels.
    uint32_t activeOriginY;                       ///< 24 -- the active rectangle's top, pixels.
    uint32_t activeWidth;                         ///< 28 -- the active rectangle's width, pixels.
    uint32_t activeHeight;                        ///< 32 -- the active rectangle's height, pixels.
    float sliceDepth[kClusterSliceBoundaryCount]; ///< 36 -- slice boundaries, view distance.
};
static_assert(sizeof(LocalLightParams) == 136, "must match LocalLights.slang's LocalLightParams");
static_assert(offsetof(LocalLightParams, rowCount) == 4);
static_assert(offsetof(LocalLightParams, gridX) == 8);
static_assert(offsetof(LocalLightParams, activeWidth) == 28);
static_assert(offsetof(LocalLightParams, sliceDepth) == 36);

/// Shaders/Common/Shadow.slang's kShadowFilterPcf.
constexpr int32_t kShadowFilterPcf = 0;
/// Shaders/Common/Shadow.slang's kShadowFilterPcss.
constexpr int32_t kShadowFilterPcss = 1;

} // namespace lmx::render::scene_detail

namespace lmx::render {

/// Plain values the scene pass uniforms are built from.
struct PassUniformInputs {
    CameraFrameState camera;         ///< This frame's camera, jittered and unjittered matrices.
    CameraFrameState previousCamera; ///< The previous declared camera, or this one on frame one.
    bool temporalEnabled;            ///< Rasterise with the jittered matrix when true.
    glm::mat4 shadowTransform;       ///< World-to-shadow-texture transform.
    glm::vec3 eyePosition;           ///< World-space eye position.
    float timeSeconds;               ///< Frame clock, seconds.
    float exposureEv;                ///< Manual exposure in stops.
    std::span<const engine::DirectionalLight, 3> lights; ///< Key, fill, rim.
    int32_t shadowCaster;      ///< Role index of the shadow caster; -1 disables it.
    ShadowFilter shadowFilter; ///< PCSS selects the PCSS constant, anything else PCF.
};

/// Builds the scene pass uniforms. `viewProj` is the jittered matrix with temporal on and the
/// plain one otherwise, the motion pair is always unjittered, and `preExposure` is
/// exp2(exposureEv). A light's `enabled` flag is not read here: its strength is copied as given.
scene_detail::PassUniforms buildPassUniforms(const PassUniformInputs& inputs);

/// Plain values the local-light parameters are built from.
struct LocalLightParamInputs {
    engine::LocalLightMode requestedMode; ///< The mode the view asks for.
    uint32_t liveLightCount;              ///< Enabled local lights; 0 forces Off.
    uint32_t lightRowCount;               ///< Addressable light rows, free slots included.
    bool hasClusters;                     ///< This frame supplies a cluster grid and index list.
    FrameExtents extents;                 ///< The active rectangle is the render extent.
    float nearZ;                          ///< Positive near-plane distance, view units.
};

/// Builds the local-light parameters. Off, or no live light, writes mode Off and row count 0; the
/// grid constants are the fixed froxel grid, the active rectangle is the render extent anchored at
/// the origin and the slice depths are clusterSliceDepths(nearZ). Requesting Clustered without
/// clusters, or a non-positive near plane, is a programming error.
scene_detail::LocalLightParams buildLocalLightParams(const LocalLightParamInputs& inputs);

/// Plain values the sky uniforms are built from.
struct SkyUniformInputs {
    glm::mat4 viewProj;         ///< This frame's unjittered view-projection.
    glm::vec3 eyePos;           ///< World-space eye position.
    float preExposure;          ///< The pass uniforms' pre-exposure multiplier.
    glm::vec2 jitterNdc;        ///< Raster jitter in NDC.
    glm::mat4 previousViewProj; ///< The previous camera's unjittered view-projection.
    glm::vec3 previousEyePos;   ///< The previous camera's world position.
};

/// Builds the sky uniforms; every field is copied and the paddings are zero.
scene_detail::SkyUniforms buildSkyUniforms(const SkyUniformInputs& inputs);

} // namespace lmx::render
