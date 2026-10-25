#pragma once

#include <rojoRHI/RHI.h>

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <cstdint>

namespace lmx::test {

struct alignas(16) ShadowSmokeRoot {
    rojoRHI::Addr vertices{};
    rojoRHI::Addr uniforms{};
    uint32_t shadow{};
    uint32_t sampler{};
    uint32_t padding0{};
    uint32_t padding1{};
};

struct ShadowSmokeUniforms {
    glm::mat4 lightViewProj;
    glm::mat4 shadowTransform;
    int32_t filter = 0;
    int32_t padding0 = 0;
    int32_t padding1 = 0;
    int32_t padding2 = 0;
};

static_assert(alignof(ShadowSmokeRoot) == 16);
static_assert(sizeof(ShadowSmokeRoot) == 32);
static_assert(offsetof(ShadowSmokeRoot, vertices) == 0);
static_assert(offsetof(ShadowSmokeRoot, uniforms) == 8);
static_assert(offsetof(ShadowSmokeRoot, shadow) == 16);
static_assert(offsetof(ShadowSmokeRoot, sampler) == 20);
static_assert(offsetof(ShadowSmokeRoot, padding0) == 24);
static_assert(offsetof(ShadowSmokeRoot, padding1) == 28);
static_assert(sizeof(ShadowSmokeUniforms) == 144);
static_assert(offsetof(ShadowSmokeUniforms, lightViewProj) == 0);
static_assert(offsetof(ShadowSmokeUniforms, shadowTransform) == 64);
static_assert(offsetof(ShadowSmokeUniforms, filter) == 128);
static_assert(offsetof(ShadowSmokeUniforms, padding0) == 132);
static_assert(offsetof(ShadowSmokeUniforms, padding1) == 136);
static_assert(offsetof(ShadowSmokeUniforms, padding2) == 140);

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
