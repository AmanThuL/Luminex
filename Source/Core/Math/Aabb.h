//----------------------------------------------------------------------------------------------------------------------
/// @file Aabb.h
/// @brief Declares shared finite axis-aligned bounds and conservative corner transforms.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <limits>
#include <optional>

namespace lmx {

/// Inclusive axis-aligned bounds in the coordinate space of their owner.
struct Aabb {
    glm::vec3 minimum{0.0f}; ///< Minimum XYZ coordinates.
    glm::vec3 maximum{0.0f}; ///< Maximum XYZ coordinates.
};

/// Returns whether every vector component is finite.
inline bool isFinite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

/// Returns whether all sixteen matrix components are finite.
inline bool isFinite(const glm::mat4& value) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(value[column][row])) {
                return false;
            }
        }
    }
    return true;
}

/// Accepts finite ordered bounds, including planar geometry and point-sized world boxes.
inline bool isValidAabb(const Aabb& bounds) {
    return isFinite(bounds.minimum) && isFinite(bounds.maximum) &&
           glm::all(glm::lessThanEqual(bounds.minimum, bounds.maximum));
}

/// Transforms all eight corners with an affine matrix; invalid or overflowing input returns none.
inline std::optional<Aabb> transformAabb(const glm::mat4& transform, const Aabb& local) {
    if (!isValidAabb(local) || !isFinite(transform)) {
        return std::nullopt;
    }
    Aabb world{glm::vec3(std::numeric_limits<float>::max()),
               glm::vec3(std::numeric_limits<float>::lowest())};
    for (unsigned corner = 0; corner < 8; ++corner) {
        const glm::vec3 point((corner & 1) ? local.maximum.x : local.minimum.x,
                              (corner & 2) ? local.maximum.y : local.minimum.y,
                              (corner & 4) ? local.maximum.z : local.minimum.z);
        const glm::vec3 transformed(transform * glm::vec4(point, 1.0f));
        if (!isFinite(transformed)) {
            return std::nullopt;
        }
        world.minimum = glm::min(world.minimum, transformed);
        world.maximum = glm::max(world.maximum, transformed);
    }
    return world;
}

} // namespace lmx
