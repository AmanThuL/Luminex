//----------------------------------------------------------------------------------------------------------------------
/// @file AppPerformanceModelTests.cpp
/// @brief Tests the editor's coherent, pausable, clearable performance snapshot model.
//----------------------------------------------------------------------------------------------------------------------

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "App/PerformanceModel.h"

#include <array>
#include <vector>

using namespace lmx;
using namespace lmx::app;

namespace {

//======================================================================================================================
PerformanceFrameSample sampleFor(uint64_t frameId, std::span<const rhi::PassTiming> timings,
                                 uint32_t objectCount = 1, uint32_t drawCount = 1,
                                 uint32_t viewportWidth = 800, uint32_t viewportHeight = 600,
                                 uint32_t sceneTargetWidth = 1600,
                                 uint32_t sceneTargetHeight = 1200, uint64_t requested = 100,
                                 uint64_t highWater = 80, uint64_t aliasSavings = 20) {
    return PerformanceFrameSample{.frameId = frameId,
                                  .timings = timings,
                                  .objectCount = objectCount,
                                  .drawCount = drawCount,
                                  .viewportLogicalWidth = viewportWidth,
                                  .viewportLogicalHeight = viewportHeight,
                                  .sceneTargetPixelWidth = sceneTargetWidth,
                                  .sceneTargetPixelHeight = sceneTargetHeight,
                                  .transientRequestedBytes = requested,
                                  .transientHighWaterBytes = highWater,
                                  .transientAliasSavingsBytes = aliasSavings};
}

} // namespace

//======================================================================================================================
TEST_CASE("a fresh performance model reports a waiting snapshot", "[app]") {
    PerformanceModel model;

    const PerformanceSnapshot& snapshot = model.snapshot();
    REQUIRE(snapshot.waitingForSamples);
    REQUIRE(snapshot.passRows.empty());
    REQUIRE(snapshot.frameIntervalsMs.empty());
    REQUIRE(snapshot.frameId == 0);
}

//======================================================================================================================
// Below the 0.25 s republish threshold the snapshot must not move at all; once accumulated ticks
// cross it, every collected sample publishes at once.
TEST_CASE("performance model republishes on a 0.25 s cadence and is stable between ticks",
          "[app]") {
    PerformanceModel model;

    model.tick(0.1f, nullptr);
    REQUIRE(model.snapshot().frameIntervalsMs.empty());
    model.tick(0.1f, nullptr);
    REQUIRE(model.snapshot().frameIntervalsMs.empty());
    model.tick(0.1f, nullptr);

    const PerformanceSnapshot& snapshot = model.snapshot();
    REQUIRE(snapshot.frameIntervalsMs.size() == 3);
    REQUIRE(snapshot.latestFrameIntervalMs == Catch::Approx(100.0f));
    REQUIRE(snapshot.framesPerSecond == Catch::Approx(10.0f));
}

//======================================================================================================================
TEST_CASE("performance model preserves the frame-interval rolling capacity", "[app]") {
    PerformanceModel model;

    for (size_t i = 0; i < PerformanceModel::kFrameIntervalCapacity + 10; ++i) {
        model.tick(0.3f, nullptr);
    }

    REQUIRE(model.snapshot().frameIntervalsMs.size() == PerformanceModel::kFrameIntervalCapacity);
}

//======================================================================================================================
TEST_CASE("performance model rolls up retired-frame pass timings without sampling one twice",
          "[app]") {
    PerformanceModel model;
    const std::array first = {rhi::PassTiming{.label = "shadow", .gpuMilliseconds = 0.02},
                              rhi::PassTiming{.label = "scene", .gpuMilliseconds = 0.20}};
    const std::array second = {rhi::PassTiming{.label = "shadow", .gpuMilliseconds = 0.04},
                               rhi::PassTiming{.label = "scene", .gpuMilliseconds = 0.40}};

    const PerformanceFrameSample sampleA = sampleFor(7, first);
    model.tick(0.3f, &sampleA);
    const PerformanceFrameSample sampleB = sampleFor(8, second);
    model.tick(0.3f, &sampleB);

    const std::vector<PassTimingSummary>& rows = model.snapshot().passRows;
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].label == "shadow");
    REQUIRE(rows[0].sampleCount == 2);
    REQUIRE(rows[0].averageGpuMilliseconds == Catch::Approx(0.03));
    REQUIRE(rows[1].averageGpuMilliseconds == Catch::Approx(0.30));
}

//======================================================================================================================
// Labels are deliberately duplicated: schedule position, not diagnostic text, keeps the two series
// distinct, exactly as PassTimingHistory guarantees on its own.
TEST_CASE("performance model preserves duplicate labels by schedule position", "[app]") {
    PerformanceModel model;
    const std::array first = {rhi::PassTiming{.label = "duplicate", .gpuMilliseconds = 0.1},
                              rhi::PassTiming{.label = "duplicate", .gpuMilliseconds = 0.9}};
    const std::array second = {rhi::PassTiming{.label = "duplicate", .gpuMilliseconds = 0.3},
                               rhi::PassTiming{.label = "duplicate", .gpuMilliseconds = 0.7}};

    const PerformanceFrameSample sampleA = sampleFor(1, first);
    model.tick(0.3f, &sampleA);
    const PerformanceFrameSample sampleB = sampleFor(2, second);
    model.tick(0.3f, &sampleB);

    const std::vector<PassTimingSummary>& rows = model.snapshot().passRows;
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].averageGpuMilliseconds == Catch::Approx(0.2));
    REQUIRE(rows[1].averageGpuMilliseconds == Catch::Approx(0.8));
}

//======================================================================================================================
TEST_CASE("performance model resets every series when the compiled schedule changes", "[app]") {
    PerformanceModel model;
    const std::array original = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 2.0},
                                 rhi::PassTiming{.label = "display", .gpuMilliseconds = 4.0}};
    const std::array changed = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 0.2},
                                rhi::PassTiming{.label = "bloom", .gpuMilliseconds = 0.4},
                                rhi::PassTiming{.label = "display", .gpuMilliseconds = 0.6}};

    const PerformanceFrameSample sampleA = sampleFor(1, original);
    model.tick(0.3f, &sampleA);
    const PerformanceFrameSample sampleB = sampleFor(2, changed);
    model.tick(0.3f, &sampleB);

    const std::vector<PassTimingSummary>& rows = model.snapshot().passRows;
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].sampleCount == 1);
    REQUIRE(rows[1].label == "bloom");
    REQUIRE(rows[2].averageGpuMilliseconds == Catch::Approx(0.6));
}

//======================================================================================================================
TEST_CASE("performance model evicts pass samples beyond the rolling capacity", "[app]") {
    PerformanceModel model;
    for (uint64_t frame = 1; frame <= PassTimingHistory::kSampleCapacity + 1; ++frame) {
        const std::array timing = {
            rhi::PassTiming{.label = "scene", .gpuMilliseconds = static_cast<double>(frame)}};
        const PerformanceFrameSample sample = sampleFor(frame, timing);
        model.tick(0.3f, &sample);
    }

    const PassTimingSummary& row = model.snapshot().passRows.front();
    REQUIRE(row.sampleCount == PassTimingHistory::kSampleCapacity);
    REQUIRE(row.minimumGpuMilliseconds == 2.0);
    REQUIRE(row.maximumGpuMilliseconds == PassTimingHistory::kSampleCapacity + 1);
}

//======================================================================================================================
// A repeated or regressing frame id is ignored in full: neither its timings nor the counts,
// resolution, or transient bytes that came with it take effect.
TEST_CASE("performance model ignores repeated or regressing frame ids in full", "[app]") {
    PerformanceModel model;
    const std::array timingsA = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 1.0}};
    const PerformanceFrameSample sampleA = sampleFor(5, timingsA, /*objectCount=*/3);
    model.tick(0.3f, &sampleA);
    const PerformanceSnapshot afterFirst = model.snapshot();

    const std::array timingsRepeat = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 99.0}};
    const PerformanceFrameSample repeat = sampleFor(5, timingsRepeat, /*objectCount=*/77);
    model.tick(0.3f, &repeat);
    const PerformanceFrameSample regressed = sampleFor(3, timingsRepeat, /*objectCount=*/88);
    model.tick(0.3f, &regressed);

    const PerformanceSnapshot& after = model.snapshot();
    REQUIRE(after.frameId == afterFirst.frameId);
    REQUIRE(after.objectCount == afterFirst.objectCount);
    REQUIRE(after.passRows[0].sampleCount == afterFirst.passRows[0].sampleCount);
    REQUIRE(after.passRows[0].averageGpuMilliseconds ==
            Catch::Approx(afterFirst.passRows[0].averageGpuMilliseconds));
}

//======================================================================================================================
TEST_CASE("performance model's timed pass sum adds the displayed pass averages", "[app]") {
    PerformanceModel model;
    const std::array timings = {rhi::PassTiming{.label = "shadow", .gpuMilliseconds = 0.5},
                                rhi::PassTiming{.label = "scene", .gpuMilliseconds = 1.5},
                                rhi::PassTiming{.label = "display", .gpuMilliseconds = 0.25}};
    const PerformanceFrameSample sample = sampleFor(1, timings);
    model.tick(0.3f, &sample);

    const PerformanceSnapshot& snapshot = model.snapshot();
    double expected = 0.0;
    for (const PassTimingSummary& row : snapshot.passRows) {
        expected += row.averageGpuMilliseconds;
    }
    REQUIRE(snapshot.timedPassSumMilliseconds == Catch::Approx(expected));
    REQUIRE(snapshot.timedPassSumMilliseconds == Catch::Approx(2.25));
}

//======================================================================================================================
// Counts, resolution, and transient bytes must name the same frame as the pass rows currently
// displayed -- never a mix of an old frame's context with a newer frame's timings, or vice versa.
TEST_CASE("performance model joins counts, resolution, and transient bytes to their frame",
          "[app]") {
    PerformanceModel model;
    const std::array timingsA = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 1.0}};
    const PerformanceFrameSample sampleA =
        sampleFor(1, timingsA, /*objectCount=*/5, /*drawCount=*/6, /*viewportWidth=*/800,
                  /*viewportHeight=*/600, /*sceneTargetWidth=*/1600, /*sceneTargetHeight=*/1200,
                  /*requested=*/1000, /*highWater=*/800, /*aliasSavings=*/200);
    model.tick(0.3f, &sampleA);

    const std::array timingsB = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 3.0}};
    const PerformanceFrameSample sampleB =
        sampleFor(2, timingsB, /*objectCount=*/9, /*drawCount=*/11, /*viewportWidth=*/1024,
                  /*viewportHeight=*/768, /*sceneTargetWidth=*/2048, /*sceneTargetHeight=*/1536,
                  /*requested=*/4000, /*highWater=*/2500, /*aliasSavings=*/1500);
    model.tick(0.3f, &sampleB);

    const PerformanceSnapshot& snapshot = model.snapshot();
    REQUIRE(snapshot.frameId == 2);
    REQUIRE(snapshot.objectCount == 9);
    REQUIRE(snapshot.drawCount == 11);
    REQUIRE(snapshot.viewportLogicalWidth == 1024);
    REQUIRE(snapshot.viewportLogicalHeight == 768);
    REQUIRE(snapshot.sceneTargetPixelWidth == 2048);
    REQUIRE(snapshot.sceneTargetPixelHeight == 1536);
    REQUIRE(snapshot.transientRequestedBytes == 4000);
    REQUIRE(snapshot.transientHighWaterBytes == 2500);
    REQUIRE(snapshot.transientAliasSavingsBytes == 1500);
    REQUIRE(snapshot.passRows[0].latestGpuMilliseconds == 3.0);
}

//======================================================================================================================
TEST_CASE("performance model freezes every field together while paused", "[app]") {
    PerformanceModel model;
    const std::array timingsA = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 1.0}};
    const PerformanceFrameSample sampleA = sampleFor(1, timingsA, /*objectCount=*/5);
    model.tick(0.3f, &sampleA);
    const PerformanceSnapshot before = model.snapshot();
    REQUIRE_FALSE(before.waitingForSamples);

    model.setPaused(true);
    REQUIRE(model.paused());

    const std::array timingsB = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 9.0}};
    const PerformanceFrameSample sampleB = sampleFor(2, timingsB, /*objectCount=*/999);
    model.tick(0.3f, &sampleB);
    model.tick(0.3f, &sampleB);
    model.tick(0.3f, nullptr);

    const PerformanceSnapshot& frozen = model.snapshot();
    REQUIRE(frozen.frameId == before.frameId);
    REQUIRE(frozen.objectCount == before.objectCount);
    REQUIRE(frozen.passRows.size() == before.passRows.size());
    REQUIRE(frozen.passRows[0].averageGpuMilliseconds ==
            Catch::Approx(before.passRows[0].averageGpuMilliseconds));
    REQUIRE(frozen.frameIntervalsMs.size() == before.frameIntervalsMs.size());
    REQUIRE(frozen.latestFrameIntervalMs == before.latestFrameIntervalMs);
}

//======================================================================================================================
TEST_CASE("performance model resume publishes one internally consistent current snapshot",
          "[app]") {
    PerformanceModel model;
    const std::array timingsA = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 1.0}};
    const PerformanceFrameSample sampleA = sampleFor(1, timingsA, /*objectCount=*/5);
    model.tick(0.3f, &sampleA);

    model.setPaused(true);
    model.setPaused(false);
    REQUIRE_FALSE(model.paused());

    // Resuming without new data is still coherent: nothing was collected while paused, so the
    // republished snapshot matches what was frozen -- never a stale row paired with fresh context.
    const PerformanceSnapshot resumedIdle = model.snapshot();
    REQUIRE(resumedIdle.frameId == 1);
    REQUIRE(resumedIdle.objectCount == 5);

    // A new sample after resume updates every field of the snapshot together.
    const std::array timingsB = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 9.0}};
    const PerformanceFrameSample sampleB = sampleFor(2, timingsB, /*objectCount=*/42);
    model.tick(0.3f, &sampleB);

    const PerformanceSnapshot& resumed = model.snapshot();
    REQUIRE(resumed.frameId == 2);
    REQUIRE(resumed.objectCount == 42);
    REQUIRE(resumed.passRows[0].latestGpuMilliseconds == 9.0);
}

//======================================================================================================================
TEST_CASE("performance model clears both histories and reports waiting until the next sample",
          "[app]") {
    PerformanceModel model;
    const std::array timingsA = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 1.0}};
    const PerformanceFrameSample sampleA = sampleFor(1, timingsA);
    model.tick(0.3f, &sampleA);
    REQUIRE_FALSE(model.snapshot().waitingForSamples);
    REQUIRE_FALSE(model.snapshot().frameIntervalsMs.empty());

    model.clearHistory();

    const PerformanceSnapshot& cleared = model.snapshot();
    REQUIRE(cleared.waitingForSamples);
    REQUIRE(cleared.passRows.empty());
    REQUIRE(cleared.frameIntervalsMs.empty());
    REQUIRE(cleared.frameId == 0);

    // The frame-id sequence also restarts, so a sample that would have regressed against the
    // pre-clear history is accepted again.
    const std::array timingsAgain = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 2.0}};
    const PerformanceFrameSample sampleAgain = sampleFor(1, timingsAgain);
    model.tick(0.3f, &sampleAgain);

    const PerformanceSnapshot& reseeded = model.snapshot();
    REQUIRE_FALSE(reseeded.waitingForSamples);
    REQUIRE(reseeded.frameId == 1);
    REQUIRE(reseeded.passRows[0].sampleCount == 1);
}

//======================================================================================================================
TEST_CASE("performance model clear takes effect immediately even while paused", "[app]") {
    PerformanceModel model;
    const std::array timings = {rhi::PassTiming{.label = "scene", .gpuMilliseconds = 1.0}};
    const PerformanceFrameSample sample = sampleFor(1, timings);
    model.tick(0.3f, &sample);
    model.setPaused(true);

    model.clearHistory();

    const PerformanceSnapshot& snapshot = model.snapshot();
    REQUIRE(model.paused());
    REQUIRE(snapshot.waitingForSamples);
    REQUIRE(snapshot.passRows.empty());
    REQUIRE(snapshot.frameIntervalsMs.empty());
}
