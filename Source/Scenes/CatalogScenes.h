//----------------------------------------------------------------------------------------------------------------------
/// @file CatalogScenes.h
/// @brief Declares append-only lab generators over one unfinished document scene.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Engine/Scene/SceneInstantiate.h"
namespace lmx::scenes {
/// Appends the material diagnostics, then calls the document environment hook.
asset::AssetResult<void> appendMaterialLab(rojoRHI::Device& device, engine::Scene& scene,
                                           const engine::EnvironmentHook& environment,
                                           bool axisStation = false);
/// Appends temporal geometry and rigid/emissive tracks, retaining previous content and clock.
asset::AssetResult<void> appendTemporalLab(rojoRHI::Device& device, engine::Scene& scene,
                                           const engine::EnvironmentHook& environment);
/// Appends deterministic visibility instances and optional occluders; validates population bounds.
asset::AssetResult<void> appendVisibilityLab(rojoRHI::Device& device, engine::Scene& scene,
                                             uint32_t instances, uint32_t occluders,
                                             const engine::EnvironmentHook& environment);
/// Appends the light field and rebased orbits, then calls the document environment hook.
asset::AssetResult<void> appendLightLab(engine::Scene& scene, uint32_t lights, uint32_t pile,
                                        const engine::EnvironmentHook& environment);
} // namespace lmx::scenes
