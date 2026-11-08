//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStageInternal.h
/// @brief Checks the pointed scene and sky payload layouts shared by stage units.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Passes/Scene/SceneFrameData.h"

#include <cstddef>

namespace lmx::render::scene_detail {
static_assert(offsetof(DirLightUniform, strength) == 0);
static_assert(offsetof(DirLightUniform, strengthPadding) == 12);
static_assert(offsetof(DirLightUniform, direction) == 16);
static_assert(offsetof(DirLightUniform, directionPadding) == 28);
static_assert(offsetof(PassUniforms, viewProj) == 0);
static_assert(offsetof(PassUniforms, shadowTransform) == 64);
static_assert(offsetof(PassUniforms, eyePos) == 128);
static_assert(offsetof(PassUniforms, eyePadding) == 140);
static_assert(offsetof(PassUniforms, time) == 144);
static_assert(offsetof(PassUniforms, preExposure) == 148);
static_assert(offsetof(PassUniforms, alignmentPadding) == 152);
static_assert(offsetof(PassUniforms, lights) == 160);
static_assert(offsetof(PassUniforms, shadowFilter) == 256);
static_assert(offsetof(PassUniforms, shadowCaster) == 260);
static_assert(offsetof(PassUniforms, tailPadding) == 264);
static_assert(offsetof(PassUniforms, viewProjUnjittered) == 272);
static_assert(offsetof(PassUniforms, previousViewProjUnjittered) == 336);
static_assert(offsetof(SkyUniforms, viewProj) == 0);
static_assert(offsetof(SkyUniforms, eyePos) == 64);
static_assert(offsetof(SkyUniforms, eyePadding) == 76);
static_assert(offsetof(SkyUniforms, preExposure) == 80);
static_assert(offsetof(SkyUniforms, jitterNdcX) == 84);
static_assert(offsetof(SkyUniforms, jitterNdcY) == 88);
static_assert(offsetof(SkyUniforms, jitterPadding) == 92);
static_assert(offsetof(SkyUniforms, previousViewProj) == 96);
static_assert(offsetof(SkyUniforms, previousEyePos) == 160);
static_assert(offsetof(SkyUniforms, previousEyePosPadding) == 172);

} // namespace lmx::render::scene_detail
