//----------------------------------------------------------------------------------------------------------------------
/// @file BloomRoots.h
/// @brief Declares address roots for bloom threshold and mip reconstruction.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <rojoRHI/RHI.h>

#include <cstddef>
#include <cstdint>

namespace lmx::render {

/// Borrows threshold parameters, sampled scene color and the destination storage view.
struct alignas(16) BloomThresholdRoot {
    rojoRHI::Addr paramsAddr; ///< Frame-owned BloomThresholdParams.
    uint32_t sceneColor;      ///< Sampled scene-color view.
    uint32_t destination;     ///< Mip-zero writable storage view.
};

/// Borrows one downsample step's parameters and exact source/destination mip views.
struct alignas(16) BloomDownsampleRoot {
    rojoRHI::Addr paramsAddr; ///< Frame-owned BloomDownsampleParams.
    uint32_t source;          ///< Larger mip's readable storage view.
    uint32_t destination;     ///< Smaller mip's writable storage view.
};

/// Borrows one reconstruction step's parameters and three exact storage views.
struct alignas(16) BloomUpsampleRoot {
    rojoRHI::Addr paramsAddr; ///< Frame-owned BloomUpsampleParams.
    uint32_t base;            ///< Larger downsample mip's readable storage view.
    uint32_t small;           ///< Smaller chain or accumulation mip's readable storage view.
    uint32_t destination;     ///< Larger accumulation mip's writable storage view.
    uint32_t padding0 = 0;    ///< Explicit zero padding.
    uint32_t padding1 = 0;    ///< Explicit zero padding.
    uint32_t padding2 = 0;    ///< Explicit zero padding.
};

static_assert(sizeof(BloomThresholdRoot) == 16);
static_assert(offsetof(BloomThresholdRoot, paramsAddr) == 0);
static_assert(offsetof(BloomThresholdRoot, sceneColor) == 8);
static_assert(offsetof(BloomThresholdRoot, destination) == 12);
static_assert(sizeof(BloomDownsampleRoot) == 16);
static_assert(offsetof(BloomDownsampleRoot, paramsAddr) == 0);
static_assert(offsetof(BloomDownsampleRoot, source) == 8);
static_assert(offsetof(BloomDownsampleRoot, destination) == 12);
static_assert(sizeof(BloomUpsampleRoot) == 32);
static_assert(offsetof(BloomUpsampleRoot, paramsAddr) == 0);
static_assert(offsetof(BloomUpsampleRoot, base) == 8);
static_assert(offsetof(BloomUpsampleRoot, small) == 12);
static_assert(offsetof(BloomUpsampleRoot, destination) == 16);
static_assert(offsetof(BloomUpsampleRoot, padding0) == 20);
static_assert(offsetof(BloomUpsampleRoot, padding1) == 24);
static_assert(offsetof(BloomUpsampleRoot, padding2) == 28);

} // namespace lmx::render
