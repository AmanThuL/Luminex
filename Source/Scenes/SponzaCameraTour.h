//----------------------------------------------------------------------------------------------------------------------
/// @file SponzaCameraTour.h
/// @brief Declares the two-storey Sponza corridor camera tour.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

namespace lmx::engine {
class Scene;
} // namespace lmx::engine

namespace lmx::scenes {

/// Complete lower/upper corridor tour period, seconds.
constexpr double kSponzaCameraTourDuration = 120.0;

/// Authors a closed, constant-distance-paced camera rail through both Sponza corridor levels and
/// the open atrium, at the animation bake rate. Sets the initial camera pose to its first key and
/// the scene animation duration/loop; preserves the camera lens and other animation tracks.
/// Coordinates match the repository's pinned, metre-scale Crytek Sponza asset. Requires no device.
void authorSponzaCameraTour(engine::Scene& scene);

} // namespace lmx::scenes
