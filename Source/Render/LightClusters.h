//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusters.h
/// @brief Declares the cluster grid lookups and the CPU mirror of the light clustering kernels.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Scene/SceneTables.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace lmx::render {

/// Froxel columns across the scene pass's active render rectangle.
inline constexpr uint32_t kClusterTilesX = 16;
/// Froxel rows across the scene pass's active render rectangle.
inline constexpr uint32_t kClusterTilesY = 9;
/// Depth slices; slice 0 is the near sliver and the last slice is open toward infinity.
inline constexpr uint32_t kClusterSliceCount = 24;
/// Froxels in one grid; the flat index is `(slice * kClusterTilesY + y) * kClusterTilesX + x`.
inline constexpr uint32_t kClusterCount = kClusterTilesX * kClusterTilesY * kClusterSliceCount;
/// Boundary depths in the uploaded slice table: one per slice plus the closing boundary.
inline constexpr uint32_t kClusterSliceBoundaryCount = kClusterSliceCount + 1;
/// View distance in metres where the exponentially spaced slices begin.
inline constexpr float kClusterNearDistance = 0.3f;
/// View distance in metres where the exponentially spaced slices end and the open slice begins.
inline constexpr float kClusterFarDistance = 100.0f;
/// Largest number of light rows one froxel may list; extra intersections are dropped.
inline constexpr uint32_t kMaxLightsPerCluster = 128;
/// Entries in the flat index list shared by every froxel.
inline constexpr uint32_t kLightClusterIndexCapacity = 65536;
/// Bit 31 of a record's `count`: the froxel lost at least one intersecting light.
inline constexpr uint32_t kClusterTruncatedBit = 1u << 31;

/// One froxel's range in the flat index list. `count` carries kClusterTruncatedBit, so a reader
/// masks it off before iterating; the mirror and the GPU kernels write this layout byte for byte.
struct ClusterRecord {
    uint32_t offset = 0; ///< First entry of this froxel's range in the index list.
    uint32_t count = 0;  ///< Listed row count in bits 0-30; bit 31 marks a truncated froxel.
};
static_assert(sizeof(ClusterRecord) == 8);
static_assert(offsetof(ClusterRecord, offset) == 0);
static_assert(offsetof(ClusterRecord, count) == 4);

/// Inputs shared by the mirror and the `lmx.pass.light.*` kernels. `view` and
/// `inverseJitteredProjection` belong to the frame that rasterizes the scene pass, so the froxel
/// bounds and the fragment's own slice lookup agree exactly.
struct LightClusterParams {
    glm::mat4 view{1.0f};                      ///< World-to-view transform, metres.
    glm::mat4 inverseJitteredProjection{1.0f}; ///< Inverse of the jittered scene projection.
    uint32_t rowCount = 0;                     ///< Addressable light rows, including free slots.
    uint32_t activeWidth = 0;                  ///< Active render rectangle width in pixels.
    uint32_t activeHeight = 0;                 ///< Active render rectangle height in pixels.
    uint32_t perClusterCap = kMaxLightsPerCluster;              ///< Entries one froxel may keep.
    uint32_t globalCapacity = kLightClusterIndexCapacity;       ///< Entries the index list holds.
    std::array<float, kClusterSliceBoundaryCount> sliceDepth{}; ///< clusterSliceDepths' table.
};

/// Reconciling totals for one built grid; `assigned + droppedPerCluster + droppedGlobal` always
/// equals `candidates`, so overflow is visible rather than silent.
struct LightClusterCounters {
    uint32_t candidates = 0;        ///< Froxel/light intersections found, before any cap.
    uint32_t assigned = 0;          ///< Entries actually written to the index list.
    uint32_t droppedPerCluster = 0; ///< Intersections beyond `perClusterCap` within a froxel.
    uint32_t droppedGlobal = 0;     ///< Entries lost to `globalCapacity` after that clamping.
    uint32_t truncatedFroxels = 0;  ///< Records carrying kClusterTruncatedBit.
    uint32_t maxCount = 0;          ///< Largest post-clamp, pre-global froxel count.
};

/// A built grid: one record per froxel in flat index order plus the entries they address.
struct LightClusterLists {
    std::vector<ClusterRecord> grid; ///< Exactly kClusterCount records.
    std::vector<uint32_t> indices;   ///< Exactly `counters.assigned` row indices.
    LightClusterCounters counters;   ///< Totals for the whole grid.
};

/// Builds the 25 reversed-Z boundary depths for a camera whose near plane sits at `nearZ` metres.
/// Boundary 0 is the near plane (depth 1), boundaries 1 to kClusterSliceCount-1 are exponentially
/// spaced view distances from kClusterNearDistance to kClusterFarDistance, and the last boundary
/// is depth 0 (infinity), which leaves the last slice open. Entries never increase; a camera whose
/// near plane already sits at or beyond kClusterNearDistance clamps boundary 1 to 1 and leaves
/// slice 0 empty. Only the CPU evaluates the transcendental spacing; the table is uploaded and the
/// GPU compares against it. `nearZ` must be positive.
std::array<float, kClusterSliceBoundaryCount> clusterSliceDepths(float nearZ);

/// Returns the slice holding reversed-Z `depth`: the lowest `s` with `table[s] >= depth` and
/// `depth > table[s + 1]`. The lookup is pure comparison, so a fragment, the kernels and the
/// mirror never disagree. Depth 1 lands in slice 0 unless slice 0 is empty; a depth at or below 0,
/// or one no slice claims, lands in the open last slice.
uint32_t clusterSlice(float depth, std::span<const float, kClusterSliceBoundaryCount> table);

/// Returns the froxel column and row holding `pixel`, in unsigned integer arithmetic clamped to
/// the last tile. `pixel` is expected inside the half-open active rectangle; the subtraction wraps
/// for a pixel before `activeOrigin` and the clamp then yields the last tile. `activeExtent` must
/// be nonzero on both axes.
glm::uvec2 clusterTile(glm::uvec2 pixel, glm::uvec2 activeOrigin, glm::uvec2 activeExtent);

/// Returns `tile`'s half-open pixel-edge span on one axis of an `extent`-wide active rectangle
/// divided into `tileCount` tiles: `{ceilDiv(tile * extent, tileCount), ceilDiv((tile + 1) *
/// extent, tileCount)}`, where `ceilDiv(a, b)` is `(a + b - 1) / b` in unsigned arithmetic. This is
/// exactly the set of pixels `clusterTile` assigns to `tile`, so a froxel box built from these
/// edges covers each of its pixels' whole area - `clusterTile` divides the pixel index while the
/// shaded point is the pixel centre, and a rectangle cut at `tile * extent / tileCount` would leave
/// the last pixel of a tile partly outside its own froxel. The span is empty
/// (`low == high`) only when `extent < tileCount`; such a tile holds no pixel, is never looked up,
/// and is never given lights. `tile` must be below `tileCount` and `tileCount` must be nonzero.
glm::uvec2 clusterTileEdges(uint32_t tile, uint32_t tileCount, uint32_t extent);

/// Builds the grid and index list the `lmx.pass.light.count`/`scan`/`fill` kernels must reproduce
/// bit for bit: every operation is an explicit ordered fp32 sequence and the unit compiles with
/// floating-point contraction disabled.
///
/// A froxel's bounds are the axis-aligned view-space box of its eight corners, unprojected from
/// its tile's NDC rectangle at the slice's two boundary depths. That rectangle comes from
/// `clusterTileEdges` against `activeWidth`/`activeHeight`, converted by one division per edge
/// (`2 * edge / extent - 1` on x, `1 - 2 * edge / extent` on y), so it is pixel-aligned and covers
/// every pixel `clusterTile` assigns to the tile. Two kinds of froxel can never be looked up and
/// are therefore never tested, keep count 0 and spend no capacity: one whose tile owns no pixel
/// (`clusterTileEdges` returns an empty span), and one whose slice the near plane collapsed
/// (`sliceDepth[slice] == sliceDepth[slice + 1]`, which `clusterSliceDepths` produces for every
/// boundary already behind the near plane). The GPU kernels must skip the same two.
/// The open last slice has no far
/// boundary, so it uses this conservative rule instead: take the box of the four corners at its
/// near boundary, let `nearDistance` be the view distance of that face's nearest corner and
/// `farDistance` the sphere centre's view distance plus its radius. A sphere whose `farDistance`
/// is below `nearDistance` is rejected; otherwise the near face is scaled by
/// `farDistance / nearDistance` on x and y - the froxel widens linearly with view distance, so the
/// union of the unscaled and scaled faces contains it out to `farDistance` - and closed in z at
/// `-farDistance`. Nothing beyond `farDistance` can lie within the radius, so the box is a
/// conservative superset of the part of the froxel the sphere can touch.
///
/// A row intersects when the squared distance from its view-space bound centre to that box is at
/// most its squared bound radius. Rows with a bound radius at or below zero are free slots: they
/// are skipped and never counted. A froxel keeps its lowest row indices up to `perClusterCap`, the
/// scan grants ranges in flat froxel order, the first froxel that does not fit takes the remaining
/// entries and every later froxel with intersections gets count 0; each such record is truncated.
/// `rows` beyond `rowCount` are ignored.
LightClusterLists buildLightClusters(std::span<const engine::LightRow> rows,
                                     const LightClusterParams& params);

} // namespace lmx::render
