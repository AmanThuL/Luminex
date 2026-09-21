//----------------------------------------------------------------------------------------------------------------------
/// @file DirectionalLight.h
/// @brief Declares the analytic directional light the scene passes shade with.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/vec3.hpp>

namespace lmx::engine {

/// Mirrors Lighting.slang's DirLight. `strength` is linear radiance, `direction` is the way the
/// rays travel (so a light overhead points down).
struct DirectionalLight {
    glm::vec3 strength{0.5f};               ///< Scene-linear RGB radiance.
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; ///< Direction rays travel in world space.
};

} // namespace lmx::engine
