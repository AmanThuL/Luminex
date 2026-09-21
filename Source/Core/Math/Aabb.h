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

/// Returns bounds with no extent, ready to accumulate points with expand().
inline Aabb emptyAabb() {
    return Aabb{glm::vec3(std::numeric_limits<float>::max()),
                glm::vec3(std::numeric_limits<float>::lowest())};
}

/// Grows bounds to include a point, component-wise.
inline void expand(Aabb& bounds, const glm::vec3& point) {
    bounds.minimum = glm::min(bounds.minimum, point);
    bounds.maximum = glm::max(bounds.maximum, point);
}

/// Returns the midpoint of minimum and maximum.
inline glm::vec3 center(const Aabb& bounds) {
    return (bounds.minimum + bounds.maximum) * 0.5f;
}

/// Returns the squared distance from a point to the nearest point on bounds; zero when inside.
inline float distanceSquared(const Aabb& bounds, const glm::vec3& point) {
    float delta[3];
    for (unsigned axis = 0; axis < 3; ++axis) {
        const int a = int(axis);
        delta[axis] = 0.0f;
        if (point[a] < bounds.minimum[a]) {
            delta[axis] = bounds.minimum[a] - point[a];
        } else if (point[a] > bounds.maximum[a]) {
            delta[axis] = point[a] - bounds.maximum[a];
        }
    }
    const float x = delta[0] * delta[0];
    const float y = delta[1] * delta[1];
    const float z = delta[2] * delta[2];
    const float xy = x + y;
    return xy + z;
}

} // namespace lmx
