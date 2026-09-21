//----------------------------------------------------------------------------------------------------------------------
/// @file Transform.h
/// @brief Declares shared translate-rotate-scale composition and validation.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/glm.hpp>

#include <optional>

namespace lmx {

/// What decomposeTransform below extracts from a general 4x4.
struct DecomposedTransform {
    glm::vec3 position{0.f};     ///< Extracted translation.
    glm::vec3 eulerDegrees{0.f}; ///< Extracted XYZ Euler rotation in degrees.
    glm::vec3 scale{1.f};        ///< Extracted per-axis scale.
};

/// Extracts translation, Euler rotation, and scale when `world` is decomposable, in the Y-X-Z
/// order `SceneObject::modelMatrix` composes them, so the two round-trip. The factorisation is
/// proved by recomposing it, so shear -- a non-orthogonal basis, which no translate-rotate-scale
/// chain can produce -- is rejected rather than silently orthogonalised, as is a projective row.
/// A single zero-scale axis is accepted, because a collapsed object is a legitimate authored pose;
/// two or more leave no rotation to extract and are rejected.
std::optional<DecomposedTransform> decomposeTransform(const glm::mat4& world);

/// Composes a pose in the Y-X-Z order shared by decoded clips and scene objects.
glm::mat4 composeTransform(const DecomposedTransform& transform);

} // namespace lmx
