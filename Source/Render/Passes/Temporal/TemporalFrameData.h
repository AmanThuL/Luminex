//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalFrameData.h
/// @brief Declares temporal kernel parameters and the pure functions that build them.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Passes/Temporal/Temporal.h"

#include <cstddef>
#include <cstdint>

namespace lmx::render::temporal_detail {

/// Mirrors Shaders/Passes/Temporal/TemporalResolve.slang's TemporalResolveParams.
struct TemporalResolveParams {
    uint32_t width = 0;                     ///< Output width, pixels.
    uint32_t height = 0;                    ///< Output height, pixels.
    uint32_t historyValid = 0;              ///< One when previous history may be reprojected.
    uint32_t writeDiagnostics = 0;          ///< Bit 0: rejection; bit 1: reprojected history.
    glm::mat4 inverseViewProjection{1.0f};  ///< This frame's unjittered clip-to-world matrix.
    glm::mat4 previousViewProjection{1.0f}; ///< Previous frame's unjittered world-to-clip matrix.
    float previousNearZ = 0.0f;             ///< Previous positive near-plane distance, view units.
    float pad[3] = {0.0f, 0.0f, 0.0f};      ///< Zero tail completing the 160-byte shader block.
};
static_assert(sizeof(TemporalResolveParams) == 160,
              "must match TemporalResolve.slang's TemporalResolveParams");

/// Mirrors SpatialUpscale.slang's active rectangle, output capacity and texture-space jitter.
struct SpatialUpscaleParams {
    uint32_t renderWidth = 0;     ///< Active render width, pixels.
    uint32_t renderHeight = 0;    ///< Active render height, pixels.
    uint32_t outputWidth = 0;     ///< Dispatch width, pixels.
    uint32_t outputHeight = 0;    ///< Dispatch height, pixels.
    uint32_t allocatedWidth = 0;  ///< Input allocation width, pixels.
    uint32_t allocatedHeight = 0; ///< Input allocation height, pixels.
    glm::vec2 jitterOffset{0.0f}; ///< Raster offset in render texels, with texture-space +y down.
};
static_assert(sizeof(SpatialUpscaleParams) == 32,
              "must match SpatialUpscale.slang's SpatialUpscaleParams");

/// Mirrors TemporalUpscale.slang's native resolve prefix followed by resampling facts.
struct TemporalUpscaleParams {
    uint32_t width = 0;                     ///< Active render width, pixels.
    uint32_t height = 0;                    ///< Active render height, pixels.
    uint32_t historyValid = 0;              ///< One when previous history may be reprojected.
    uint32_t writeDiagnostics = 0;          ///< Bit 0: rejection; bit 1: reprojected history.
    glm::mat4 inverseViewProjection{1.0f};  ///< This frame's unjittered clip-to-world matrix.
    glm::mat4 previousViewProjection{1.0f}; ///< Previous frame's unjittered world-to-clip matrix.
    float previousNearZ = 0.0f;             ///< Previous positive near-plane distance, view units.
    float pad[3] = {0.0f, 0.0f, 0.0f};      ///< Zero tail preserving the native block's layout.
    uint32_t outputWidth = 0;               ///< Dispatch width, pixels.
    uint32_t outputHeight = 0;              ///< Dispatch height, pixels.
    uint32_t allocatedWidth = 0;            ///< Render-input allocation width, pixels.
    uint32_t allocatedHeight = 0;           ///< Render-input allocation height, pixels.
    uint32_t previousRenderWidth = 0;       ///< Previous depth's active width, pixels.
    uint32_t previousRenderHeight = 0;      ///< Previous depth's active height, pixels.
    glm::vec2 jitterOffset{0.0f}; ///< Raster offset in render texels, texture-space +y down.
};
static_assert(sizeof(TemporalUpscaleParams) == 192,
              "must match TemporalUpscale.slang's TemporalUpscaleParams");

/// Mirrors TemporalReproject.slang's output dispatch and active input rectangle.
struct TemporalReprojectParams {
    uint32_t width = 0;           ///< Output dispatch width, pixels.
    uint32_t height = 0;          ///< Output dispatch height, pixels.
    uint32_t renderWidth = 0;     ///< Scene colour and motion active width, pixels.
    uint32_t renderHeight = 0;    ///< Scene colour and motion active height, pixels.
    uint32_t allocatedWidth = 0;  ///< Scene colour and motion allocation width, pixels.
    uint32_t allocatedHeight = 0; ///< Scene colour and motion allocation height, pixels.
    glm::vec2 jitterOffset{0.0f}; ///< Raster offset in render texels, texture-space +y down.
};
static_assert(sizeof(TemporalReprojectParams) == 32,
              "must match TemporalReproject.slang's TemporalReprojectParams");

/// Mirrors TemporalDebugView.slang's selector and source pixel extents.
struct TemporalDebugViewParams {
    uint32_t view = 0;         ///< Shader selector; MotionVectors and Off both map to one.
    uint32_t renderWidth = 0;  ///< Motion target's active width, pixels.
    uint32_t renderHeight = 0; ///< Motion target's active height, pixels.
    uint32_t outputWidth = 0;  ///< Every other input's width, pixels.
    uint32_t outputHeight = 0; ///< Every other input's height, pixels.
};
static_assert(sizeof(TemporalDebugViewParams) == 20,
              "must match TemporalDebugView.slang's TemporalDebugViewParams");

/// Mirrors VendorTemporalPack.slang's active input extent.
struct VendorPackParams {
    uint32_t width;  ///< Active render width, pixels.
    uint32_t height; ///< Active render height, pixels.
};
static_assert(sizeof(VendorPackParams) == 8);
static_assert(offsetof(VendorPackParams, height) == 4);

} // namespace lmx::render::temporal_detail

namespace lmx::render {

/// Plain frame facts consumed by the native accumulation kernel.
struct TemporalResolveParamsInputs {
    FrameExtents extents;            ///< Native resolve uses the output pixel extent.
    CameraFrameState camera;         ///< This frame's camera, with an unjittered inverse matrix.
    CameraFrameState previousCamera; ///< Previous frame's unjittered matrix and near plane.
    bool historyValid;               ///< False when the caller derived any history reset reason.
    bool rejectionWanted;            ///< Enable the rejection diagnostic write.
    bool reprojectedWanted;          ///< Enable the reprojected-history diagnostic write.
};

/// Builds the native shader block, converting historyValid to 0/1 and keeping padding zero.
temporal_detail::TemporalResolveParams
buildTemporalResolveParams(const TemporalResolveParamsInputs& inputs);

/// Plain frame facts consumed by the temporal upscaling kernel.
struct TemporalUpscaleParamsInputs {
    FrameExtents extents;         ///< Active render rectangle and output allocation, pixels.
    FrameExtents previousExtents; ///< Previous depth slot's active rectangle, pixels.
    CameraFrameState camera;      ///< Current unjittered inverse and raster jitter, render pixels.
    CameraFrameState previousCamera; ///< Previous unjittered projection and near plane.
    bool historyValid;               ///< False when the caller derived any history reset reason.
    bool rejectionWanted;            ///< Enable the rejection diagnostic write.
    bool reprojectedWanted;          ///< Enable the reprojected-history diagnostic write.
};

/// Builds the upscale block; input allocations equal output extent and jitter flips to +y down.
temporal_detail::TemporalUpscaleParams
buildTemporalUpscaleParams(const TemporalUpscaleParamsInputs& inputs);

/// Builds spatial resampling facts from pixel extents and raster jitter in render pixels.
/// Inputs are allocated at output capacity; jitter is converted to texture-space +y down.
temporal_detail::SpatialUpscaleParams buildSpatialUpscaleParams(const FrameExtents& extents,
                                                                glm::vec2 jitterPixels);

/// Builds reprojection facts, dispatching over output pixels and reading the active render extent.
/// Jitter is supplied in render pixels and converted to texture-space +y down.
temporal_detail::TemporalReprojectParams buildTemporalReprojectParams(const FrameExtents& extents,
                                                                      glm::vec2 jitterPixels);

/// Builds the display selector and pixel extents. Off maps to MotionVectors' selector, one,
/// because Off never declares the pass and the shader has no separate Off branch.
temporal_detail::TemporalDebugViewParams buildTemporalDebugViewParams(TemporalDebugView view,
                                                                      const FrameExtents& extents);

/// Builds the packing kernel's active render extent in pixels; packing policy stays in the shader.
temporal_detail::VendorPackParams buildVendorPackParams(const FrameExtents& extents);

} // namespace lmx::render
