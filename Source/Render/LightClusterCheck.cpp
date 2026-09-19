//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterCheck.cpp
/// @brief Counts exact GPU and CPU differences without hiding length or overflow mismatches.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/LightClusterCheck.h"

#include <algorithm>
#include <array>

namespace lmx::render {
//======================================================================================================================
LightClusterCheckResult checkLightClusters(const LightClusterLists& expected,
                                           std::span<const ClusterRecord> grid,
                                           std::span<const uint32_t> indices,
                                           const LightClusterCounters& counters) {
    LightClusterCheckResult result;
    const auto countDifference = [](size_t a, size_t b) {
        return static_cast<uint32_t>(std::max(a, b) - std::min(a, b));
    };
    result.gridMismatches = countDifference(expected.grid.size(), grid.size());
    for (size_t i = 0; i < std::min(expected.grid.size(), grid.size()); ++i)
        result.gridMismatches +=
            expected.grid[i].offset != grid[i].offset || expected.grid[i].count != grid[i].count;
    result.indexMismatches = countDifference(expected.indices.size(), indices.size());
    for (size_t i = 0; i < std::min(expected.indices.size(), indices.size()); ++i)
        result.indexMismatches += expected.indices[i] != indices[i];
    const auto words = [](const LightClusterCounters& c) {
        return std::array{c.candidates,    c.assigned,         c.droppedPerCluster,
                          c.droppedGlobal, c.truncatedFroxels, c.maxCount};
    };
    const auto expectedWords = words(expected.counters);
    const auto actualWords = words(counters);
    for (size_t i = 0; i < expectedWords.size(); ++i)
        result.counterMismatches += expectedWords[i] != actualWords[i];
    return result;
}
} // namespace lmx::render
