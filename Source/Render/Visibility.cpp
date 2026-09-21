//----------------------------------------------------------------------------------------------------------------------
/// @file Visibility.cpp
/// @brief Implements five-plane conservative classification from canonical shared rows.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/Visibility.h"
#include "Core/Diagnostics/Assert.h"
#include "Render/Renderer/SceneView.h"

namespace lmx::render {

//======================================================================================================================
FrustumPlanes extractFrustumPlanes(const glm::mat4& matrix) {
    return extractFrustum(matrix, kVisibilityGuardWorldUnits);
}

//======================================================================================================================
InstanceVisibility classifyInstance(const FrustumPlanes& planes, const engine::InstanceRow& row,
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
    if (!planes.valid || (row.flags & engine::kInstanceBoundsUnreliable) ||
        !isValidAabb(result.worldBounds))
        return bypass(VisibilityReason::UnreliableBounds);
    for (const auto& plane : planes.planes) {
        if (planeRejects(plane, result.worldBounds)) {
            result.state = VisibilityState::Rejected;
            break;
        }
    }
    return result;
}

//======================================================================================================================
VisibilityResult classifyView(const FrustumPlanes& planes, std::span<const engine::DrawItem> items,
                              const engine::SceneTables& tables, bool enabled, bool viewUnculled) {
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
