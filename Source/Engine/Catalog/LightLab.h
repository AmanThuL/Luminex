//----------------------------------------------------------------------------------------------------------------------
/// @file LightLab.h
/// @brief Declares LightLab's device-free light and orbit-track generation.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Asset/Model/SceneAnimation.h"
#include "Engine/Types/LocalLight.h"

#include <cstdint>
#include <vector>

namespace lmx::engine {

/// Lights-per-froxel reference count: `lightLabRange` returns `kLightLabReferenceRange` at exactly
/// this population, and scales as `1/sqrt(n)` away from it so density (lights per unit footprint
/// area times range squared) stays constant across every requested `n`.
constexpr uint32_t kLightLabReferenceLightCount = 256;

/// `lightLabRange(kLightLabReferenceLightCount)`, metres. Chosen so neighbouring lights on the
/// reference grid's spacing (`2 * kLightLabGridHalfExtent / sqrt(kLightLabReferenceLightCount)`)
/// overlap without one light's range spanning the whole field.
constexpr float kLightLabReferenceRange = 5.0f;

/// Half the side length, metres, of the fixed square footprint the light grid occupies for every
/// `n`; only spacing between lights shrinks as `n` grows.
constexpr float kLightLabGridHalfExtent = 20.0f;

/// Seconds per revolution given to every orbiting light; divides the 12 s camera-rail loop so the
/// scene's motion repeats exactly with the rail.
constexpr float kLightLabOrbitPeriod = 6.0f;

/// Camera-rail loop length, seconds, matching the lab's other procedural scenes.
constexpr double kLightLabRailDuration = 12.0;

/// Vertical field of view, radians, shared by the authored camera and device-free lab checks.
constexpr float kLightLabCameraFovY = 50.0f * 3.14159265358979323846f / 180.0f;

/// Camera near-plane distance, metres, shared by the scene and cluster reference checks.
constexpr float kLightLabCameraNearZ = 0.1f;

/// Builds the lab's 12-second camera rail at the animation bake rate. The first and last keys
/// provide a field overview; its midpoint passes near the overflow pile. No device is required.
std::vector<asset::CameraKey> lightLabCameraTrack();

/// Returns the world-space point every `--lab-light-pile` light shares: near the centre of the
/// camera rail's view, so a saturated froxel is visible across the loop.
glm::vec3 lightLabPilePosition();

/// The per-light range at population `n`, scaled by `1/sqrt(n)` from the reference population and
/// range above so lights-per-unit-area times range^2 is constant. `n` must be at least 1.
float lightLabRange(uint32_t n);

/// Builds `n` deterministic lab lights on a jittered grid over `kLightLabGridHalfExtent`'s fixed
/// footprint, with heights between 0.35 and 0.65 times their range so every light illuminates
/// the floor even at the largest population; plus `pile` extra point lights stacked at
/// `lightLabPilePosition()`, in a fixed creation order: the `n` grid lights first (so
/// `LightOrbitTrack::light` indices from `lightLabTracks` stay valid), then the `pile` pile lights.
/// One in four grid lights (index % 4
/// == 1) is a spot pointing down with a deterministic tilt; the rest, including every pile light,
/// are points. Two calls at the same `(n, pile)` produce identical fields and byte-identical packed
/// LightRows: the seed, palette and derived values are fixed functions of the light's index.
/// `n` must be at least 1 and `n + pile` must not exceed `render::kMaxLocalLights`.
std::vector<engine::LocalLight> lightLabLights(uint32_t n, uint32_t pile);

/// Builds the closed-form orbit tracks for `lightLabLights(n, pile)`'s orbiting grid lights (index
/// % 4 == 2): `LightOrbitTrack::light` is that light's position in the vector `lightLabLights`
/// returns, so the pair must be called with the same `(n, pile)`. Pile lights never orbit. Every
/// returned track's `period` is `kLightLabOrbitPeriod`, which divides `kLightLabRailDuration`.
std::vector<asset::LightOrbitTrack> lightLabTracks(uint32_t n, uint32_t pile);

} // namespace lmx::engine
