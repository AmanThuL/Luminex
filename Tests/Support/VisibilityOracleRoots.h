#pragma once

#include <rojoRHI/RHI.h>

#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>

namespace lmx::test {

/// Carries the depth-fixture mask selector.
struct alignas(16) HzbDepthRoot {
    uint32_t masked = 0;   ///< Selects the depth fixture mask.
    uint32_t padding0 = 0; ///< Explicit zero padding.
    uint32_t padding1 = 0; ///< Explicit zero padding.
    uint32_t padding2 = 0; ///< Explicit zero padding.
};

static_assert(alignof(HzbDepthRoot) == 16);
static_assert(sizeof(HzbDepthRoot) == 16);
static_assert(offsetof(HzbDepthRoot, masked) == 0);
static_assert(offsetof(HzbDepthRoot, padding0) == 4);
static_assert(offsetof(HzbDepthRoot, padding1) == 8);
static_assert(offsetof(HzbDepthRoot, padding2) == 12);

/// Borrows readback constants, writable values and the source view.
struct alignas(16) HzbReadbackRoot {
    rojoRHI::Addr paramsAddr; ///< Borrowed params data.
    rojoRHI::Addr outputAddr; ///< Borrowed output data.
    uint32_t source = 0;      ///< Sampled source mip view.
    uint32_t padding0 = 0;    ///< Explicit zero padding.
    uint32_t padding1 = 0;    ///< Explicit zero padding.
    uint32_t padding2 = 0;    ///< Explicit zero padding.
};

static_assert(alignof(HzbReadbackRoot) == 16);
static_assert(sizeof(HzbReadbackRoot) == 32);
static_assert(offsetof(HzbReadbackRoot, paramsAddr) == 0);
static_assert(offsetof(HzbReadbackRoot, outputAddr) == 8);
static_assert(offsetof(HzbReadbackRoot, source) == 16);
static_assert(offsetof(HzbReadbackRoot, padding0) == 20);
static_assert(offsetof(HzbReadbackRoot, padding1) == 24);
static_assert(offsetof(HzbReadbackRoot, padding2) == 28);

/// Borrows probe bounds, writable observations and occlusion constants.
struct alignas(16) OcclusionProbeRoot {
    rojoRHI::Addr boundsAddr; ///< Borrowed bounds data.
    rojoRHI::Addr outputAddr; ///< Borrowed output data.
    rojoRHI::Addr paramsAddr; ///< Borrowed params data.
    uint32_t pyramid = 0;     ///< Sampled depth pyramid view.
    uint32_t padding = 0;     ///< Explicit zero padding.
};

static_assert(alignof(OcclusionProbeRoot) == 16);
static_assert(sizeof(OcclusionProbeRoot) == 32);
static_assert(offsetof(OcclusionProbeRoot, boundsAddr) == 0);
static_assert(offsetof(OcclusionProbeRoot, outputAddr) == 8);
static_assert(offsetof(OcclusionProbeRoot, paramsAddr) == 16);
static_assert(offsetof(OcclusionProbeRoot, pyramid) == 24);
static_assert(offsetof(OcclusionProbeRoot, padding) == 28);

/// Source dimensions, selected mip and destination offset for fixture readback.
struct ReadbackParams {
    uint32_t width;
    uint32_t height;
    uint32_t level;
    uint32_t offset;
};
static_assert(sizeof(ReadbackParams) == 16);
static_assert(offsetof(ReadbackParams, width) == 0);
static_assert(offsetof(ReadbackParams, height) == 4);
static_assert(offsetof(ReadbackParams, level) == 8);
static_assert(offsetof(ReadbackParams, offset) == 12);

/// Minimum and maximum corners consumed by the projection oracle.
struct ProbeBounds {
    glm::vec4 minimum;
    glm::vec4 maximum;
};
static_assert(sizeof(ProbeBounds) == 32);
static_assert(offsetof(ProbeBounds, minimum) == 0);
static_assert(offsetof(ProbeBounds, maximum) == 16);

using HzbReadParams = ReadbackParams;
using HzbProbeBounds = ProbeBounds;

} // namespace lmx::test
