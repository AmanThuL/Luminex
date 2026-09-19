//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterCheck.h
/// @brief Compares retired froxel lists and counters with their declaration-time CPU mirror.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/LightClusters.h"

namespace lmx::render {
/// Owned raw evidence from one checked declaration, retained only when list checking is enabled.
struct LightClusterCheckFrame {
    uint64_t frameNumber = 0;  ///< Device frame identity for exact joins to images and timings.
    LightClusterParams params; ///< Declaration-time camera, extent, row count and capacities.
    LightClusterLists gpu;     ///< Retired GPU records, defined indices and counters.
    LightClusterLists cpu;     ///< Independent CPU mirror from that declaration's copied rows.
};

/// Exact differences for one retired GPU grid; lengths and record flags participate in equality.
struct LightClusterCheckResult {
    uint32_t gridMismatches = 0;    ///< Differing records, including missing or extra records.
    uint32_t indexMismatches = 0;   ///< Differing indices, including missing or extra entries.
    uint32_t counterMismatches = 0; ///< Differing fields among the six reconciliation counters.
    /// Whether all grid records, list entries and counter fields equal the CPU mirror.
    bool passed() const {
        return gridMismatches == 0 && indexMismatches == 0 && counterMismatches == 0;
    }
};

/// Compares defined readback bytes with an owned mirror from the same declaration. No GPU access
/// occurs here. Offsets and the truncated bit are compared exactly, including zero-count records.
LightClusterCheckResult checkLightClusters(const LightClusterLists& expected,
                                           std::span<const ClusterRecord> grid,
                                           std::span<const uint32_t> indices,
                                           const LightClusterCounters& counters);
} // namespace lmx::render
