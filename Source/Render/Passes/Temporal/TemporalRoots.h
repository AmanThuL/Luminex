//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalRoots.h
/// @brief Declares address roots for temporal reconstruction, diagnostics and vendor packing.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <rojoRHI/RHI.h>

#include <cstddef>
#include <cstdint>

namespace lmx::render {

/// Borrows reprojection parameters, the history, scene-colour and motion views, the diagnostic
/// storage view and the clamped sampler.
struct alignas(16) TemporalReprojectRoot {
    rojoRHI::Addr paramsAddr; ///< Frame-owned TemporalReprojectParams.
    uint32_t history;         ///< Previous colour slot's sampled view.
    uint32_t sceneColor;      ///< Jittered scene colour's sampled view.
    uint32_t motion;          ///< Motion target's sampled view.
    uint32_t diagnostic;      ///< Diagnostic transient's storage view.
    uint32_t sampler;         ///< Clamped linear sampler.
    uint32_t padding0 = 0;    ///< Explicit zero padding.
};

/// Borrows one accumulation's parameters, exposure pair, six sampled inputs, three storage outputs
/// and the clamped sampler. The native resolve and the upscaling kernel share it.
struct alignas(16) TemporalResolveRoot {
    rojoRHI::Addr paramsAddr;   ///< Frame-owned TemporalResolveParams or TemporalUpscaleParams.
    rojoRHI::Addr exposureAddr; ///< GPU-resident applied/previous float pair, read only.
    uint32_t sceneColor;        ///< Jittered scene colour's sampled view.
    uint32_t depth;             ///< This frame's depth slot's sampled view.
    uint32_t previousDepth;     ///< Previous depth slot's sampled view.
    uint32_t motion;            ///< Motion target's sampled view.
    uint32_t reactive;          ///< Reactive target's sampled view.
    uint32_t history;           ///< Previous colour slot's sampled view.
    uint32_t output;            ///< This frame's colour slot's storage view.
    uint32_t rejection;         ///< Rejection storage view, or the fallback's when not wanted.
    uint32_t reprojected;       ///< Reprojected storage view, or the fallback's when not wanted.
    uint32_t sampler;           ///< Clamped linear sampler.
    uint32_t padding0 = 0;      ///< Explicit zero padding.
    uint32_t padding1 = 0;      ///< Explicit zero padding.
};

/// Borrows spatial-upscale parameters, the scene-colour view, the colour slot's storage view and
/// the clamped sampler.
struct alignas(16) SpatialUpscaleRoot {
    rojoRHI::Addr paramsAddr; ///< Frame-owned SpatialUpscaleParams.
    uint32_t sceneColor;      ///< Jittered scene colour's sampled view.
    uint32_t output;          ///< This frame's colour slot's storage view.
    uint32_t sampler;         ///< Clamped linear sampler.
    uint32_t padding0 = 0;    ///< Explicit zero padding.
    uint32_t padding1 = 0;    ///< Explicit zero padding.
    uint32_t padding2 = 0;    ///< Explicit zero padding.
};

/// Borrows debug-view parameters and the five sampled views the view selector may read.
struct alignas(16) TemporalDebugViewRoot {
    rojoRHI::Addr paramsAddr; ///< Frame-owned TemporalDebugViewParams.
    uint32_t motion;          ///< Motion target's sampled view.
    uint32_t diagnostic;      ///< Reprojection diagnostic's view, or the fallback's.
    uint32_t rejection;       ///< Rejection transient's view, or the fallback's.
    uint32_t reprojected;     ///< Reprojected-history transient's view, or the fallback's.
    uint32_t resolved;        ///< This frame's colour slot's view, or the fallback's.
    uint32_t padding0 = 0;    ///< Explicit zero padding.
};

/// Borrows the vendor history diagnostic's parameters, exposure pair, four sampled inputs, the
/// reprojected storage view and the clamped sampler.
struct alignas(16) VendorTemporalHistoryRoot {
    rojoRHI::Addr paramsAddr;   ///< Frame-owned TemporalUpscaleParams.
    rojoRHI::Addr exposureAddr; ///< GPU-resident applied/previous float pair, read only.
    uint32_t depth;             ///< This frame's depth slot's sampled view.
    uint32_t previousDepth;     ///< Previous depth slot's sampled view.
    uint32_t motion;            ///< Motion target's sampled view.
    uint32_t history;           ///< Previous colour slot's sampled view.
    uint32_t reprojected;       ///< Reprojected-history transient's storage view.
    uint32_t sampler;           ///< Clamped linear sampler.
    uint32_t padding0 = 0;      ///< Explicit zero padding.
    uint32_t padding1 = 0;      ///< Explicit zero padding.
};

/// Borrows vendor packing's active extent, exposure pair, two sampled inputs and three storage
/// outputs.
struct alignas(16) VendorTemporalPackRoot {
    rojoRHI::Addr paramsAddr;   ///< Frame-owned VendorPackParams.
    rojoRHI::Addr exposureAddr; ///< GPU-resident applied/previous float pair, read only.
    uint32_t motion;            ///< Motion target's sampled view.
    uint32_t reactive;          ///< Reactive target's sampled view.
    uint32_t packedMotion;      ///< Packed-motion transient's storage view.
    uint32_t packedReactive;    ///< Packed-reactive transient's storage view.
    uint32_t exposureTexel;     ///< 1x1 inverse-exposure transient's storage view.
    uint32_t padding0 = 0;      ///< Explicit zero padding.
    uint32_t padding1 = 0;      ///< Explicit zero padding.
    uint32_t padding2 = 0;      ///< Explicit zero padding.
};

static_assert(sizeof(TemporalReprojectRoot) == 32);
static_assert(offsetof(TemporalReprojectRoot, paramsAddr) == 0);
static_assert(offsetof(TemporalReprojectRoot, history) == 8);
static_assert(offsetof(TemporalReprojectRoot, sceneColor) == 12);
static_assert(offsetof(TemporalReprojectRoot, motion) == 16);
static_assert(offsetof(TemporalReprojectRoot, diagnostic) == 20);
static_assert(offsetof(TemporalReprojectRoot, sampler) == 24);
static_assert(offsetof(TemporalReprojectRoot, padding0) == 28);
static_assert(sizeof(TemporalResolveRoot) == 64);
static_assert(offsetof(TemporalResolveRoot, paramsAddr) == 0);
static_assert(offsetof(TemporalResolveRoot, exposureAddr) == 8);
static_assert(offsetof(TemporalResolveRoot, sceneColor) == 16);
static_assert(offsetof(TemporalResolveRoot, depth) == 20);
static_assert(offsetof(TemporalResolveRoot, previousDepth) == 24);
static_assert(offsetof(TemporalResolveRoot, motion) == 28);
static_assert(offsetof(TemporalResolveRoot, reactive) == 32);
static_assert(offsetof(TemporalResolveRoot, history) == 36);
static_assert(offsetof(TemporalResolveRoot, output) == 40);
static_assert(offsetof(TemporalResolveRoot, rejection) == 44);
static_assert(offsetof(TemporalResolveRoot, reprojected) == 48);
static_assert(offsetof(TemporalResolveRoot, sampler) == 52);
static_assert(offsetof(TemporalResolveRoot, padding0) == 56);
static_assert(offsetof(TemporalResolveRoot, padding1) == 60);
static_assert(sizeof(SpatialUpscaleRoot) == 32);
static_assert(offsetof(SpatialUpscaleRoot, paramsAddr) == 0);
static_assert(offsetof(SpatialUpscaleRoot, sceneColor) == 8);
static_assert(offsetof(SpatialUpscaleRoot, output) == 12);
static_assert(offsetof(SpatialUpscaleRoot, sampler) == 16);
static_assert(offsetof(SpatialUpscaleRoot, padding0) == 20);
static_assert(offsetof(SpatialUpscaleRoot, padding1) == 24);
static_assert(offsetof(SpatialUpscaleRoot, padding2) == 28);
static_assert(sizeof(TemporalDebugViewRoot) == 32);
static_assert(offsetof(TemporalDebugViewRoot, paramsAddr) == 0);
static_assert(offsetof(TemporalDebugViewRoot, motion) == 8);
static_assert(offsetof(TemporalDebugViewRoot, diagnostic) == 12);
static_assert(offsetof(TemporalDebugViewRoot, rejection) == 16);
static_assert(offsetof(TemporalDebugViewRoot, reprojected) == 20);
static_assert(offsetof(TemporalDebugViewRoot, resolved) == 24);
static_assert(offsetof(TemporalDebugViewRoot, padding0) == 28);
static_assert(sizeof(VendorTemporalHistoryRoot) == 48);
static_assert(offsetof(VendorTemporalHistoryRoot, paramsAddr) == 0);
static_assert(offsetof(VendorTemporalHistoryRoot, exposureAddr) == 8);
static_assert(offsetof(VendorTemporalHistoryRoot, depth) == 16);
static_assert(offsetof(VendorTemporalHistoryRoot, previousDepth) == 20);
static_assert(offsetof(VendorTemporalHistoryRoot, motion) == 24);
static_assert(offsetof(VendorTemporalHistoryRoot, history) == 28);
static_assert(offsetof(VendorTemporalHistoryRoot, reprojected) == 32);
static_assert(offsetof(VendorTemporalHistoryRoot, sampler) == 36);
static_assert(offsetof(VendorTemporalHistoryRoot, padding0) == 40);
static_assert(offsetof(VendorTemporalHistoryRoot, padding1) == 44);
static_assert(sizeof(VendorTemporalPackRoot) == 48);
static_assert(offsetof(VendorTemporalPackRoot, paramsAddr) == 0);
static_assert(offsetof(VendorTemporalPackRoot, exposureAddr) == 8);
static_assert(offsetof(VendorTemporalPackRoot, motion) == 16);
static_assert(offsetof(VendorTemporalPackRoot, reactive) == 20);
static_assert(offsetof(VendorTemporalPackRoot, packedMotion) == 24);
static_assert(offsetof(VendorTemporalPackRoot, packedReactive) == 28);
static_assert(offsetof(VendorTemporalPackRoot, exposureTexel) == 32);
static_assert(offsetof(VendorTemporalPackRoot, padding0) == 36);
static_assert(offsetof(VendorTemporalPackRoot, padding1) == 40);
static_assert(offsetof(VendorTemporalPackRoot, padding2) == 44);

} // namespace lmx::render
