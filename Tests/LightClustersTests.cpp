//----------------------------------------------------------------------------------------------------------------------
/// @file LightClustersTests.cpp
/// @brief Tests the cluster grid lookups, the CPU clustering mirror and its overflow policy.
//----------------------------------------------------------------------------------------------------------------------

#include "Engine/Lights/LocalLight.h"
#include "Engine/Lights/LocalLightMath.h"
#include "Engine/Scene/SceneTables.h"
#include "Engine/View/Camera.h"
#include "Render/LightClusters.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace lmx::render;
using Catch::Approx;

namespace {

constexpr uint32_t kBoundaries = kClusterSliceBoundaryCount;

// A camera plus the jittered projection the scene pass would rasterize with.
struct TestView {
    lmx::engine::Camera camera;
    glm::mat4 projection{1.0f};
    uint32_t width = 0;
    uint32_t height = 0;
};

//======================================================================================================================
TestView makeView(glm::vec3 position, float yaw, float pitch, float nearZ, uint32_t width,
                  uint32_t height, glm::vec2 jitter) {
    TestView view;
    view.camera.position = position;
    view.camera.yaw = yaw;
    view.camera.pitch = pitch;
    view.camera.nearZ = nearZ;
    view.camera.fovY = glm::radians(60.0f);
    view.width = width;
    view.height = height;
    view.projection = view.camera.projectionMatrix(float(width) / float(height));
    // Temporal jitter is a clip-space shear proportional to w, exactly as TemporalResolve applies.
    view.projection[2][0] = jitter.x;
    view.projection[2][1] = jitter.y;
    return view;
}

//======================================================================================================================
LightClusterParams makeParams(const TestView& view, uint32_t rowCount) {
    LightClusterParams params;
    params.view = view.camera.viewMatrix();
    params.inverseJitteredProjection = glm::inverse(view.projection);
    params.rowCount = rowCount;
    params.activeWidth = view.width;
    params.activeHeight = view.height;
    params.sliceDepth = clusterSliceDepths(view.camera.nearZ);
    return params;
}

//======================================================================================================================
// The view-space point a pixel centre reconstructs to at `distance`, derived from the projection's
// own coefficients rather than from the mirror's unprojection.
glm::vec3 pixelCentreViewPoint(const TestView& view, glm::uvec2 pixel, float distance) {
    const double ndcX = (double(pixel.x) + 0.5) / double(view.width) * 2.0 - 1.0;
    const double ndcY = 1.0 - (double(pixel.y) + 0.5) / double(view.height) * 2.0;
    const double x =
        (ndcX + double(view.projection[2][0])) * double(distance) / double(view.projection[0][0]);
    const double y =
        (ndcY + double(view.projection[2][1])) * double(distance) / double(view.projection[1][1]);
    return {float(x), float(y), -distance};
}

//======================================================================================================================
lmx::engine::LightRow makePoint(glm::vec3 position, float range) {
    lmx::engine::LocalLight light;
    light.position = position;
    light.range = range;
    const auto row = lmx::engine::makeLightRow(light);
    REQUIRE(row.has_value());
    return *row;
}

//======================================================================================================================
uint32_t froxelIndex(glm::uvec2 tile, uint32_t slice) {
    return (slice * kClusterTilesY + tile.y) * kClusterTilesX + tile.x;
}

// Deterministic 64-bit LCG; the property test must not depend on a platform generator.
struct Rng {
    uint64_t state = 0;
};

//======================================================================================================================
double nextUnit(Rng& rng) {
    rng.state = rng.state * 6364136223846793005ull + 1442695040888963407ull;
    return double((rng.state >> 11) & ((1ull << 53) - 1)) / double(1ull << 53);
}

//======================================================================================================================
double nextRange(Rng& rng, double low, double high) {
    return low + (high - low) * nextUnit(rng);
}

} // namespace

//======================================================================================================================
TEST_CASE("the cluster slice table spans the frozen bounds in reversed depth",
          "[render][light-cluster]") {
    SECTION("an ordinary near plane") {
        const auto table = clusterSliceDepths(0.1f);
        REQUIRE(table[0] == 1.0f);
        REQUIRE(table[1] == Approx(0.1f / kClusterNearDistance));
        REQUIRE(table[kClusterSliceCount - 1] == Approx(0.1f / kClusterFarDistance));
        REQUIRE(table[kBoundaries - 1] == 0.0f);
        for (uint32_t s = 0; s + 1 < kBoundaries; ++s) {
            REQUIRE(table[s] > table[s + 1]);
        }
        // Equal ratios between consecutive view distances: the spacing is exponential.
        const float firstRatio = table[1] / table[2];
        for (uint32_t s = 1; s + 2 < kClusterSliceCount; ++s) {
            REQUIRE(table[s] / table[s + 1] == Approx(firstRatio).epsilon(1e-5));
        }
    }
    SECTION("a near plane at or beyond the first boundary empties slice 0") {
        const auto table = clusterSliceDepths(0.5f);
        REQUIRE(table[0] == 1.0f);
        REQUIRE(table[1] == 1.0f);
        for (uint32_t s = 0; s + 1 < kBoundaries; ++s) {
            REQUIRE(table[s] >= table[s + 1]);
        }
        // Every boundary whose view distance is already behind the near plane clamps to 1, so the
        // slices it closes are empty and the near plane's own depth lands past them.
        const uint32_t first = clusterSlice(1.0f, table);
        REQUIRE(first > 0);
        for (uint32_t s = 0; s < first; ++s) {
            REQUIRE(table[s] == table[s + 1]);
        }
        for (uint32_t s = first; s + 1 < kBoundaries; ++s) {
            REQUIRE(table[s] > table[s + 1]);
        }
    }
}

//======================================================================================================================
TEST_CASE("the cluster slice lookup claims every boundary and its open ends",
          "[render][light-cluster]") {
    const auto table = clusterSliceDepths(0.1f);
    for (uint32_t s = 0; s + 1 < kBoundaries; ++s) {
        REQUIRE(clusterSlice(table[s], table) == s);
        const float inside = (table[s] + table[s + 1]) * 0.5f;
        REQUIRE(clusterSlice(inside, table) == s);
    }
    REQUIRE(clusterSlice(1.0f, table) == 0);
    REQUIRE(clusterSlice(2.0f, table) == 0);
    REQUIRE(clusterSlice(0.0f, table) == kClusterSliceCount - 1);
    REQUIRE(clusterSlice(-1.0f, table) == kClusterSliceCount - 1);
    REQUIRE(clusterSlice(table[kClusterSliceCount - 1] * 0.5f, table) == kClusterSliceCount - 1);
}

//======================================================================================================================
TEST_CASE("the cluster tile lookup covers an odd active extent", "[render][light-cluster]") {
    const glm::uvec2 origin{0, 0};
    const glm::uvec2 extent{1281, 721};
    REQUIRE(clusterTile({0, 0}, origin, extent) == glm::uvec2{0, 0});
    REQUIRE(clusterTile({1280, 720}, origin, extent) == glm::uvec2{15, 8});
    for (uint32_t pixel = 0; pixel < extent.x; ++pixel) {
        const uint32_t expected = std::min(pixel * kClusterTilesX / extent.x, kClusterTilesX - 1);
        REQUIRE(clusterTile({pixel, 0}, origin, extent).x == expected);
    }
    for (uint32_t pixel = 0; pixel < extent.y; ++pixel) {
        const uint32_t expected = std::min(pixel * kClusterTilesY / extent.y, kClusterTilesY - 1);
        REQUIRE(clusterTile({0, pixel}, origin, extent).y == expected);
    }
    SECTION("the active origin shifts the whole grid") {
        const glm::uvec2 shifted{40, 20};
        REQUIRE(clusterTile(shifted, shifted, extent) == glm::uvec2{0, 0});
        REQUIRE(clusterTile({40 + 1280, 20 + 720}, shifted, extent) == glm::uvec2{15, 8});
    }
    SECTION("a pixel past the rectangle clamps to the last tile") {
        REQUIRE(clusterTile({4000, 4000}, origin, extent) == glm::uvec2{15, 8});
    }
}

//======================================================================================================================
TEST_CASE("froxel pixel edges are exactly the pixels the tile lookup assigns",
          "[render][light-cluster]") {
    const glm::uvec2 extents[] = {{1280, 720}, {640, 360}, {100, 57}, {1283, 721}, {17, 10}};
    for (const auto extent : extents) {
        std::vector<uint32_t> owner(extent.x, kClusterTilesX);
        for (uint32_t pixel = 0; pixel < extent.x; ++pixel) {
            const uint32_t tile = clusterTile({pixel, 0}, {0, 0}, extent).x;
            const glm::uvec2 edges = clusterTileEdges(tile, kClusterTilesX, extent.x);
            REQUIRE(edges.x <= pixel);
            REQUIRE(pixel < edges.y);
            owner[pixel] = tile;
        }
        for (uint32_t pixel = 0; pixel < extent.y; ++pixel) {
            const uint32_t tile = clusterTile({0, pixel}, {0, 0}, extent).y;
            const glm::uvec2 edges = clusterTileEdges(tile, kClusterTilesY, extent.y);
            REQUIRE(edges.x <= pixel);
            REQUIRE(pixel < edges.y);
        }
        // The spans partition the whole rectangle: contiguous, non-overlapping, closing at extent.
        uint32_t previous = 0;
        for (uint32_t tile = 0; tile < kClusterTilesX; ++tile) {
            const glm::uvec2 edges = clusterTileEdges(tile, kClusterTilesX, extent.x);
            REQUIRE(edges.x == previous);
            REQUIRE(edges.y >= edges.x);
            for (uint32_t pixel = edges.x; pixel < edges.y; ++pixel) {
                REQUIRE(owner[pixel] == tile);
            }
            previous = edges.y;
        }
        REQUIRE(previous == extent.x);
        previous = 0;
        for (uint32_t tile = 0; tile < kClusterTilesY; ++tile) {
            const glm::uvec2 edges = clusterTileEdges(tile, kClusterTilesY, extent.y);
            REQUIRE(edges.x == previous);
            REQUIRE(edges.y >= edges.x);
            previous = edges.y;
        }
        REQUIRE(previous == extent.y);
    }
}

//======================================================================================================================
TEST_CASE("a tile holding no pixel lists nothing", "[render][light-cluster]") {
    // Ten columns over sixteen tiles: six tiles own no pixel at all.
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 10, 8, {0.0f, 0.0f});
    const std::vector<lmx::engine::LightRow> rows{makePoint({0.0f, 0.0f, 0.0f}, 1000.0f)};
    const auto params = makeParams(view, 1);
    const auto lists = buildLightClusters(rows, params);

    uint32_t emptyTiles = 0;
    for (uint32_t tile = 0; tile < kClusterTilesX; ++tile) {
        const glm::uvec2 edges = clusterTileEdges(tile, kClusterTilesX, view.width);
        if (edges.x != edges.y) {
            continue;
        }
        ++emptyTiles;
        for (uint32_t slice = 0; slice < kClusterSliceCount; ++slice) {
            for (uint32_t y = 0; y < kClusterTilesY; ++y) {
                REQUIRE(lists.grid[froxelIndex({tile, y}, slice)].count == 0);
            }
        }
    }
    REQUIRE(emptyTiles == kClusterTilesX - view.width);
    REQUIRE(lists.counters.assigned > 0);
    // No pixel maps to an empty tile, so none is ever looked up.
    for (uint32_t pixel = 0; pixel < view.width; ++pixel) {
        const uint32_t tile = clusterTile({pixel, 0}, {0, 0}, {view.width, view.height}).x;
        const glm::uvec2 edges = clusterTileEdges(tile, kClusterTilesX, view.width);
        REQUIRE(edges.x != edges.y);
    }
}

//======================================================================================================================
// Regression: `clusterTile` divides the pixel INDEX while the shaded point is the pixel CENTRE. At
// an extent the grid does not divide, the last pixel of a tile has its centre past the tile's
// `tile * extent / tileCount` edge, so a froxel cut there would lose a light that only reaches that
// centre. The pixel-aligned rectangle keeps it.
TEST_CASE("a light reaching only a straddling pixel's centre stays listed",
          "[render][light-cluster]") {
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 100, 57, {0.0f, 0.0f});
    const glm::uvec2 pixel{6, 3};
    REQUIRE(clusterTile(pixel, {0, 0}, {view.width, view.height}) == glm::uvec2{0, 0});
    // Pixel 6 belongs to tile 0, yet its centre 6.5 lies past tile 0's exact edge 6.25.
    REQUIRE(clusterTileEdges(0, kClusterTilesX, view.width) == glm::uvec2{0, 7});

    auto params = makeParams(view, 1);
    // Sit the point on the slice's near boundary, where the froxel box hugs the tile rectangle and
    // the quarter-pixel gap is not masked by the box's growth toward the far boundary.
    const uint32_t slice = 12;
    const float distance = view.camera.nearZ / params.sliceDepth[slice];
    REQUIRE(distance > 5.0f);
    REQUIRE(clusterSlice(params.sliceDepth[slice], params.sliceDepth) == slice);

    const glm::vec3 viewPoint = pixelCentreViewPoint(view, pixel, distance);
    const glm::vec3 world{glm::inverse(view.camera.viewMatrix()) * glm::vec4(viewPoint, 1.0f)};
    // A 1 cm light is far narrower than the quarter pixel separating the two rectangles.
    const std::vector<lmx::engine::LightRow> rows{makePoint(world, 0.01f)};
    const auto lists = buildLightClusters(rows, params);

    const auto& record = lists.grid[froxelIndex({0, 0}, slice)];
    REQUIRE((record.count & ~kClusterTruncatedBit) == 1);
    REQUIRE(lists.indices[record.offset] == 0);
}

//======================================================================================================================
TEST_CASE("built cluster lists are ascending, contiguous and reconcile",
          "[render][light-cluster]") {
    const auto view = makeView({0.0f, 1.5f, 4.0f}, 0.0f, 0.0f, 0.1f, 1280, 720, {0.0f, 0.0f});
    std::vector<lmx::engine::LightRow> rows;
    for (uint32_t i = 0; i < 12; ++i) {
        const float offset = float(i) - 6.0f;
        rows.push_back(
            makePoint({offset * 0.7f, 1.0f, -2.0f - offset * 0.5f}, 3.0f + offset * 0.1f));
    }
    // A free slot must never be listed.
    rows.push_back(lmx::engine::LightRow{});
    const auto params = makeParams(view, uint32_t(rows.size()));
    const auto lists = buildLightClusters(rows, params);

    REQUIRE(lists.grid.size() == kClusterCount);
    REQUIRE(lists.indices.size() == lists.counters.assigned);
    REQUIRE(lists.counters.assigned > 0);
    REQUIRE(lists.counters.droppedPerCluster == 0);
    REQUIRE(lists.counters.droppedGlobal == 0);
    REQUIRE(lists.counters.truncatedFroxels == 0);
    REQUIRE(lists.counters.assigned + lists.counters.droppedPerCluster +
                lists.counters.droppedGlobal ==
            lists.counters.candidates);

    uint32_t running = 0;
    uint32_t largest = 0;
    for (const auto& record : lists.grid) {
        REQUIRE((record.count & kClusterTruncatedBit) == 0);
        REQUIRE(record.offset == running);
        running += record.count;
        largest = std::max(largest, record.count);
        for (uint32_t i = 0; i + 1 < record.count; ++i) {
            REQUIRE(lists.indices[record.offset + i] < lists.indices[record.offset + i + 1]);
        }
        for (uint32_t i = 0; i < record.count; ++i) {
            REQUIRE(lists.indices[record.offset + i] < rows.size() - 1);
        }
    }
    REQUIRE(running == lists.counters.assigned);
    REQUIRE(lists.counters.maxCount == largest);
}

//======================================================================================================================
TEST_CASE("the open last slice lists a light at 500 m", "[render][light-cluster]") {
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 1280, 720, {0.0f, 0.0f});
    const std::vector<lmx::engine::LightRow> rows{makePoint({0.0f, 0.0f, -500.0f}, 20.0f)};
    const auto params = makeParams(view, 1);
    const auto lists = buildLightClusters(rows, params);

    const float depth = view.camera.nearZ / 500.0f;
    const uint32_t slice = clusterSlice(depth, params.sliceDepth);
    REQUIRE(slice == kClusterSliceCount - 1);
    const glm::uvec2 tile =
        clusterTile({view.width / 2, view.height / 2}, {0, 0}, {view.width, view.height});
    const auto& record = lists.grid[froxelIndex(tile, slice)];
    REQUIRE((record.count & ~kClusterTruncatedBit) == 1);
    REQUIRE(lists.indices[record.offset] == 0);

    // Conservative, not exhaustive: a 20 m sphere at 500 m cannot reach the near slices.
    for (uint32_t s = 0; s < kClusterSliceCount - 2; ++s) {
        for (uint32_t y = 0; y < kClusterTilesY; ++y) {
            for (uint32_t x = 0; x < kClusterTilesX; ++x) {
                REQUIRE(lists.grid[froxelIndex({x, y}, s)].count == 0);
            }
        }
    }
}

//======================================================================================================================
// Discriminates the open slice's scaling rule: these lights sit far off the view axis, so the
// unscaled near face cannot contain them and both signs of the min/max union are exercised.
TEST_CASE("the open slice lists off-axis lights on both sides of the view axis",
          "[render][light-cluster]") {
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 1280, 720, {0.0f, 0.0f});
    const glm::mat4 viewToWorld = glm::inverse(view.camera.viewMatrix());
    const auto place = [&](glm::uvec2 pixel, float distance, float range) {
        const glm::vec3 viewPoint = pixelCentreViewPoint(view, pixel, distance);
        return makePoint(glm::vec3{viewToWorld * glm::vec4(viewPoint, 1.0f)}, range);
    };
    // Right of the axis and above it, then left of the axis and below it.
    const glm::uvec2 rightPixel{1150, 40};
    const glm::uvec2 leftPixel{130, 680};
    const std::vector<lmx::engine::LightRow> rows{place(rightPixel, 400.0f, 1.0f),
                                                  place(leftPixel, 400.0f, 1.0f)};
    const auto params = makeParams(view, 2);
    const auto lists = buildLightClusters(rows, params);

    const uint32_t open = kClusterSliceCount - 1;
    const glm::uvec2 extent{view.width, view.height};
    const glm::uvec2 rightTile = clusterTile(rightPixel, {0, 0}, extent);
    const glm::uvec2 leftTile = clusterTile(leftPixel, {0, 0}, extent);
    REQUIRE(rightTile == glm::uvec2{14, 0});
    REQUIRE(leftTile == glm::uvec2{1, 8});

    const auto listedIn = [&](glm::uvec2 tile, uint32_t slice, uint32_t row) {
        const auto& record = lists.grid[froxelIndex(tile, slice)];
        for (uint32_t i = 0; i < (record.count & ~kClusterTruncatedBit); ++i) {
            if (lists.indices[record.offset + i] == row) {
                return true;
            }
        }
        return false;
    };
    // Independent sections: each branch of the union must fail on its own.
    SECTION("right of the view axis") {
        REQUIRE(listedIn(rightTile, open, 0));
        // The light may not leak into the opposite corner or the froxel on the view axis.
        REQUIRE_FALSE(listedIn(leftTile, open, 0));
        REQUIRE_FALSE(listedIn({7, 4}, open, 0));
    }
    SECTION("left of the view axis") {
        REQUIRE(listedIn(leftTile, open, 1));
        REQUIRE_FALSE(listedIn(rightTile, open, 1));
        REQUIRE_FALSE(listedIn({7, 4}, open, 1));
    }
    // A 1 m light at 400 m reaches no closed slice, all of which end by 100 m.
    for (uint32_t slice = 0; slice < open; ++slice) {
        for (uint32_t froxel = 0; froxel < kClusterTilesX * kClusterTilesY; ++froxel) {
            REQUIRE(lists.grid[slice * kClusterTilesX * kClusterTilesY + froxel].count == 0);
        }
    }
}

//======================================================================================================================
// Discriminates the open slice's `farDistance < nearDistance` rejection and its close-off in z.
TEST_CASE("the open slice rejects a sphere that ends before its near boundary",
          "[render][light-cluster]") {
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 1280, 720, {0.0f, 0.0f});
    const auto params = makeParams(view, 1);
    const uint32_t open = kClusterSliceCount - 1;
    const uint32_t firstOpenFroxel = open * kClusterTilesX * kClusterTilesY;

    SECTION("a sphere ending at 60 m reaches no open froxel") {
        const std::vector<lmx::engine::LightRow> rows{makePoint({0.0f, 0.0f, -50.0f}, 10.0f)};
        const auto lists = buildLightClusters(rows, params);
        REQUIRE(lists.counters.assigned > 0);
        for (uint32_t froxel = 0; froxel < kClusterTilesX * kClusterTilesY; ++froxel) {
            REQUIRE(lists.grid[firstOpenFroxel + froxel].count == 0);
        }
    }
    SECTION("a sphere just reaching past 100 m enters the open slice") {
        const std::vector<lmx::engine::LightRow> rows{makePoint({0.0f, 0.0f, -90.0f}, 10.5f)};
        const auto lists = buildLightClusters(rows, params);
        const auto& record = lists.grid[froxelIndex({8, 4}, open)];
        REQUIRE((record.count & ~kClusterTruncatedBit) == 1);
        REQUIRE(lists.indices[record.offset] == 0);
    }
}

//======================================================================================================================
TEST_CASE("a degenerate depth slice lists nothing", "[render][light-cluster]") {
    // A near plane past the first boundaries collapses the slices they close.
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.5f, 1280, 720, {0.0f, 0.0f});
    const std::vector<lmx::engine::LightRow> rows{makePoint({0.0f, 0.0f, 0.0f}, 1000.0f)};
    const auto params = makeParams(view, 1);
    const auto lists = buildLightClusters(rows, params);

    uint32_t degenerate = 0;
    for (uint32_t slice = 0; slice + 1 < kClusterSliceCount; ++slice) {
        if (params.sliceDepth[slice] != params.sliceDepth[slice + 1]) {
            continue;
        }
        ++degenerate;
        // No depth can look the slice up, so it must never hold a light either.
        REQUIRE(clusterSlice(params.sliceDepth[slice], params.sliceDepth) != slice);
        for (uint32_t froxel = 0; froxel < kClusterTilesX * kClusterTilesY; ++froxel) {
            REQUIRE(lists.grid[slice * kClusterTilesX * kClusterTilesY + froxel].count == 0);
        }
    }
    REQUIRE(degenerate >= 1);
    REQUIRE(lists.counters.assigned > 0);
}

//======================================================================================================================
TEST_CASE("per-cluster overflow keeps the lowest rows", "[render][light-cluster]") {
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 1280, 720, {0.0f, 0.0f});
    std::vector<lmx::engine::LightRow> rows;
    for (uint32_t i = 0; i < 10; ++i) {
        rows.push_back(makePoint({0.0f, 0.0f, 0.0f}, 1000.0f));
    }
    auto params = makeParams(view, uint32_t(rows.size()));
    params.perClusterCap = 4;
    const auto lists = buildLightClusters(rows, params);

    REQUIRE(lists.counters.candidates == kClusterCount * 10);
    REQUIRE(lists.counters.assigned == kClusterCount * 4);
    REQUIRE(lists.counters.droppedPerCluster == kClusterCount * 6);
    REQUIRE(lists.counters.droppedGlobal == 0);
    REQUIRE(lists.counters.truncatedFroxels == kClusterCount);
    REQUIRE(lists.counters.maxCount == 4);
    for (const auto& record : lists.grid) {
        REQUIRE((record.count & kClusterTruncatedBit) != 0);
        REQUIRE((record.count & ~kClusterTruncatedBit) == 4);
        for (uint32_t i = 0; i < 4; ++i) {
            REQUIRE(lists.indices[record.offset + i] == i);
        }
    }
}

//======================================================================================================================
TEST_CASE("global overflow truncates the first non-fitting froxel and zeroes the rest",
          "[render][light-cluster]") {
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 1280, 720, {0.0f, 0.0f});
    std::vector<lmx::engine::LightRow> rows;
    for (uint32_t i = 0; i < 10; ++i) {
        rows.push_back(makePoint({0.0f, 0.0f, 0.0f}, 1000.0f));
    }
    auto params = makeParams(view, uint32_t(rows.size()));
    params.globalCapacity = 95;
    const auto lists = buildLightClusters(rows, params);

    REQUIRE(lists.counters.candidates == kClusterCount * 10);
    REQUIRE(lists.counters.droppedPerCluster == 0);
    REQUIRE(lists.counters.assigned == 95);
    REQUIRE(lists.indices.size() == 95);
    REQUIRE(lists.counters.droppedGlobal == kClusterCount * 10 - 95);
    REQUIRE(lists.counters.truncatedFroxels == kClusterCount - 9);
    REQUIRE(lists.counters.maxCount == 10);
    REQUIRE(lists.counters.assigned + lists.counters.droppedPerCluster +
                lists.counters.droppedGlobal ==
            lists.counters.candidates);

    for (uint32_t f = 0; f < 9; ++f) {
        REQUIRE(lists.grid[f].count == 10);
        REQUIRE(lists.grid[f].offset == f * 10);
    }
    REQUIRE(lists.grid[9].offset == 90);
    REQUIRE(lists.grid[9].count == (5u | kClusterTruncatedBit));
    for (uint32_t i = 0; i < 5; ++i) {
        REQUIRE(lists.indices[90 + i] == i);
    }
    for (uint32_t f = 10; f < kClusterCount; ++f) {
        REQUIRE(lists.grid[f].count == kClusterTruncatedBit);
        REQUIRE(lists.grid[f].offset == 95);
    }
}

//======================================================================================================================
TEST_CASE("a froxel with no candidates is never truncated by global overflow",
          "[render][light-cluster]") {
    const auto view = makeView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.1f, 1280, 720, {0.0f, 0.0f});
    // One small light near the camera reaches a few froxels and leaves the rest empty.
    const std::vector<lmx::engine::LightRow> rows{makePoint({0.0f, 0.0f, -1.0f}, 0.5f)};
    auto params = makeParams(view, 1);
    params.globalCapacity = 3;
    const auto lists = buildLightClusters(rows, params);

    REQUIRE(lists.counters.candidates > 3);
    REQUIRE(lists.counters.assigned == 3);
    uint32_t empty = 0;
    for (const auto& record : lists.grid) {
        if ((record.count & ~kClusterTruncatedBit) == 0 &&
            (record.count & kClusterTruncatedBit) == 0) {
            ++empty;
        }
    }
    REQUIRE(empty == kClusterCount - lists.counters.truncatedFroxels - 3);
}

//======================================================================================================================
// Independent conservativeness: this case rebuilds the projection, unprojection and reach test in
// double precision from the projection's own coefficients and shares no code with the mirror
// beyond the tile and slice lookups, which are the contract the fragment itself uses.
TEST_CASE("every reaching light appears in its sampled point's froxel", "[render][light-cluster]") {
    Rng rng{0x9E3779B97F4A7C15ull};

    // The third extent divides neither 16 nor 9, so its samples include straddling pixels whose
    // centres lie past the tile's exact fractional edge.
    const TestView views[3] = {
        makeView({0.0f, 1.5f, 6.0f}, 0.0f, -0.1f, 0.1f, 1280, 720, {0.0f, 0.0f}),
        makeView({6.0f, 3.0f, -8.0f}, 1.1f, 0.25f, 0.05f, 960, 540, {0.0021f, -0.0013f}),
        makeView({-3.0f, 2.0f, 2.0f}, -0.7f, 0.05f, 0.08f, 1283, 721, {-0.0017f, 0.0009f})};

    // Pixel centres past 100 m, sampled on top of the random ones so the open slice is never
    // examined vacuously; the far lights below are placed on exactly these points.
    struct FarAnchor {
        glm::uvec2 pixel;
        float distance;
    };
    const FarAnchor anchors[4] = {
        {{200, 100}, 150.0f}, {{700, 300}, 210.0f}, {{1000, 500}, 270.0f}, {{400, 650}, 330.0f}};

    std::vector<lmx::engine::LightRow> rows;
    for (uint32_t i = 0; i < 200; ++i) {
        lmx::engine::LocalLight light;
        light.position = {float(nextRange(rng, -20.0, 20.0)), float(nextRange(rng, -6.0, 10.0)),
                          float(nextRange(rng, -40.0, 8.0))};
        light.range = float(nextRange(rng, 0.5, 30.0));
        if (i % 4 == 0) {
            light.type = lmx::engine::LocalLightType::Spot;
            const glm::vec3 direction{float(nextRange(rng, -1.0, 1.0)),
                                      float(nextRange(rng, -1.0, 1.0)),
                                      float(nextRange(rng, -1.0, 1.0))};
            light.direction = glm::length(direction) > 1e-3f ? glm::normalize(direction)
                                                             : glm::vec3(0.0f, -1.0f, 0.0f);
            light.innerCone = float(nextRange(rng, 0.05, 0.3));
            light.outerCone = light.innerCone + float(nextRange(rng, 0.05, 0.9));
        }
        const auto row = lmx::engine::makeLightRow(light);
        REQUIRE(row.has_value());
        rows.push_back(*row);
    }

    // Four lights beyond the open slice's near boundary, alternating a tiny point light sitting on
    // the anchor and a spot three metres in front of it aimed at it.
    const glm::mat4 viewToWorldFar = glm::inverse(views[0].camera.viewMatrix());
    for (uint32_t k = 0; k < 4; ++k) {
        const glm::vec3 viewPoint =
            pixelCentreViewPoint(views[0], anchors[k].pixel, anchors[k].distance);
        const glm::vec3 world{viewToWorldFar * glm::vec4(viewPoint, 1.0f)};
        lmx::engine::LocalLight light;
        if (k % 2 == 0) {
            light.position = world;
            light.range = 0.5f;
        } else {
            const glm::vec3 forward = glm::normalize(world - views[0].camera.position);
            light.type = lmx::engine::LocalLightType::Spot;
            light.position = world - forward * 3.0f;
            light.direction = forward;
            light.range = 5.0f;
            light.innerCone = 0.1f;
            light.outerCone = 0.3f;
        }
        const auto row = lmx::engine::makeLightRow(light);
        REQUIRE(row.has_value());
        rows.push_back(*row);
    }

    uint32_t openSliceReaching = 0;
    for (const auto& view : views) {
        auto params = makeParams(view, uint32_t(rows.size()));
        // The property is conservativeness, not capacity: give the run room so no cap can drop a
        // light, and pin that with the counters below.
        params.perClusterCap = uint32_t(rows.size());
        params.globalCapacity = kClusterCount * uint32_t(rows.size());
        const auto lists = buildLightClusters(rows, params);
        REQUIRE(lists.counters.droppedPerCluster == 0);
        REQUIRE(lists.counters.droppedGlobal == 0);

        const double p00 = double(view.projection[0][0]);
        const double p11 = double(view.projection[1][1]);
        const double jitterX = double(view.projection[2][0]);
        const double jitterY = double(view.projection[2][1]);
        const double nearZ = double(view.camera.nearZ);
        const glm::dmat4 viewToWorld = glm::inverse(glm::dmat4(view.camera.viewMatrix()));

        uint32_t reachingFound = 0;
        for (uint32_t sample = 0; sample < 4096 + 4; ++sample) {
            uint32_t px = 0;
            uint32_t py = 0;
            double distance = 0.0;
            if (sample < 4096) {
                px = uint32_t(nextRange(rng, 0.0, double(view.width)));
                py = uint32_t(nextRange(rng, 0.0, double(view.height)));
                distance = std::exp(nextRange(rng, std::log(nearZ * 1.5), std::log(400.0)));
            } else {
                const auto& anchor = anchors[sample - 4096];
                px = std::min(anchor.pixel.x, view.width - 1);
                py = std::min(anchor.pixel.y, view.height - 1);
                distance = double(anchor.distance);
            }
            const double ndcX = (double(px) + 0.5) / double(view.width) * 2.0 - 1.0;
            const double ndcY = 1.0 - (double(py) + 0.5) / double(view.height) * 2.0;

            const glm::dvec4 viewPoint{(ndcX + jitterX) * distance / p00,
                                       (ndcY + jitterY) * distance / p11, -distance, 1.0};
            const glm::dvec3 world{viewToWorld * viewPoint};

            const glm::uvec2 tile = clusterTile({px, py}, {0, 0}, {view.width, view.height});
            const uint32_t slice = clusterSlice(float(nearZ / distance), params.sliceDepth);
            const auto& record = lists.grid[froxelIndex(tile, slice)];
            REQUIRE((record.count & kClusterTruncatedBit) == 0);

            for (uint32_t index = 0; index < rows.size(); ++index) {
                const auto& row = rows[index];
                const glm::dvec3 toLight = glm::dvec3(row.position) - world;
                const double d = glm::length(toLight);
                if (!(d < double(row.range))) {
                    continue;
                }
                const glm::dvec3 lightVec = toLight / std::max(d, 1e-12);
                const double cosTheta = glm::dot(-lightVec, glm::dvec3(row.direction));
                const double cone =
                    std::clamp(cosTheta * double(row.spotScale) + double(row.spotOffset), 0.0, 1.0);
                if (!(cone > 0.0)) {
                    continue;
                }
                bool listed = false;
                for (uint32_t i = 0; i < record.count; ++i) {
                    listed = listed || lists.indices[record.offset + i] == index;
                }
                REQUIRE(listed);
                ++reachingFound;
                if (slice == kClusterSliceCount - 1) {
                    ++openSliceReaching;
                }
            }
        }
        // The sampling must actually exercise the predicate.
        REQUIRE(reachingFound > 100);
    }
    // The open slice's check must never go vacuous: the far anchors guarantee it runs.
    REQUIRE(openSliceReaching >= 4);
}
