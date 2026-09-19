//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterReadback.cpp
/// @brief Decodes retired froxel-grid counters and, when asked, the grid and index list.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/LightClusterStage.h"

#include <algorithm>

namespace lmx::render {

//======================================================================================================================
RetiredLightClusters LightClusterStage::readback(const Pending& pending) const {
    RetiredLightClusters result{.frameNumber = pending.frameNumber};
    std::array<uint32_t, kLightClusterCounterWords> words{};
    pending.counters->readback(words.data(), sizeof(words));
    result.counters = {.candidates = words[0],
                       .assigned = words[1],
                       .droppedPerCluster = words[2],
                       .droppedGlobal = words[3],
                       .truncatedFroxels = words[4],
                       .maxCount = words[5]};
    if (!pending.captureLists) {
        return result;
    }
    result.grid.resize(kClusterCount);
    pending.grid->readback(result.grid.data(), result.grid.size() * sizeof(ClusterRecord));
    // Only the leading `assigned` entries are defined; the rest of the list is whatever the
    // previous occupant of this paced slot left behind, so it is never handed out.
    const uint32_t assigned = std::min(result.counters.assigned, kLightClusterIndexCapacity);
    if (assigned > 0) {
        result.indices.resize(assigned);
        pending.indices->readback(result.indices.data(), result.indices.size() * sizeof(uint32_t));
    }
    return result;
}

//======================================================================================================================
void LightClusterStage::retireThrough(uint64_t completedFrame) {
    for (const auto& pending : m_pending)
        if (pending.frameNumber <= completedFrame)
            m_retired.push_back(readback(pending));
    std::erase_if(m_pending, [completedFrame](const Pending& pending) {
        return pending.frameNumber <= completedFrame;
    });
}

//======================================================================================================================
std::vector<RetiredLightClusters> LightClusterStage::takeRetired() {
    auto result = std::move(m_retired);
    m_retired.clear();
    return result;
}

} // namespace lmx::render
