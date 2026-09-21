#include "Core/Containers/Handle.h"
#include "Core/Containers/SlotAllocator.h"

#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {
struct AppleTag;
struct PearTag;
using Apple = lmx::Handle<AppleTag>;
using Pear = lmx::Handle<PearTag>;
} // namespace

//======================================================================================================================
TEST_CASE("handles with different tags neither compare nor convert", "[core]") {
    STATIC_REQUIRE(std::equality_comparable<Apple>);
    STATIC_REQUIRE_FALSE(std::equality_comparable_with<Apple, Pear>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<Apple, Pear>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<Pear, Apple>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<Apple, Pear>);
}

//======================================================================================================================
TEST_CASE("handle is eight bytes with slot, generation and store at 0, 4 and 6", "[core]") {
    STATIC_REQUIRE(sizeof(Apple) == 8);
    STATIC_REQUIRE(std::is_standard_layout_v<Apple>);
    STATIC_REQUIRE(offsetof(Apple, slot) == 0);
    STATIC_REQUIRE(offsetof(Apple, generation) == 4);
    STATIC_REQUIRE(offsetof(Apple, store) == 6);
    STATIC_REQUIRE(std::is_same_v<decltype(Apple::slot), uint32_t>);
    STATIC_REQUIRE(std::is_same_v<decltype(Apple::generation), uint16_t>);
    STATIC_REQUIRE(std::is_same_v<decltype(Apple::store), uint16_t>);
}

//======================================================================================================================
TEST_CASE("default handle is all zero and equality covers every field", "[core]") {
    const Apple zero{};
    CHECK(zero.slot == 0);
    CHECK(zero.generation == 0);
    CHECK(zero.store == 0);
    CHECK(Apple{1, 2, 3} == Apple{1, 2, 3});
    CHECK(Apple{1, 2, 3} != Apple{9, 2, 3});
    CHECK(Apple{1, 2, 3} != Apple{1, 9, 3});
    CHECK(Apple{1, 2, 3} != Apple{1, 2, 9});
}

//======================================================================================================================
TEST_CASE("slot allocator appends new slots at generation one", "[core]") {
    lmx::SlotAllocator slots;
    CHECK(slots.size() == 0);
    CHECK(slots.allocate() == 0);
    CHECK(slots.allocate() == 1);
    CHECK(slots.allocate() == 2);
    CHECK(slots.size() == 3);
    CHECK(slots.generation(0) == 1);
    CHECK(slots.generation(2) == 1);
    CHECK(slots.resolves(1, 1));
}

//======================================================================================================================
TEST_CASE("slot allocator reuses the lowest released slot at generation two", "[core]") {
    lmx::SlotAllocator slots;
    for (int i = 0; i < 4; ++i) {
        slots.allocate();
    }
    slots.release(2);
    slots.release(1);
    CHECK(slots.generation(1) == 2);
    CHECK_FALSE(slots.resolves(1, 1));
    CHECK_FALSE(slots.resolves(1, 2));
    CHECK(slots.allocate() == 1);
    CHECK(slots.resolves(1, 2));
    CHECK(slots.allocate() == 2);
    CHECK(slots.generation(2) == 2);
    CHECK(slots.allocate() == 4);
    CHECK(slots.size() == 5);
}

//======================================================================================================================
TEST_CASE("slot allocator search resumes after the last reused slot", "[core]") {
    lmx::SlotAllocator slots;
    for (int i = 0; i < 3; ++i) {
        slots.allocate();
    }
    slots.release(0);
    slots.release(2);
    CHECK(slots.allocate() == 0);
    CHECK(slots.allocate() == 2);
    CHECK(slots.allocate() == 3);
}

//======================================================================================================================
TEST_CASE("slot allocator retires a slot whose generation reaches 0xFFFF", "[core]") {
    lmx::SlotAllocator slots;
    CHECK(slots.allocate() == 0);
    for (uint32_t generation = 1; generation < 0xFFFE; ++generation) {
        slots.release(0);
        REQUIRE(slots.allocate() == 0);
    }
    CHECK(slots.generation(0) == 0xFFFE);
    CHECK(slots.resolves(0, 0xFFFE));
    slots.release(0);
    CHECK(slots.generation(0) == 0xFFFF);
    CHECK_FALSE(slots.resolves(0, 0xFFFF));
    CHECK(slots.allocate() == 1);
    CHECK(slots.allocate() == 2);
    CHECK(slots.generation(0) == 0xFFFF);
    CHECK_FALSE(slots.resolves(0, 0xFFFF));
}

//======================================================================================================================
TEST_CASE("slot allocator never resolves stale or out-of-range slots", "[core]") {
    lmx::SlotAllocator slots;
    CHECK_FALSE(slots.resolves(0, 0));
    CHECK_FALSE(slots.resolves(0, 1));
    slots.allocate();
    CHECK_FALSE(slots.resolves(0, 0));
    CHECK_FALSE(slots.resolves(0, 2));
    CHECK_FALSE(slots.resolves(1, 1));
    CHECK_FALSE(slots.resolves(0xFFFFFFFFu, 1));
    slots.release(0);
    CHECK_FALSE(slots.resolves(0, 1));
    CHECK_FALSE(slots.resolves(0, 2));
    slots.allocate();
    CHECK_FALSE(slots.resolves(0, 1));
    CHECK(slots.resolves(0, 2));
}

//======================================================================================================================
TEST_CASE("slot allocator reports a slot live until it is released", "[core]") {
    lmx::SlotAllocator slots;
    CHECK_FALSE(slots.live(0));
    const auto slot = slots.allocate();
    CHECK(slots.live(slot));
    slots.release(slot);
    CHECK_FALSE(slots.live(slot));
    CHECK_FALSE(slots.live(slot + 1));
    CHECK_FALSE(slots.live(0xFFFFFFFFu));
}
