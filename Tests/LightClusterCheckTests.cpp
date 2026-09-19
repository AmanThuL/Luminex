//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterCheckTests.cpp
/// @brief Pins exact list checking, including lengths, offsets and truncated records.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/LightClusterCheck.h"

#include <catch2/catch_test_macros.hpp>

using namespace lmx::render;

//======================================================================================================================
TEST_CASE("light cluster check distinguishes grid, index and counter failures",
          "[render][light-check]") {
    LightClusterLists expected;
    expected.grid = {{0, 2}, {2, kClusterTruncatedBit | 1}, {3, 0}};
    expected.indices = {0, 3, 2};
    expected.counters = {5, 3, 1, 1, 1, 2};
    auto actual = expected;
    SECTION("equal") {
        CHECK(checkLightClusters(expected, actual.grid, actual.indices, actual.counters).passed());
    }
    SECTION("one differing index") {
        actual.indices[1] = 4;
        const auto result =
            checkLightClusters(expected, actual.grid, actual.indices, actual.counters);
        CHECK_FALSE(result.passed());
        CHECK(result.indexMismatches == 1);
        CHECK(result.gridMismatches == 0);
        CHECK(result.counterMismatches == 0);
    }
    SECTION("each counter field participates") {
        auto checkCounter = [&](LightClusterCounters counts) {
            const auto result = checkLightClusters(expected, actual.grid, actual.indices, counts);
            CHECK_FALSE(result.passed());
            CHECK(result.counterMismatches == 1);
        };
        auto counts = actual.counters;
        ++counts.candidates;
        checkCounter(counts);
        counts = actual.counters;
        ++counts.assigned;
        checkCounter(counts);
        counts = actual.counters;
        ++counts.droppedPerCluster;
        checkCounter(counts);
        counts = actual.counters;
        ++counts.droppedGlobal;
        checkCounter(counts);
        counts = actual.counters;
        ++counts.truncatedFroxels;
        checkCounter(counts);
        counts = actual.counters;
        ++counts.maxCount;
        checkCounter(counts);
    }
    SECTION("offset on empty record is significant") {
        ++actual.grid.back().offset;
        CHECK(checkLightClusters(expected, actual.grid, actual.indices, actual.counters)
                  .gridMismatches == 1);
    }
    SECTION("truncated bit is significant") {
        actual.grid[1].count &= ~kClusterTruncatedBit;
        CHECK(checkLightClusters(expected, actual.grid, actual.indices, actual.counters)
                  .gridMismatches == 1);
    }
    SECTION("missing and extra entries count separately") {
        actual.grid.pop_back();
        actual.indices.push_back(6);
        const auto result =
            checkLightClusters(expected, actual.grid, actual.indices, actual.counters);
        CHECK(result.gridMismatches == 1);
        CHECK(result.indexMismatches == 1);
        CHECK_FALSE(result.passed());
    }
}
