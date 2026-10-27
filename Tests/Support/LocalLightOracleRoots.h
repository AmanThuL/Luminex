#pragma once

#include "Engine/Scene/SceneTables.h"

#include <rojoRHI/RHI.h>

#include <glm/vec3.hpp>

#include <cstddef>

namespace lmx::test {

struct alignas(16) PunctualLightRoot {
    rojoRHI::Addr samples{};
    rojoRHI::Addr output{};
};

static_assert(alignof(PunctualLightRoot) == 16);
static_assert(sizeof(PunctualLightRoot) == 16);
static_assert(offsetof(PunctualLightRoot, samples) == 0);
static_assert(offsetof(PunctualLightRoot, output) == 8);

// Each float3 is followed by its scalar, preserving the oracle's structured-buffer stride.
struct PunctualSample {
    engine::LightRow light;
    glm::vec3 position{0.0f};
    float pad0 = 0.0f;
    glm::vec3 normal{0.0f};
    float pad1 = 0.0f;
    glm::vec3 toEye{0.0f};
    float pad2 = 0.0f;
    glm::vec3 baseColour{0.0f};
    float metallic = 0.0f;
    glm::vec3 f0{0.0f};
    float alpha = 0.0f;
};

static_assert(sizeof(PunctualSample) == 144);
static_assert(offsetof(PunctualSample, light) == 0);
static_assert(offsetof(PunctualSample, position) == 64);
static_assert(offsetof(PunctualSample, pad0) == 76);
static_assert(offsetof(PunctualSample, normal) == 80);
static_assert(offsetof(PunctualSample, pad1) == 92);
static_assert(offsetof(PunctualSample, toEye) == 96);
static_assert(offsetof(PunctualSample, pad2) == 108);
static_assert(offsetof(PunctualSample, baseColour) == 112);
static_assert(offsetof(PunctualSample, metallic) == 124);
static_assert(offsetof(PunctualSample, f0) == 128);
static_assert(offsetof(PunctualSample, alpha) == 140);

} // namespace lmx::test
