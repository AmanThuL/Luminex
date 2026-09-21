//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusters.cpp
/// @brief Implements the cluster lookups and the mirror without floating-point contraction.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/LightClusters.h"

#include "Core/Diagnostics/Assert.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lmx::render {

namespace {

// View-space bounds of one froxel. The open last slice stores only its near face and closes in z
// per tested sphere, so `open` selects the rule documented on buildLightClusters.
struct FroxelBounds {
    glm::vec3 minimum{0.0f};
    glm::vec3 maximum{0.0f};
    float nearDistance = 0.0f;
    bool open = false;
    bool empty = false;
};

//======================================================================================================================
// Literal comparisons rather than min/max intrinsics: a Slang kernel transcribes the comparison and
// inherits no separate NaN or signed-zero rule.
float lesser(float a, float b) {
    return a < b ? a : b;
}

//======================================================================================================================
float greater(float a, float b) {
    return a > b ? a : b;
}

//======================================================================================================================
// One ordered fp32 4x4 transform of a point; glm stores columns, so m[column][row].
glm::vec3 transformPoint(const glm::mat4& m, glm::vec3 point) {
    glm::vec3 result{0.0f};
    for (uint32_t r = 0; r < 3; ++r) {
        const float x = m[0][r] * point.x;
        const float y = m[1][r] * point.y;
        const float z = m[2][r] * point.z;
        const float xy = x + y;
        const float xyz = xy + z;
        result[int(r)] = xyz + m[3][r];
    }
    return result;
}

//======================================================================================================================
// Unprojects one NDC corner; the per-component division is exactly rounded, unlike a shared
// reciprocal, so a shader compiled with safe math reproduces it.
glm::vec3 unprojectCorner(const glm::mat4& m, float ndcX, float ndcY, float depth) {
    float clip[4];
    for (uint32_t r = 0; r < 4; ++r) {
        const float x = m[0][r] * ndcX;
        const float y = m[1][r] * ndcY;
        const float z = m[2][r] * depth;
        const float xy = x + y;
        const float xyz = xy + z;
        clip[r] = xyz + m[3][r];
    }
    return {clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]};
}

//======================================================================================================================
// Squared distance from `centre` to the closed box, in the same order a kernel would write it.
float boxDistanceSquared(glm::vec3 minimum, glm::vec3 maximum, glm::vec3 centre) {
    float delta[3];
    for (uint32_t axis = 0; axis < 3; ++axis) {
        const int a = int(axis);
        delta[axis] = 0.0f;
        if (centre[a] < minimum[a]) {
            delta[axis] = minimum[a] - centre[a];
        } else if (centre[a] > maximum[a]) {
            delta[axis] = centre[a] - maximum[a];
        }
    }
    const float x = delta[0] * delta[0];
    const float y = delta[1] * delta[1];
    const float z = delta[2] * delta[2];
    const float xy = x + y;
    return xy + z;
}

//======================================================================================================================
FroxelBounds froxelBounds(const LightClusterParams& params, uint32_t tileX, uint32_t tileY,
                          uint32_t slice) {
    const glm::uvec2 edgesX = clusterTileEdges(tileX, kClusterTilesX, params.activeWidth);
    const glm::uvec2 edgesY = clusterTileEdges(tileY, kClusterTilesY, params.activeHeight);
    FroxelBounds bounds;
    // A tile owning no pixel, or a slice the near plane collapsed, can never be looked up.
    if (edgesX.x == edgesX.y || edgesY.x == edgesY.y ||
        params.sliceDepth[slice] == params.sliceDepth[slice + 1]) {
        bounds.empty = true;
        return bounds;
    }
    const float left = 2.0f * float(edgesX.x) / float(params.activeWidth) - 1.0f;
    const float right = 2.0f * float(edgesX.y) / float(params.activeWidth) - 1.0f;
    const float top = 1.0f - 2.0f * float(edgesY.x) / float(params.activeHeight);
    const float bottom = 1.0f - 2.0f * float(edgesY.y) / float(params.activeHeight);
    const bool open = slice + 1 == kClusterSliceCount;
    const float depths[2] = {params.sliceDepth[slice], params.sliceDepth[slice + 1]};

    bounds.open = open;
    bounds.minimum = glm::vec3(std::numeric_limits<float>::max());
    bounds.maximum = glm::vec3(-std::numeric_limits<float>::max());
    const uint32_t faces = open ? 1u : 2u;
    for (uint32_t face = 0; face < faces; ++face) {
        for (uint32_t corner = 0; corner < 4; ++corner) {
            const float ndcX = (corner & 1u) != 0 ? right : left;
            const float ndcY = (corner & 2u) != 0 ? bottom : top;
            const glm::vec3 point =
                unprojectCorner(params.inverseJitteredProjection, ndcX, ndcY, depths[face]);
            for (uint32_t axis = 0; axis < 3; ++axis) {
                const int a = int(axis);
                bounds.minimum[a] = lesser(bounds.minimum[a], point[a]);
                bounds.maximum[a] = greater(bounds.maximum[a], point[a]);
            }
        }
    }
    // The nearest corner of the open slice's near face; the camera looks down -Z.
    bounds.nearDistance = -bounds.maximum.z;
    return bounds;
}

//======================================================================================================================
bool froxelIntersectsSphere(const FroxelBounds& bounds, glm::vec3 centre, float radius) {
    const float radiusSquared = radius * radius;
    if (!bounds.open) {
        return boxDistanceSquared(bounds.minimum, bounds.maximum, centre) <= radiusSquared;
    }
    // The open slice widens linearly with view distance, so scaling its near face by the sphere's
    // far extent covers every part of the froxel the sphere can touch; nothing past that extent
    // can lie within the radius.
    const float centreDistance = -centre.z;
    const float farDistance = centreDistance + radius;
    if (farDistance < bounds.nearDistance) {
        return false;
    }
    const float scale = farDistance / bounds.nearDistance;
    const float minX = bounds.minimum.x * scale;
    const float maxX = bounds.maximum.x * scale;
    const float minY = bounds.minimum.y * scale;
    const float maxY = bounds.maximum.y * scale;
    const glm::vec3 minimum{lesser(bounds.minimum.x, minX), lesser(bounds.minimum.y, minY),
                            -farDistance};
    const glm::vec3 maximum{greater(bounds.maximum.x, maxX), greater(bounds.maximum.y, maxY),
                            bounds.maximum.z};
    return boxDistanceSquared(minimum, maximum, centre) <= radiusSquared;
}

} // namespace

//======================================================================================================================
std::array<float, kClusterSliceBoundaryCount> clusterSliceDepths(float nearZ) {
    LMX_ASSERT(nearZ > 0.0f, "clusterSliceDepths: the near plane distance must be positive");
    std::array<float, kClusterSliceBoundaryCount> table{};
    table[0] = 1.0f;
    for (uint32_t boundary = 1; boundary + 1 < kClusterSliceBoundaryCount; ++boundary) {
        float distance = kClusterFarDistance;
        if (boundary == 1) {
            distance = kClusterNearDistance;
        } else if (boundary + 1 < kClusterSliceCount) {
            // Only the CPU evaluates this; the table is uploaded and the GPU only compares.
            const double fraction = double(boundary - 1) / double(kClusterSliceCount - 2);
            const double ratio = double(kClusterFarDistance) / double(kClusterNearDistance);
            distance = float(double(kClusterNearDistance) * std::pow(ratio, fraction));
        }
        table[boundary] = lesser(1.0f, nearZ / distance);
    }
    table[kClusterSliceBoundaryCount - 1] = 0.0f;
    return table;
}

//======================================================================================================================
uint32_t clusterSlice(float depth, std::span<const float, kClusterSliceBoundaryCount> table) {
    if (depth > table[0]) {
        return 0;
    }
    for (uint32_t slice = 0; slice + 1 < kClusterSliceBoundaryCount; ++slice) {
        if (!(depth > table[slice]) && depth > table[slice + 1]) {
            return slice;
        }
    }
    return kClusterSliceCount - 1;
}

//======================================================================================================================
glm::uvec2 clusterTile(glm::uvec2 pixel, glm::uvec2 activeOrigin, glm::uvec2 activeExtent) {
    LMX_ASSERT(activeExtent.x > 0 && activeExtent.y > 0,
               "clusterTile: the active extent must be nonzero on both axes");
    const uint32_t x = (pixel.x - activeOrigin.x) * kClusterTilesX / activeExtent.x;
    const uint32_t y = (pixel.y - activeOrigin.y) * kClusterTilesY / activeExtent.y;
    return {std::min(x, kClusterTilesX - 1), std::min(y, kClusterTilesY - 1)};
}

//======================================================================================================================
glm::uvec2 clusterTileEdges(uint32_t tile, uint32_t tileCount, uint32_t extent) {
    LMX_ASSERT(tileCount > 0 && tile < tileCount,
               "clusterTileEdges: the tile must lie inside the grid");
    const uint32_t low = (tile * extent + tileCount - 1) / tileCount;
    const uint32_t high = ((tile + 1) * extent + tileCount - 1) / tileCount;
    return {low, high};
}

//======================================================================================================================
LightClusterLists buildLightClusters(std::span<const engine::LightRow> rows,
                                     const LightClusterParams& params) {
    LMX_ASSERT(params.perClusterCap > 0, "buildLightClusters: the per-cluster cap must be nonzero");
    LMX_ASSERT(params.activeWidth > 0 && params.activeHeight > 0,
               "buildLightClusters: the active render rectangle must be nonempty");
    LMX_ASSERT(params.rowCount <= rows.size(),
               "buildLightClusters: rowCount must not exceed the rows it addresses");
    const uint32_t rowCount = params.rowCount;

    LightClusterLists lists;
    lists.grid.assign(kClusterCount, ClusterRecord{});
    if (rowCount == 0) {
        return lists;
    }

    // The bound centres are frame constants; a kernel transforms them per test, which the ordered
    // sequence above makes an identical value either way.
    std::vector<glm::vec3> centres(rowCount);
    for (uint32_t row = 0; row < rowCount; ++row) {
        centres[row] = transformPoint(params.view, rows[row].boundCentre);
    }

    std::vector<uint32_t> counts(kClusterCount, 0);
    std::vector<uint8_t> truncated(kClusterCount, 0);
    std::vector<FroxelBounds> bounds(kClusterCount);
    for (uint32_t slice = 0; slice < kClusterSliceCount; ++slice) {
        for (uint32_t y = 0; y < kClusterTilesY; ++y) {
            for (uint32_t x = 0; x < kClusterTilesX; ++x) {
                const uint32_t froxel = (slice * kClusterTilesY + y) * kClusterTilesX + x;
                bounds[froxel] = froxelBounds(params, x, y, slice);
                if (bounds[froxel].empty) {
                    continue;
                }
                uint32_t hits = 0;
                for (uint32_t row = 0; row < rowCount; ++row) {
                    if (rows[row].boundRadius <= 0.0f) {
                        continue;
                    }
                    if (froxelIntersectsSphere(bounds[froxel], centres[row],
                                               rows[row].boundRadius)) {
                        ++hits;
                    }
                }
                const uint32_t kept = std::min(hits, params.perClusterCap);
                counts[froxel] = kept;
                lists.counters.candidates += hits;
                lists.counters.droppedPerCluster += hits - kept;
                lists.counters.maxCount = std::max(lists.counters.maxCount, kept);
                truncated[froxel] = hits > kept ? 1 : 0;
            }
        }
    }

    uint32_t running = 0;
    std::vector<uint32_t> granted(kClusterCount, 0);
    for (uint32_t froxel = 0; froxel < kClusterCount; ++froxel) {
        const uint32_t remaining = params.globalCapacity - running;
        granted[froxel] = std::min(counts[froxel], remaining);
        if (granted[froxel] < counts[froxel]) {
            truncated[froxel] = 1;
            lists.counters.droppedGlobal += counts[froxel] - granted[froxel];
        }
        lists.grid[froxel].offset = running;
        lists.grid[froxel].count =
            granted[froxel] | (truncated[froxel] != 0 ? kClusterTruncatedBit : 0u);
        lists.counters.truncatedFroxels += truncated[froxel];
        running += granted[froxel];
    }
    lists.counters.assigned = running;

    // Repeating the test in ascending row order is what the fill kernel does, and it is what keeps
    // a truncated froxel's kept set the lowest indices of its candidates.
    lists.indices.assign(running, 0);
    for (uint32_t froxel = 0; froxel < kClusterCount; ++froxel) {
        uint32_t written = 0;
        for (uint32_t row = 0; row < rowCount && written < granted[froxel]; ++row) {
            if (rows[row].boundRadius <= 0.0f) {
                continue;
            }
            if (froxelIntersectsSphere(bounds[froxel], centres[row], rows[row].boundRadius)) {
                lists.indices[lists.grid[froxel].offset + written] = row;
                ++written;
            }
        }
    }
    return lists;
}

} // namespace lmx::render
