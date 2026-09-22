//----------------------------------------------------------------------------------------------------------------------
/// @file AppPassTimingHistoryTests.cpp
/// @brief Tests the editor's rolling GPU pass-timing summaries.
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "App/Model/Performance/PassTimingHistory.h"

#include <array>
#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::app;

//======================================================================================================================
TEST_CASE("pass timing history summarizes retired frames without sampling one twice", "[app]") {
    PassTimingHistory history;
    const std::array first = {rojoRHI::PassTiming{.label = "shadow", .gpuMilliseconds = 0.02},
                              rojoRHI::PassTiming{.label = "scene", .gpuMilliseconds = 0.20}};
    const std::array second = {rojoRHI::PassTiming{.label = "shadow", .gpuMilliseconds = 0.04},
                               rojoRHI::PassTiming{.label = "scene", .gpuMilliseconds = 0.40}};

    REQUIRE(history.addFrame(7, first));
    REQUIRE_FALSE(history.addFrame(7, second));
    REQUIRE_FALSE(history.addFrame(8, second));

    const std::vector<PassTimingSummary> rows = history.summaries();
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].label == "shadow");
    REQUIRE(rows[0].sampleCount == 2);
    REQUIRE(rows[0].averageGpuMilliseconds == Catch::Approx(0.03));
    REQUIRE(rows[0].latestGpuMilliseconds == 0.04);
    REQUIRE(rows[0].minimumGpuMilliseconds == 0.02);
    REQUIRE(rows[0].maximumGpuMilliseconds == 0.04);
    REQUIRE(rows[1].averageGpuMilliseconds == Catch::Approx(0.30));
}

//======================================================================================================================
// Labels are deliberately duplicated: schedule position, not diagnostic text, keeps the two pass
// series distinct.
TEST_CASE("pass timing history preserves duplicate labels by schedule position", "[app]") {
    PassTimingHistory history;
    const std::array first = {rojoRHI::PassTiming{.label = "duplicate", .gpuMilliseconds = 0.1},
                              rojoRHI::PassTiming{.label = "duplicate", .gpuMilliseconds = 0.9}};
    const std::array second = {rojoRHI::PassTiming{.label = "duplicate", .gpuMilliseconds = 0.3},
                               rojoRHI::PassTiming{.label = "duplicate", .gpuMilliseconds = 0.7}};

    history.addFrame(1, first);
    history.addFrame(2, second);

    const std::vector<PassTimingSummary> rows = history.summaries();
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].averageGpuMilliseconds == Catch::Approx(0.2));
    REQUIRE(rows[1].averageGpuMilliseconds == Catch::Approx(0.8));
}

//======================================================================================================================
TEST_CASE("pass timing history resets when the compiled schedule changes", "[app]") {
    PassTimingHistory history;
    const std::array original = {rojoRHI::PassTiming{.label = "scene", .gpuMilliseconds = 2.0},
                                 rojoRHI::PassTiming{.label = "display", .gpuMilliseconds = 4.0}};
    const std::array changed = {rojoRHI::PassTiming{.label = "scene", .gpuMilliseconds = 0.2},
                                rojoRHI::PassTiming{.label = "bloom", .gpuMilliseconds = 0.4},
                                rojoRHI::PassTiming{.label = "display", .gpuMilliseconds = 0.6}};

    history.addFrame(1, original);
    REQUIRE(history.addFrame(2, changed));

    const std::vector<PassTimingSummary> rows = history.summaries();
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].sampleCount == 1);
    REQUIRE(rows[0].averageGpuMilliseconds == 0.2);
    REQUIRE(rows[1].label == "bloom");
    REQUIRE(rows[2].averageGpuMilliseconds == 0.6);
}

//======================================================================================================================
TEST_CASE("pass timing history evicts samples beyond its rolling capacity", "[app]") {
    PassTimingHistory history;
    for (uint64_t frame = 1; frame <= PassTimingHistory::kSampleCapacity + 1; ++frame) {
        const std::array timing = {
            rojoRHI::PassTiming{.label = "scene", .gpuMilliseconds = static_cast<double>(frame)}};
        history.addFrame(frame, timing);
    }

    const std::vector<PassTimingSummary> rows = history.summaries();
    const PassTimingSummary& row = rows.front();
    REQUIRE(row.sampleCount == PassTimingHistory::kSampleCapacity);
    REQUIRE(row.minimumGpuMilliseconds == 2.0);
    REQUIRE(row.maximumGpuMilliseconds == 61.0);
    REQUIRE(row.latestGpuMilliseconds == 61.0);
    REQUIRE(row.averageGpuMilliseconds == 31.5);
}

//======================================================================================================================
TEST_CASE("pass cost sorting preserves source identities and deterministic schedule ties",
          "[app]") {
    const std::array rows = {PassTimingSummary{.label = "duplicate",
                                               .averageGpuMilliseconds = 1.0,
                                               .latestGpuMilliseconds = 9.0,
                                               .sampleCount = 2},
                             PassTimingSummary{.label = "expensive",
                                               .averageGpuMilliseconds = 4.0,
                                               .latestGpuMilliseconds = 2.0,
                                               .sampleCount = 4},
                             PassTimingSummary{.label = "duplicate",
                                               .averageGpuMilliseconds = 4.0,
                                               .latestGpuMilliseconds = 1.0,
                                               .sampleCount = 3}};
    REQUIRE(sortedPassTimingIndices(rows, PassTimingSort::Average) == std::vector<size_t>{1, 2, 0});
    REQUIRE(sortedPassTimingIndices(rows, PassTimingSort::Average, false) ==
            std::vector<size_t>{0, 1, 2});
    REQUIRE(sortedPassTimingIndices(rows, PassTimingSort::Latest) == std::vector<size_t>{0, 1, 2});
    REQUIRE(sortedPassTimingIndices(rows, PassTimingSort::Samples) == std::vector<size_t>{1, 2, 0});
    REQUIRE(sortedPassTimingIndices(rows, PassTimingSort::Schedule) ==
            std::vector<size_t>{0, 1, 2});
    REQUIRE(rows[0].label == "duplicate");
    REQUIRE(rows[0].latestGpuMilliseconds == 9.0);
}
