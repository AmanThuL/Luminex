//----------------------------------------------------------------------------------------------------------------------
/// @file LocalLight.h
/// @brief Declares the CPU-authored point/spot light model docs/milestones/m7/m7.5.md defines.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace lmx::render {

/// Runtime local-light selection; all modes share the scene pipelines and one accumulation loop.
enum class LocalLightMode : uint32_t {
    Off,       ///< Visit no row; the local sum is exact zero.
    Direct,    ///< Visit every addressable row; the unbounded O(pixels x lights) reference.
    Clustered, ///< Visit the fragment's ascending froxel list.
};

/// Selects a post-display local-light diagnostic without changing temporal history.
enum class LightDebugView : uint8_t {
    Off,      ///< Final shaded image.
    Count,    ///< Froxel list occupancy heatmap.
    Overflow, ///< Froxels whose lists were truncated.
    Missed,   ///< Independent geometric reach oracle: red loss, yellow expected truncation.
};

/// Distinguishes a point light from a spot light without a virtual type; `LightRow` encodes both
/// with the same fields, a point light storing the spot's degenerate cone (scale 0, offset 1).
enum class LocalLightType : uint8_t {
    Point, ///< Radiates in every direction from `position`.
    Spot,  ///< Radiates within `outerCone` of `direction` from `position`.
};

/// One CPU-authored point or spot light. A point light of intensity 1 at distance 1 m and normal
/// incidence reproduces a directional light of strength `colour`; `range` is mandatory, finite and
/// positive, and `direction`/`innerCone`/`outerCone` matter only for `Spot`.
struct LocalLight {
    LocalLightType type = LocalLightType::Point; ///< Point or spot.
    glm::vec3 position{0.0f};                    ///< World-space origin, metres.
    glm::vec3 colour{1.0f};                      ///< Linear-RGB colour.
    float intensity = 1.0f;                      ///< Scalar multiplied onto `colour`.
    float range = 1.0f;                          ///< Metres; attenuation reaches exactly zero here.
    glm::vec3 direction{0.0f, 0.0f, -1.0f};      ///< Unit ray-travel direction; spots only.
    float innerCone = 0.0f; ///< Radians; full intensity within this half-angle.
    float outerCone = 0.0f; ///< Radians; zero intensity beyond this half-angle.
    /// Disabled lights retain their authored values and identity but upload an inert row.
    bool enabled = true;
};

} // namespace lmx::render
