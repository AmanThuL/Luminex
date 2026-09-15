//----------------------------------------------------------------------------------------------------------------------
/// @file Visibility.cpp
/// @brief Implements five-plane conservative classification from canonical shared rows.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Visibility.h"
#include "Core/Assert.h"
#include "Render/SceneView.h"
#include <cmath>

namespace lmx::render {

//======================================================================================================================
FrustumPlanes extractFrustumPlanes(const glm::mat4& matrix) {
    FrustumPlanes result;
    if (!isFinite(matrix))
        return result;
    const auto rows = glm::transpose(matrix);
    result.planes = {rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1], rows[3] - rows[1],
                     rows[3] - rows[2]};
    for (auto& plane : result.planes) {
        const float length = glm::length(glm::vec3(plane));
        if (!std::isfinite(length) || length <= 0)
            return result;
        plane /= length;
        plane.w += kVisibilityGuardWorldUnits;
        if (!std::isfinite(plane.w))
            return result;
    }
    result.valid = true;
    return result;
}

//======================================================================================================================
InstanceVisibility classifyInstance(const FrustumPlanes& planes, const InstanceRow& row,
                                    uint32_t instanceRow, bool enabled, bool viewUnculled) {
    InstanceVisibility result{.instanceRow = instanceRow,
                              .worldBounds = {row.worldBoundsMin, row.worldBoundsMax}};
    auto bypass = [&](VisibilityReason reason) {
        result.state = VisibilityState::Bypassed;
        result.reason = reason;
        return result;
    };
    if (!enabled)
        return bypass(VisibilityReason::Disabled);
    if (viewUnculled)
        return bypass(VisibilityReason::ViewUnculled);
    if (!isFinite(row.model))
        return bypass(VisibilityReason::NonFiniteTransform);
    if (!planes.valid || (row.flags & kInstanceBoundsUnreliable) ||
        !isValidAabb(result.worldBounds))
        return bypass(VisibilityReason::UnreliableBounds);
    for (const auto& plane : planes.planes) {
        const glm::vec3 positive{plane.x >= 0 ? row.worldBoundsMax.x : row.worldBoundsMin.x,
                                 plane.y >= 0 ? row.worldBoundsMax.y : row.worldBoundsMin.y,
                                 plane.z >= 0 ? row.worldBoundsMax.z : row.worldBoundsMin.z};
        const float x = plane.x * positive.x;
        const float y = plane.y * positive.y;
        const float z = plane.z * positive.z;
        const float xy = x + y;
        const float xyz = xy + z;
        if (xyz + plane.w < 0) {
            result.state = VisibilityState::Rejected;
            break;
        }
    }
    return result;
}

//======================================================================================================================
VisibilityResult classifyView(const FrustumPlanes& planes, std::span<const DrawItem> items,
                              const SceneTables& tables, bool enabled, bool viewUnculled) {
    VisibilityResult result;
    result.candidates.reserve(items.size());
    result.visibleItems.reserve(items.size());
    for (uint32_t index = 0; index < items.size(); ++index) {
        const uint32_t slot = items[index].instanceRow;
        LMX_ASSERT(slot < tables.instanceRows.size(),
                   "visibility requires canonical instance rows");
        const auto state =
            classifyInstance(planes, tables.instanceRows[slot], slot, enabled, viewUnculled);
        result.candidates.push_back(state);
        if (state.state == VisibilityState::Rejected)
            ++result.rejected;
        else {
            result.visibleItems.push_back(index);
            if (state.state == VisibilityState::Visible)
                ++result.visible;
            else
                ++result.bypassed[static_cast<size_t>(state.reason)];
        }
    }
    return result;
}
} // namespace lmx::render
