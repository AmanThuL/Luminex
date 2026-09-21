//----------------------------------------------------------------------------------------------------------------------
/// @file Frustum.h
/// @brief Declares five-plane frustum extraction and conservative box rejection against one plane.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Math/Aabb.h"

#include <glm/glm.hpp>

#include <array>
#include <cmath>

namespace lmx {

/// Five inward normalized half-spaces; a reversed infinite projection has no far plane.
struct Frustum {
    std::array<glm::vec4, 5> planes{}; ///< Left, right, bottom, top, near; dot(n,p)+w >= 0 inside.
    bool valid = false;                ///< False for a nonfinite or degenerate matrix.
};

/// Extracts the left, right, bottom, top and near planes of a view-projection matrix, normalized
/// and pushed outward by guardDistance world units; a nonfinite matrix or plane leaves it invalid.
inline Frustum extractFrustum(const glm::mat4& viewProjection, float guardDistance) {
    Frustum result;
    if (!isFinite(viewProjection))
        return result;
    const auto rows = glm::transpose(viewProjection);
    result.planes = {rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1], rows[3] - rows[1],
                     rows[3] - rows[2]};
    for (auto& plane : result.planes) {
        const float length = glm::length(glm::vec3(plane));
        if (!std::isfinite(length) || length <= 0)
            return result;
        plane /= length;
        plane.w += guardDistance;
        if (!std::isfinite(plane.w))
            return result;
    }
    result.valid = true;
    return result;
}

/// Returns whether the box lies wholly outside the plane: its most positive corner along the
/// normal is behind it. Sums x, then y, then z, then the distance, in that order.
inline bool planeRejects(const glm::vec4& plane, const Aabb& bounds) {
    const glm::vec3 positive{plane.x >= 0 ? bounds.maximum.x : bounds.minimum.x,
                             plane.y >= 0 ? bounds.maximum.y : bounds.minimum.y,
                             plane.z >= 0 ? bounds.maximum.z : bounds.minimum.z};
    const float x = plane.x * positive.x;
    const float y = plane.y * positive.y;
    const float z = plane.z * positive.z;
    const float xy = x + y;
    const float xyz = xy + z;
    return xyz + plane.w < 0;
}

} // namespace lmx
