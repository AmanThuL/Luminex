//----------------------------------------------------------------------------------------------------------------------
/// @file SceneTableStats.h
/// @brief Declares CPU diagnostics for paced scene tables.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>

namespace lmx::engine {

/// Scene ownership and upload counters; diagnostics do not measure GPU performance.
struct SceneTableStats {
    uint32_t instanceCount = 0;    ///< Live instances, independent of row holes.
    uint32_t materialCount = 0;    ///< Live shared materials.
    uint32_t meshCount = 0;        ///< Immutable meshes, including sky geometry.
    uint32_t instanceCapacity = 0; ///< Allocated rows per frame slot.
    uint32_t materialCapacity = 0; ///< Allocated rows per frame slot.
    uint32_t meshCapacity = 0;     ///< Allocated rows per frame slot.
    uint32_t lightCount = 0;       ///< Existing light identities, including disabled lights.
    uint32_t lightCapacity = 0; ///< Allocated rows per frame slot; retained after removal/disable.
    uint64_t vertexBytes = 0;   ///< Geometry-pool vertex bytes.
    uint64_t indexBytes = 0;    ///< Geometry-pool index bytes.
    uint32_t rowsWritten = 0;   ///< Rows uploaded by the last prepareFrame call.
    uint64_t bytesWritten = 0;  ///< Bytes uploaded by the last prepareFrame call.
    uint32_t slot = 0;          ///< Last prepared frame slot in [0, 2].
    uint64_t growthEvents = 0;  ///< Table capacity increases after finalize.
    uint32_t pendingReleaseBuffers = 0; ///< Old table buffers awaiting paced retirement.
};

} // namespace lmx::engine
