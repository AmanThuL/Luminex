//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionCheck.cpp
/// @brief Computes generation-safe per-instance missing-frame streaks.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/OcclusionCheck.h"
#include <algorithm>

namespace lmx::render {
//======================================================================================================================
OcclusionCheckResult
OcclusionCheckHistory::observe(uint64_t frameNumber, uint64_t sceneGeneration, bool strict,
                               std::span<const OcclusionCheckObservation> observations) {
    if (frameNumber != m_frameNumber + 1 || sceneGeneration != m_sceneGeneration)
        m_streaks.clear();
    OcclusionCheckResult result{.enabled = true,
                                .strict = strict,
                                .frameNumber = frameNumber,
                                .sceneGeneration = sceneGeneration};
    std::unordered_map<Key, uint32_t, KeyHash> next;
    for (const auto& observation : observations) {
        if (observation.visiblePixels == 0)
            continue;
        ++result.visibleInstances;
        if (!observation.occluded)
            continue;
        const Key key{observation.instanceIdentity,
                      observation.instanceIdentity == 0 ? observation.instanceRow : 0};
        const auto previous = m_streaks.find(key);
        const uint32_t streak = previous == m_streaks.end() ? 1 : previous->second + 1;
        next.emplace(key, streak);
        ++result.falselyRejectedInstances;
        result.falselyRejectedPixels += observation.visiblePixels;
        result.maximumMissingStreak = std::max(result.maximumMissingStreak, streak);
        result.missing.push_back({observation.instanceIdentity, observation.instanceRow,
                                  observation.visiblePixels, streak});
    }
    m_streaks = std::move(next);
    m_frameNumber = frameNumber;
    m_sceneGeneration = sceneGeneration;
    return result;
}
//======================================================================================================================
void OcclusionCheckHistory::reset() {
    m_streaks.clear();
    m_frameNumber = 0;
    m_sceneGeneration = 0;
}
} // namespace lmx::render
