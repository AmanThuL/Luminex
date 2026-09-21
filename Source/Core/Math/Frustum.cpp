//----------------------------------------------------------------------------------------------------------------------
/// @file Frustum.cpp
/// @brief Implements five-plane frustum extraction without floating-point contraction.
//----------------------------------------------------------------------------------------------------------------------

#include "Core/Math/Frustum.h"

#include <cmath>

namespace lmx {

//======================================================================================================================
Frustum extractFrustum(const glm::mat4& viewProjection, float guardDistance) {
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

} // namespace lmx
