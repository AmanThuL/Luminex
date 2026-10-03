//----------------------------------------------------------------------------------------------------------------------
/// @file CatalogScenes.h
/// @brief Declares append-only lab generators over one unfinished document scene.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Engine/Scene/SceneInstantiate.h"
namespace lmx::scenes {
/// Appends deterministic visibility instances and optional occluders; validates population bounds.
asset::AssetResult<void> appendVisibilityLab(rojoRHI::Device& device, engine::Scene& scene,
                                             uint32_t instances, uint32_t occluders,
                                             const engine::EnvironmentHook& environment);
/// Appends scalable lights, pile and rebased orbits, retaining the document's saved material field.
asset::AssetResult<void> appendLightLab(engine::Scene& scene, uint32_t lights, uint32_t pile,
                                        const engine::EnvironmentHook& environment);
} // namespace lmx::scenes
