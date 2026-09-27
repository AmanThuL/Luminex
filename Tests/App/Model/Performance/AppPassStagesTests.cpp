//----------------------------------------------------------------------------------------------------------------------
/// @file AppPassStagesTests.cpp
/// @brief Tests additive GPU stage grouping and stable cost ordering.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Performance/PassStages.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lmx::app;

//======================================================================================================================
TEST_CASE("pass stages keep renderer families and preserve foreign labels", "[app][pass-stages]") {
    REQUIRE(passStage("lmx.pass.bloom.downsample2") == "bloom");
    REQUIRE(passStage("lmx.pass.scene") == "scene");
    REQUIRE(passStage("lmx.pass.temporal.vendor.pack") == "temporal");
    REQUIRE(passStage("external.draw") == "external.draw");
    REQUIRE(passStage("lmx.pass.") == "lmx.pass.");
    REQUIRE(passStage("").empty());
}

//======================================================================================================================
TEST_CASE("pass stage grouping sums costs and retains every schedule member",
          "[app][pass-stages]") {
    const std::array<PassTimingSummary, 5> passes{
        {{.label = "lmx.pass.bloom.downsample2",
          .averageGpuMilliseconds = 1.5,
          .latestGpuMilliseconds = 2.0},
         {.label = "lmx.pass.scene", .averageGpuMilliseconds = 4.0, .latestGpuMilliseconds = 5.0},
         {.label = "lmx.pass.bloom.upsample",
          .averageGpuMilliseconds = 2.5,
          .latestGpuMilliseconds = 3.0},
         {.label = "foreign.label", .averageGpuMilliseconds = 0.5, .latestGpuMilliseconds = 1.0},
         {.label = "lmx.pass.bloom.upsample",
          .averageGpuMilliseconds = 1.0,
          .latestGpuMilliseconds = 1.5}}};
    const auto groups = groupPassStages(passes);
    REQUIRE(groups.size() == 3);
    REQUIRE(groups[0].stage == "bloom");
    REQUIRE(groups[0].averageMs == Catch::Approx(5.0));
    REQUIRE(groups[0].latestMs == Catch::Approx(6.5));
    REQUIRE(groups[0].firstSchedule == 0);
    REQUIRE(groups[0].members == std::vector<size_t>{0, 2, 4});
    REQUIRE(groups[1].stage == "scene");
    REQUIRE(groups[1].firstSchedule == 1);
    REQUIRE(groups[2].stage == "foreign.label");
    REQUIRE(groups[2].firstSchedule == 3);
    REQUIRE(groupPassStages({}).empty());
}

//======================================================================================================================
TEST_CASE("stage costs sort stably while schedule order remains available", "[app][pass-stages]") {
    const std::array<StageTimingRow, 4> rows{
        {{.stage = "scene", .averageMs = 2, .latestMs = 3, .firstSchedule = 0, .members = {0}},
         {.stage = "bloom", .averageMs = 5, .latestMs = 2, .firstSchedule = 1, .members = {1}},
         {.stage = "temporal", .averageMs = 5, .latestMs = 2, .firstSchedule = 2, .members = {2}},
         {.stage = "display", .averageMs = 1, .latestMs = 4, .firstSchedule = 3, .members = {3}}}};
    REQUIRE(sortedStageTimingIndices(rows, StageTimingSort::Average) ==
            std::vector<size_t>{1, 2, 0, 3});
    REQUIRE(sortedStageTimingIndices(rows, StageTimingSort::Average, false) ==
            std::vector<size_t>{3, 0, 1, 2});
    REQUIRE(sortedStageTimingIndices(rows, StageTimingSort::Latest) ==
            std::vector<size_t>{3, 0, 1, 2});
    REQUIRE(sortedStageTimingIndices(rows, StageTimingSort::Schedule, false) ==
            std::vector<size_t>{0, 1, 2, 3});
    REQUIRE(sortedStageTimingIndices(rows, StageTimingSort::Schedule, true) ==
            std::vector<size_t>{3, 2, 1, 0});
    REQUIRE(rows[0].stage == "scene");
}
