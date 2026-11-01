//----------------------------------------------------------------------------------------------------------------------
/// @file LightRoots.h
/// @brief Declares flat address roots for clustered lighting and its display diagnostics.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <rojoRHI/RHI.h>

#include <cstddef>
#include <cstdint>

namespace lmx::render {

/// Shared address transport for the light count, scan and fill kernels.
struct alignas(16) LightClusterRoot {
    rojoRHI::Addr params{};   ///< Uploaded kernel parameters.
    rojoRHI::Addr lights{};   ///< Addressable light rows.
    rojoRHI::Addr counts{};   ///< Per-froxel intersection counts.
    rojoRHI::Addr grid{};     ///< Per-froxel index-list ranges.
    rojoRHI::Addr indices{};  ///< Ascending light-row index lists.
    rojoRHI::Addr counters{}; ///< Assignment and overflow totals.
};

static_assert(alignof(LightClusterRoot) == 16);
static_assert(sizeof(LightClusterRoot) == 48);
static_assert(offsetof(LightClusterRoot, params) == 0);
static_assert(offsetof(LightClusterRoot, lights) == 8);
static_assert(offsetof(LightClusterRoot, counts) == 16);
static_assert(offsetof(LightClusterRoot, grid) == 24);
static_assert(offsetof(LightClusterRoot, indices) == 32);
static_assert(offsetof(LightClusterRoot, counters) == 40);

/// Light assignment and source-depth inputs for post-display diagnostics.
struct alignas(16) LightDebugRoot {
    rojoRHI::Addr params{};  ///< Uploaded diagnostic parameters.
    rojoRHI::Addr lights{};  ///< Addressable light rows.
    rojoRHI::Addr grid{};    ///< Per-froxel index-list ranges.
    rojoRHI::Addr indices{}; ///< Ascending light-row index lists.
    uint32_t depth{};        ///< Sampled scene-depth view.
    uint32_t display{};      ///< Sampled ordinary display view.
    uint32_t padding0{};     ///< Explicit zero padding.
    uint32_t padding1{};     ///< Explicit zero padding.
};

static_assert(alignof(LightDebugRoot) == 16);
static_assert(sizeof(LightDebugRoot) == 48);
static_assert(offsetof(LightDebugRoot, params) == 0);
static_assert(offsetof(LightDebugRoot, lights) == 8);
static_assert(offsetof(LightDebugRoot, grid) == 16);
static_assert(offsetof(LightDebugRoot, indices) == 24);
static_assert(offsetof(LightDebugRoot, depth) == 32);
static_assert(offsetof(LightDebugRoot, display) == 36);
static_assert(offsetof(LightDebugRoot, padding0) == 40);
static_assert(offsetof(LightDebugRoot, padding1) == 44);

} // namespace lmx::render
