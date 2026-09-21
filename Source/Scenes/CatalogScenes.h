//----------------------------------------------------------------------------------------------------------------------
/// @file CatalogScenes.h
/// @brief Declares the catalog's scene loaders.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Asset.h"
#include "Engine/Scene/Scene.h"
#include <rojoRHI/RHI.h>

#include <cstdint>
#include <memory>

namespace lmx::engine {

/// Crytek Sponza from the McGuire Computer Graphics Archive. `xmake setup` converts the pinned OBJ
/// archive to core glTF; camera and bounding sphere are computed from the loaded AABB.
asset::AssetResult<std::unique_ptr<Scene>> loadSponzaScene(rojoRHI::Device& device);

/// Khronos' DamagedHelmet sample (Assets/Fetched/DamagedHelmet, fetched by `xmake setup`). No
/// floor -- a model showcase, floating near the origin.
asset::AssetResult<std::unique_ptr<Scene>> loadHelmetScene(rojoRHI::Device& device);

/// Deterministic code-generated diagnostics: a material sweep sphere grid plus horizontal color,
/// texture, normal, and depth lanes. A fetched studio HDRI upgrades its lighting, with a neutral
/// deterministic fallback that keeps the scene always available.
asset::AssetResult<std::unique_ptr<Scene>> loadMaterialLabScene(rojoRHI::Device& device);

/// Khronos' CesiumMilkTruck sample (Assets/Fetched/CesiumMilkTruck, fetched by `xmake setup`).
/// Its wheel clip loads as rigid tracks, so the truck is the fetched rigid-motion reference;
/// camera and bounding sphere are computed from the loaded AABB.
asset::AssetResult<std::unique_ptr<Scene>> loadMilkTruckScene(rojoRHI::Device& device);

/// Deterministic code-generated temporal diagnostics: a checkerboard floor under a rotating cube,
/// a sphere orbiting a static reference cube, a row of oscillating poles, and one cube flagged
/// `engine::MotionClass::Invalid`, all driven by looping tracks alongside a looping camera track.
asset::AssetResult<std::unique_ptr<Scene>> loadTemporalLabScene(rojoRHI::Device& device);

/// San Miguel's pinned realtime variant with masked foliage and a looping camera rail. Optional
/// assets are fetched by `xmake setup --san-miguel`; missing assets return NotFound with that hint.
asset::AssetResult<std::unique_ptr<Scene>> loadSanMiguelScene(rojoRHI::Device& device);

/// Builds a seeded repeated-geometry visibility lab with exactly instanceCount candidates.
/// Counts from 1 through 1,048,576 include up to five initial-camera boundary probes.
/// Adds occluderCount slabs (0..1,024) with wide gaps; zero preserves the original scene bytes.
asset::AssetResult<std::unique_ptr<Scene>> loadVisibilityLabScene(rojoRHI::Device& device,
                                                                  uint32_t instanceCount = 4096,
                                                                  uint32_t occluderCount = 0);

/// Builds a deterministic material field (matte floor, pillar/sphere sweep) under lightCount local
/// lights (1..engine::kMaxLocalLights) on a jittered grid whose range scales with
/// 1/sqrt(lightCount), plus pileCount extra lights (default 0) stacked at one point; lightCount +
/// pileCount must not exceed engine::kMaxLocalLights. See LightLab.h for the device-free
/// generation this wraps.
asset::AssetResult<std::unique_ptr<Scene>>
loadLightLabScene(rojoRHI::Device& device, uint32_t lightCount = 256, uint32_t pileCount = 0);

} // namespace lmx::engine
