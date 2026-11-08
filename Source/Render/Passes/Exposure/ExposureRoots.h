//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureRoots.h
/// @brief Declares address roots for exposure seeding, metering and feedback.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <rojoRHI/RHI.h>

#include <cstddef>
#include <cstdint>

namespace lmx::render {

/// Borrows the persistent exposure pair and this dispatch's seed parameters.
struct alignas(16) ExposureSeedRoot {
    rojoRHI::Addr exposureAddr; ///< GPU-resident applied/previous float pair.
    rojoRHI::Addr paramsAddr;   ///< Frame-owned ExposureSeedParams.
};

/// Borrows the metered scene view, exposure and histogram for one dispatch.
struct alignas(16) HistogramRoot {
    rojoRHI::Addr histogramAddr; ///< GPU-resident 256-word histogram.
    rojoRHI::Addr exposureAddr;  ///< Applied exposure at the first float.
    rojoRHI::Addr paramsAddr;    ///< Frame-owned HistogramParams.
    uint32_t sceneColor;         ///< Sampled scene-color view.
    uint32_t padding = 0;        ///< Explicit zero padding.
};

/// Borrows the histogram, exposure pair and this dispatch's resolve parameters.
struct alignas(16) ExposureResolveRoot {
    rojoRHI::Addr histogramAddr; ///< GPU-resident histogram read by resolve.
    rojoRHI::Addr exposureAddr;  ///< GPU-resident applied/previous float pair.
    rojoRHI::Addr paramsAddr;    ///< Frame-owned ExposureResolveParams.
    uint32_t padding0 = 0;       ///< Explicit zero padding.
    uint32_t padding1 = 0;       ///< Explicit zero padding.
};

static_assert(sizeof(ExposureSeedRoot) == 16);
static_assert(offsetof(ExposureSeedRoot, exposureAddr) == 0);
static_assert(offsetof(ExposureSeedRoot, paramsAddr) == 8);
static_assert(sizeof(HistogramRoot) == 32);
static_assert(offsetof(HistogramRoot, histogramAddr) == 0);
static_assert(offsetof(HistogramRoot, exposureAddr) == 8);
static_assert(offsetof(HistogramRoot, paramsAddr) == 16);
static_assert(offsetof(HistogramRoot, sceneColor) == 24);
static_assert(offsetof(HistogramRoot, padding) == 28);
static_assert(sizeof(ExposureResolveRoot) == 32);
static_assert(offsetof(ExposureResolveRoot, histogramAddr) == 0);
static_assert(offsetof(ExposureResolveRoot, exposureAddr) == 8);
static_assert(offsetof(ExposureResolveRoot, paramsAddr) == 16);
static_assert(offsetof(ExposureResolveRoot, padding0) == 24);
static_assert(offsetof(ExposureResolveRoot, padding1) == 28);

} // namespace lmx::render
