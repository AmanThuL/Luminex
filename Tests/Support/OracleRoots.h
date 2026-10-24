#pragma once

#include <rojoRHI/RHI.h>

#include <cstddef>
#include <cstdint>

namespace lmx::test {

struct BufferHazardRoot {
    rojoRHI::Addr output;
    rojoRHI::Addr source;
    uint32_t bias;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};

struct ComputeImageRoot {
    uint32_t image;
    uint32_t source;
    uint32_t extent;
    uint32_t padding = 0;
};

struct FullscreenSampleRoot {
    uint32_t source;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};

struct SamplerRoot {
    uint32_t source;
    uint32_t sampler;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
};

struct VisibilityDepthRoot {
    rojoRHI::Addr output;
    uint32_t depth;
    uint32_t padding = 0;
};

static_assert(sizeof(BufferHazardRoot) == 32 && alignof(BufferHazardRoot) == 8);
static_assert(offsetof(BufferHazardRoot, output) == 0);
static_assert(offsetof(BufferHazardRoot, source) == 8);
static_assert(offsetof(BufferHazardRoot, bias) == 16);
static_assert(offsetof(BufferHazardRoot, padding0) == 20);
static_assert(offsetof(BufferHazardRoot, padding1) == 24);
static_assert(offsetof(BufferHazardRoot, padding2) == 28);
static_assert(sizeof(ComputeImageRoot) == 16 && alignof(ComputeImageRoot) == 4);
static_assert(offsetof(ComputeImageRoot, image) == 0);
static_assert(offsetof(ComputeImageRoot, source) == 4);
static_assert(offsetof(ComputeImageRoot, extent) == 8);
static_assert(offsetof(ComputeImageRoot, padding) == 12);
static_assert(sizeof(FullscreenSampleRoot) == 16 && alignof(FullscreenSampleRoot) == 4);
static_assert(offsetof(FullscreenSampleRoot, source) == 0);
static_assert(offsetof(FullscreenSampleRoot, padding0) == 4);
static_assert(offsetof(FullscreenSampleRoot, padding1) == 8);
static_assert(offsetof(FullscreenSampleRoot, padding2) == 12);
static_assert(sizeof(SamplerRoot) == 16 && alignof(SamplerRoot) == 4);
static_assert(offsetof(SamplerRoot, source) == 0);
static_assert(offsetof(SamplerRoot, sampler) == 4);
static_assert(offsetof(SamplerRoot, padding0) == 8);
static_assert(offsetof(SamplerRoot, padding1) == 12);
static_assert(sizeof(VisibilityDepthRoot) == 16 && alignof(VisibilityDepthRoot) == 8);
static_assert(offsetof(VisibilityDepthRoot, output) == 0);
static_assert(offsetof(VisibilityDepthRoot, depth) == 8);
static_assert(offsetof(VisibilityDepthRoot, padding) == 12);

} // namespace lmx::test
