#include "App/Model/Graph/GraphSnapshot.h"

#include "App/Model/Graph/GraphNodeModel.h"
#include "Render/Graph/GraphDump.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::app;

namespace {

//======================================================================================================================
RetainedFrame measuredFrame(uint64_t frameId, std::string label, double milliseconds) {
    RetainedFrame frame;
    frame.record.frameId = frameId;
    frame.record.debug.passes.push_back({.label = label});
    frame.record.debug.schedule.passes = {0};
    frame.timings = {{label, milliseconds}};
    frame.metrics = FrameMetricsMetadata{.contextEpoch = frameId};
    frame.timed = true;
    return frame;
}

} // namespace

//======================================================================================================================
TEST_CASE("graph publication owns one exact frame and changes topology only at four Hz", "[app]") {
    GraphSnapshot snapshot;
    snapshot.update(100.0, nullptr);
    snapshot.freeze();
    REQUIRE_FALSE(snapshot.frozen());
    REQUIRE(snapshot.displayed() == nullptr);

    auto first = measuredFrame(7, "pass.original", 1.25);
    snapshot.update(100.0, &first);
    const std::string firstDump = render::dumpCompiledFrame(snapshot.displayed()->record);
    auto second = measuredFrame(8, "pass.changedTopology", 9.0);
    snapshot.update(100.125, &second);
    first.record.debug.passes.clear();
    first.timings.clear();
    first.metrics.reset();
    REQUIRE(render::dumpCompiledFrame(snapshot.displayed()->record) == firstDump);
    REQUIRE(snapshot.displayed()->record.frameId == 7);
    REQUIRE(snapshot.displayed()->metrics->contextEpoch == 7);
    REQUIRE(snapshot.displayed()->timings.front().gpuMilliseconds == 1.25);

    snapshot.update(100.25, &second);
    const auto model =
        buildGraphNodeModel(snapshot.displayed()->record, snapshot.displayed()->timings);
    REQUIRE(model.frameId == 8);
    REQUIRE(model.nodes.front().label == "pass.changedTopology");
    REQUIRE(model.nodes.front().gpuMilliseconds == 9.0);
    REQUIRE(snapshot.displayed()->metrics->contextEpoch == 8);
    auto third = measuredFrame(9, "pass.latest", 0.0);
    snapshot.update(100.375, &third);
    REQUIRE(snapshot.displayed()->record.frameId == 8);
    snapshot.update(100.5, &third);
    REQUIRE(snapshot.displayed()->record.frameId == 9);
    REQUIRE(snapshot.displayed()->timings.front().gpuMilliseconds == 0.0);
}

//======================================================================================================================
TEST_CASE("graph freeze keeps the displayed publication through eviction and resumes immediately",
          "[app]") {
    FrameRecordRing ring;
    auto first = measuredFrame(7, "pass.first", 1.25);
    ring.retain(first.record, first.metrics);
    REQUIRE(ring.joinTimings(7, first.timings));
    GraphSnapshot snapshot;
    snapshot.update(0.0, ring.newestTimedFrame());
    const std::string frozenDump = render::dumpCompiledFrame(snapshot.displayed()->record);
    for (uint64_t frameId = 8; frameId < 20; ++frameId) {
        auto frame = measuredFrame(frameId, "pass.otherScene", 5.0);
        ring.retain(frame.record, frame.metrics);
        REQUIRE(ring.joinTimings(frameId, frame.timings));
    }
    REQUIRE(ring.find(7) == nullptr);
    snapshot.update(0.125, ring.newestTimedFrame());
    snapshot.freeze();
    snapshot.update(2.0, ring.newestTimedFrame());
    REQUIRE(snapshot.frozen());
    REQUIRE(snapshot.displayed()->record.frameId == 7);
    REQUIRE(snapshot.displayed()->timings.front().gpuMilliseconds == 1.25);
    REQUIRE(render::dumpCompiledFrame(snapshot.displayed()->record) == frozenDump);
    snapshot.resume();
    REQUIRE_FALSE(snapshot.frozen());
    REQUIRE(snapshot.displayed() == nullptr);
    snapshot.update(2.0, ring.newestTimedFrame());
    REQUIRE(snapshot.displayed()->record.frameId == 19);
    REQUIRE(snapshot.displayed()->timings.front().gpuMilliseconds == 5.0);
    snapshot.freeze();
    snapshot.resume();
    snapshot.update(2.125, nullptr);
    REQUIRE(snapshot.displayed() == nullptr);
    snapshot.update(2.125, ring.newestTimedFrame());
    REQUIRE(snapshot.displayed()->record.frameId == 19);
}

//======================================================================================================================
TEST_CASE("graph publication only exposes joined timings when the whole frame publishes", "[app]") {
    FrameRecordRing ring;
    auto frame = measuredFrame(20, "pass.measuredZero", 0.0);
    ring.retain(frame.record);
    REQUIRE_FALSE(ring.joinTimings(21, frame.timings));
    GraphSnapshot snapshot;
    snapshot.update(0.0, ring.find(20));
    REQUIRE_FALSE(snapshot.displayed()->timed);
    REQUIRE(snapshot.displayed()->timings.empty());
    REQUIRE(ring.joinTimings(20, frame.timings));
    snapshot.update(0.125, ring.find(20));
    REQUIRE_FALSE(snapshot.displayed()->timed);
    REQUIRE(snapshot.displayed()->timings.empty());
    snapshot.update(0.25, ring.find(20));
    REQUIRE(snapshot.displayed()->timed);
    REQUIRE(snapshot.displayed()->timings.front().gpuMilliseconds == 0.0);

    frame.record.frameId = 21;
    frame.timed = false;
    snapshot.update(0.5, &frame);
    REQUIRE_FALSE(snapshot.displayed()->timed);
    REQUIRE(snapshot.displayed()->timings.empty());
}
