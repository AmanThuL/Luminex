//----------------------------------------------------------------------------------------------------------------------
/// @file SceneStageInternal.h
/// @brief Shares scene shader mirror layouts and binding slots across stage units.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Passes/Scene/SceneFrameData.h"

#include <cstdint>

namespace lmx::render::scene_detail {

constexpr uint32_t kVertexBufferSlot = 0;
constexpr uint32_t kPassUniformsSlot = 2;
// The persistent exposure buffer (spec 9), read by ScenePassAuto.slang/SkyAuto.slang's fragment
// shaders -- the pipelines SceneStage selects only while auto-exposure is on. Bound via
// bindBuffer (a plain buffer read, not a storage binding) so a *raster* pass may read it --
// bindStorageBuffer is compute-pass-only.
constexpr uint32_t kExposureOverrideSlot = 3;
// The scene pass's texture slot map, which Shaders/Passes/Scene/ScenePass.slang's header documents
// in full. Two groups share one index space: a per-draw material set rebound for every DrawItem,
// and a per-pass shared set bound once before the draw loop.
//
// Slot 2 is the sky cubemap. Only Shaders/Passes/Scene/Sky.slang reads it, and the sky draws at the
// end of this same render pass, so it is bound beside that draw rather than with the shared set --
// the scene fragment stopped sampling the sky when the prefiltered environment (slot 8) replaced
// its ad-hoc mirror reflection.
constexpr uint32_t kDiffuseTextureSlot = 0;
constexpr uint32_t kNormalTextureSlot = 1;
constexpr uint32_t kSkyTextureSlot = 2;
constexpr uint32_t kShadowTextureSlot = 3;
constexpr uint32_t kMetallicRoughnessTextureSlot = 4;
constexpr uint32_t kOcclusionTextureSlot = 5;
constexpr uint32_t kEmissiveTextureSlot = 6;
constexpr uint32_t kIrradianceTextureSlot = 7;
constexpr uint32_t kPrefilteredEnvTextureSlot = 8;
constexpr uint32_t kDfgLutTextureSlot = 9;
constexpr uint32_t kLinearSamplerSlot = 0;
constexpr uint32_t kShadowSamplerSlot = 1;
constexpr uint32_t kIblSamplerSlot = 2;

} // namespace lmx::render::scene_detail
