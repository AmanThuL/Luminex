#include <catch2/catch_test_macros.hpp>

#include "GraphTestSupport.h"
#include "Render/TransientPool.h"

using namespace lmx;
using namespace lmx::render;

//======================================================================================================================
// One reservation per frame, whatever it asks for.
//
// The equal-bytes case is the one worth pinning: a second graph asking for the footprint the slot
// already holds changes nothing about the heap, so a guard that only watched for a *different*
// footprint would let it through -- and both callers would then place at the same offsets in memory
// the first one's resources are still live in, with no barrier between two graphs that never saw
// each other. Refusing it is the whole point, so the same-bytes and different-bytes cases are both
// asserted, and a fresh frame is asserted to reserve again so the guard is per frame rather than
// permanent.
TEST_CASE("a frame reserves its transient memory once", "[render][transient]") {
    FakeDevice device;
    TransientPool pool(device);

    device.frame = 1;
    pool.beginFrame();
    REQUIRE(pool.reserve(4096).has_value());
    REQUIRE(pool.heapBytes() == 4096);

    REQUIRE_FALSE(pool.reserve(4096).has_value());
    REQUIRE_FALSE(pool.reserve(8192).has_value());
    // The refusal changed nothing: the frame still holds the generation it reserved.
    REQUIRE(pool.heapBytes() == 4096);
    REQUIRE(pool.liveGenerationCount() == 1);

    device.frame = 2;
    pool.beginFrame();
    REQUIRE(pool.reserve(4096).has_value());
}

//======================================================================================================================
// The rotation the pool's whole safety argument rests on: consecutive frames land on different
// slots, so a frame's transient memory is never the memory of a frame still in flight, and a slot
// that keeps asking for the same footprint keeps the generation it already has.
TEST_CASE("consecutive frames reserve separate slots", "[render][transient]") {
    FakeDevice device;
    TransientPool pool(device);

    for (uint64_t frame = 1; frame <= kTransientFrameSlots; ++frame) {
        device.frame = frame;
        pool.beginFrame();
        REQUIRE(pool.reserve(4096).has_value());
        // A slot per frame so far, none of them shared.
        REQUIRE(pool.liveGenerationCount() == frame);
    }

    // The fourth frame comes back to the first slot, which already holds a heap of these bytes, so
    // nothing new is created and nothing is retired.
    device.frame = kTransientFrameSlots + 1;
    pool.beginFrame();
    REQUIRE(pool.reserve(4096).has_value());
    REQUIRE(pool.liveGenerationCount() == kTransientFrameSlots);
    REQUIRE(pool.retiringGenerationCount() == 0);
}

//======================================================================================================================
// A shape change retires the generation the slot held rather than dropping it, and the release is
// dated by the last frame that used it: a frame still in flight may be reading it.
TEST_CASE("a resized slot retires its generation before releasing it", "[render][transient]") {
    FakeDevice device;
    TransientPool pool(device);

    device.frame = 1;
    pool.beginFrame();
    REQUIRE(pool.reserve(4096).has_value());

    // Back to the same slot with a different footprint: a new generation, and the old one held.
    device.frame = 1 + kTransientFrameSlots;
    pool.beginFrame();
    REQUIRE(pool.reserve(8192).has_value());
    REQUIRE(pool.heapBytes() == 8192);
    REQUIRE(pool.retiringGenerationCount() == 1);

    // Released once the frames that could still have been reading it have gone by.
    device.frame = 1 + 2 * kTransientFrameSlots;
    pool.beginFrame();
    REQUIRE(pool.retiringGenerationCount() == 0);
    REQUIRE(pool.liveGenerationCount() == 1);
}
