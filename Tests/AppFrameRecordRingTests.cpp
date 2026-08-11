#include <catch2/catch_test_macros.hpp>

#include "App/FrameRecordRing.h"

#include <array>
#include <string>
#include <vector>

using namespace lmx;
using namespace lmx::app;

namespace {

//======================================================================================================================
// A record carries a frame number and whatever compilation decided; only the number takes part in
// retention and the join, so a pass label is enough payload to tell one record from another.
render::CompiledFrameRecord recordFor(uint64_t frameId, std::string label) {
    render::CompiledFrameRecord record;
    record.frameId = frameId;
    record.debug.passes.push_back({.label = std::move(label),
                                   .kind = render::PassKind::Raster,
                                   .uses = {},
                                   .cullReason = {}});
    return record;
}

} // namespace

//======================================================================================================================
TEST_CASE("a fresh ring reports no frame at all", "[app]") {
    FrameRecordRing ring;

    REQUIRE(ring.size() == 0);
    REQUIRE(ring.newestTimedFrame() == nullptr);
    REQUIRE(ring.find(1) == nullptr);
}

//======================================================================================================================
// The capacity has to outlast the in-flight window, because the timings of a frame are published
// after it retires: the case walks past the capacity and asserts the *oldest* frame is the one that
// went, not the newest.
TEST_CASE("the ring keeps its newest frames and evicts the oldest", "[app]") {
    FrameRecordRing ring;

    for (uint64_t frame = 1; frame <= FrameRecordRing::kCapacity + 2; ++frame) {
        ring.retain(recordFor(frame, "lmx.pass.frame" + std::to_string(frame)));
    }

    REQUIRE(ring.size() == FrameRecordRing::kCapacity);
    REQUIRE(ring.find(1) == nullptr);
    REQUIRE(ring.find(2) == nullptr);
    for (uint64_t frame = 3; frame <= FrameRecordRing::kCapacity + 2; ++frame) {
        INFO("frame " + std::to_string(frame));
        REQUIRE(ring.find(frame) != nullptr);
        REQUIRE(ring.find(frame)->record.debug.passes[0].label ==
                "lmx.pass.frame" + std::to_string(frame));
    }
}

//======================================================================================================================
// Four retained frames is one more than the three that can be in flight, which is what makes the
// frame a publication names still findable when it names one past the window.
TEST_CASE("the ring outlasts the frames that can be in flight", "[app]") {
    REQUIRE(FrameRecordRing::kCapacity >= 4);
}

//======================================================================================================================
// The join is by frame number and by nothing else: the timings that arrive while frame 5 is open
// belong to frame 2, and land on frame 2's record rather than on the newest one.
TEST_CASE("timings join the record of the frame they measured", "[app]") {
    FrameRecordRing ring;
    for (uint64_t frame = 1; frame <= 4; ++frame) {
        ring.retain(recordFor(frame, "lmx.pass.frame" + std::to_string(frame)));
    }

    const std::array<rhi::PassTiming, 2> measured = {
        rhi::PassTiming{.label = "lmx.pass.shadow", .gpuMilliseconds = 0.25},
        rhi::PassTiming{.label = "lmx.pass.scene", .gpuMilliseconds = 1.5}};

    REQUIRE(ring.joinTimings(2, measured));

    const RetainedFrame* joined = ring.find(2);
    REQUIRE(joined != nullptr);
    REQUIRE(joined->timed);
    REQUIRE(joined->timings.size() == 2);
    REQUIRE(joined->timings[1].label == "lmx.pass.scene");
    REQUIRE(joined->timings[1].gpuMilliseconds == 1.5);

    // The newest frame is not the measured one, and must not be given someone else's numbers.
    REQUIRE_FALSE(ring.find(4)->timed);
    REQUIRE(ring.find(4)->timings.empty());
    REQUIRE(ring.newestTimedFrame() == joined);
}

//======================================================================================================================
// Two halves of the same rule: a publication of zero is the RHI saying nothing has retired yet, and
// a frame already evicted has no record to join to. Neither is an error, and neither invents one.
TEST_CASE("a join with nothing to land on changes nothing", "[app]") {
    FrameRecordRing ring;
    ring.retain(recordFor(9, "lmx.pass.frame9"));

    const std::array<rhi::PassTiming, 1> measured = {
        rhi::PassTiming{.label = "lmx.pass.scene", .gpuMilliseconds = 1.0}};

    REQUIRE_FALSE(ring.joinTimings(0, measured));
    REQUIRE_FALSE(ring.joinTimings(3, measured));
    REQUIRE(ring.newestTimedFrame() == nullptr);
    REQUIRE_FALSE(ring.find(9)->timed);
}

//======================================================================================================================
// A frame loop joins the same publication every frame until it advances, so joining twice must
// replace rather than accumulate.
TEST_CASE("joining a frame twice replaces its timings", "[app]") {
    FrameRecordRing ring;
    ring.retain(recordFor(1, "lmx.pass.frame1"));

    const std::array<rhi::PassTiming, 2> first = {
        rhi::PassTiming{.label = "a", .gpuMilliseconds = 1.0},
        rhi::PassTiming{.label = "b", .gpuMilliseconds = 2.0}};
    const std::array<rhi::PassTiming, 1> second = {
        rhi::PassTiming{.label = "a", .gpuMilliseconds = 3.0}};

    REQUIRE(ring.joinTimings(1, first));
    REQUIRE(ring.joinTimings(1, second));

    REQUIRE(ring.find(1)->timings.size() == 1);
    REQUIRE(ring.find(1)->timings[0].gpuMilliseconds == 3.0);
}

//======================================================================================================================
// The frame an observer displays is the newest one both halves are known for, which is not the
// newest retained frame: newer ones are still in flight and have no timings yet.
TEST_CASE("the newest timed frame is the newest one that retired", "[app]") {
    FrameRecordRing ring;
    for (uint64_t frame = 1; frame <= 4; ++frame) {
        ring.retain(recordFor(frame, "lmx.pass.frame" + std::to_string(frame)));
    }

    const std::array<rhi::PassTiming, 1> measured = {
        rhi::PassTiming{.label = "lmx.pass.scene", .gpuMilliseconds = 1.0}};
    REQUIRE(ring.joinTimings(1, measured));
    REQUIRE(ring.joinTimings(2, measured));

    const RetainedFrame* newest = ring.newestTimedFrame();
    REQUIRE(newest != nullptr);
    REQUIRE(newest->record.frameId == 2);
}

//======================================================================================================================
// A frame that ran no passes reports an empty span rather than the previous frame's numbers, so an
// empty join is still a join -- the distinction the `timed` flag carries.
TEST_CASE("a frame measured as having no passes is still timed", "[app]") {
    FrameRecordRing ring;
    ring.retain(recordFor(1, "lmx.pass.frame1"));

    REQUIRE(ring.joinTimings(1, {}));
    REQUIRE(ring.find(1)->timed);
    REQUIRE(ring.find(1)->timings.empty());
    REQUIRE(ring.newestTimedFrame() == ring.find(1));
}
