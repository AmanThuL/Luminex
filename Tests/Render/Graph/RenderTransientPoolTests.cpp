#include <catch2/catch_test_macros.hpp>

#include "Render/Graph/TransientPool.h"
#include "Support/TransientBackendTestSupport.h"

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
    FakeTransientBackend backend;
    TransientPool pool(backend);

    backend.frame = 1;
    pool.beginFrame();
    REQUIRE(pool.reserve(4096).has_value());
    REQUIRE(pool.heapBytes() == 4096);

    REQUIRE_FALSE(pool.reserve(4096).has_value());
    REQUIRE_FALSE(pool.reserve(8192).has_value());
    // The refusal changed nothing: the frame still holds the generation it reserved.
    REQUIRE(pool.heapBytes() == 4096);
    REQUIRE(pool.liveGenerationCount() == 1);

    backend.frame = 2;
    pool.beginFrame();
    REQUIRE(pool.reserve(4096).has_value());
}

//======================================================================================================================
// The rotation the pool's whole safety argument rests on: consecutive frames land on different
// slots, so a frame's transient memory is never the memory of a frame still in flight, and a slot
// that keeps asking for the same footprint keeps the generation it already has.
TEST_CASE("consecutive frames reserve separate slots", "[render][transient]") {
    FakeTransientBackend backend;
    TransientPool pool(backend);

    for (uint64_t frame = 1; frame <= kTransientFrameSlots; ++frame) {
        backend.frame = frame;
        pool.beginFrame();
        REQUIRE(pool.reserve(4096).has_value());
        // A slot per frame so far, none of them shared.
        REQUIRE(pool.liveGenerationCount() == frame);
    }

    // The fourth frame comes back to the first slot, which already holds a heap of these bytes, so
    // nothing new is created and nothing is retired.
    backend.frame = kTransientFrameSlots + 1;
    pool.beginFrame();
    REQUIRE(pool.reserve(4096).has_value());
    REQUIRE(pool.liveGenerationCount() == kTransientFrameSlots);
    REQUIRE(pool.retiringGenerationCount() == 0);
}

//======================================================================================================================
// A shape change retires the generation the slot held rather than dropping it, and the release is
// dated by the last frame that used it: a frame still in flight may be reading it.
TEST_CASE("a resized slot retires its generation before releasing it", "[render][transient]") {
    FakeTransientBackend backend;
    TransientPool pool(backend);

    backend.frame = 1;
    pool.beginFrame();
    REQUIRE(pool.reserve(4096).has_value());

    // Back to the same slot with a different footprint: a new generation, and the old one held.
    backend.frame = 1 + kTransientFrameSlots;
    pool.beginFrame();
    REQUIRE(pool.reserve(8192).has_value());
    REQUIRE(pool.heapBytes() == 8192);
    REQUIRE(pool.retiringGenerationCount() == 1);

    // Released once the frames that could still have been reading it have gone by.
    backend.frame = 1 + 2 * kTransientFrameSlots;
    pool.beginFrame();
    REQUIRE(pool.retiringGenerationCount() == 0);
    REQUIRE(pool.liveGenerationCount() == 1);
}

//======================================================================================================================
// A slot's placements live exactly until the slot comes round again: the frames between it place
// into their own slots and leave it alone, and reopening it releases what it placed while keeping
// the heap, which the same footprint goes on using.
TEST_CASE("the pool releases a slot's placements when the slot comes round",
          "[render][transient]") {
    constexpr TransientTextureDesc kColor{.width = 64,
                                          .height = 64,
                                          .format = rojoRHI::Format::RGBA16Float,
                                          .renderTarget = true,
                                          .sampled = true};
    FakeTransientBackend backend;
    TransientPool pool(backend);

    backend.frame = 1;
    pool.beginFrame();
    REQUIRE(pool.reserve(2 * FakeTransientBackend::kTextureAlignment).has_value());
    REQUIRE(pool.placeTexture(kColor, 0, "lmx.transient.first").has_value());
    REQUIRE(
        pool.placeTexture(kColor, FakeTransientBackend::kTextureAlignment, "lmx.transient.second")
            .has_value());
    REQUIRE(backend.heaps.size() == 1);
    const TransientHeapId heap{backend.heaps.begin()->first};
    REQUIRE(backend.placementCount(heap) == 2);

    for (uint64_t frame = 2; frame <= kTransientFrameSlots; ++frame) {
        backend.frame = frame;
        pool.beginFrame();
        REQUIRE(backend.placementCount(heap) == 2);
    }

    backend.frame = kTransientFrameSlots + 1;
    pool.beginFrame();
    REQUIRE(backend.placementCount(heap) == 0);
    REQUIRE(backend.heaps.size() == 1);
    REQUIRE(backend.heaps.contains(heap.value));
    REQUIRE(backend.destroyedHeaps.empty());
}

//======================================================================================================================
// A replacement takes the slot's heap out of service, and the backend keeps it until the frame
// that last used it is proved retired: through the frame that replaced it, and released by the
// first beginFrame() past that bound.
TEST_CASE("a replaced heap is destroyed only after its last frame retires", "[render][transient]") {
    constexpr uint64_t kSmall = 64 * 1024;
    constexpr uint64_t kLarge = 128 * 1024;
    FakeTransientBackend backend;
    TransientPool pool(backend);

    backend.frame = 1;
    pool.beginFrame();
    REQUIRE(pool.reserve(kSmall).has_value());
    REQUIRE(backend.heaps.size() == 1);
    const uint32_t small = backend.heaps.begin()->first;
    REQUIRE(backend.heaps.at(small).bytes == kSmall);
    REQUIRE(backend.heaps.at(small).label ==
            "lmx.render.transientHeap" + std::to_string(1 % kTransientFrameSlots));

    for (uint64_t frame = 2; frame <= kTransientFrameSlots; ++frame) {
        backend.frame = frame;
        pool.beginFrame();
    }

    backend.frame = 1 + kTransientFrameSlots;
    pool.beginFrame();
    REQUIRE(pool.reserve(kLarge).has_value());
    REQUIRE(backend.heaps.size() == 2);
    REQUIRE(backend.heaps.contains(small));
    REQUIRE(backend.destroyedHeaps.empty());
    REQUIRE(pool.retiringGenerationCount() == 1);

    backend.frame = 2 + kTransientFrameSlots;
    pool.beginFrame();
    REQUIRE_FALSE(backend.heaps.contains(small));
    REQUIRE(backend.destroyedHeaps == std::vector<uint32_t>{small});
    REQUIRE(backend.heaps.size() == 1);
    REQUIRE(backend.heaps.begin()->second.bytes == kLarge);
    REQUIRE(pool.retiringGenerationCount() == 0);
}
