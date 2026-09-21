#include "Core/Containers/DirtySet.h"
#include "Core/Containers/Interval.h"
#include "Core/Containers/RingBuffer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

//======================================================================================================================
TEST_CASE("DirtySet::resize marks only new entries, for every consumer", "[core]") {
    lmx::DirtySet dirty(3);
    dirty.resize(2);
    for (uint32_t consumer = 0; consumer < 3; ++consumer) {
        REQUIRE(dirty.test(0, consumer));
        REQUIRE(dirty.test(1, consumer));
    }

    dirty.clear(0, 0);
    dirty.clear(0, 1);
    dirty.clear(0, 2);
    dirty.clear(1, 1);
    dirty.resize(4);

    // Retained entries keep whatever a consumer already cleared.
    REQUIRE_FALSE(dirty.test(0, 0));
    REQUIRE_FALSE(dirty.test(0, 1));
    REQUIRE_FALSE(dirty.test(0, 2));
    REQUIRE(dirty.test(1, 0));
    REQUIRE_FALSE(dirty.test(1, 1));
    REQUIRE(dirty.test(1, 2));

    // Entries added by the grow start dirty for every consumer.
    for (uint32_t consumer = 0; consumer < 3; ++consumer) {
        REQUIRE(dirty.test(2, consumer));
        REQUIRE(dirty.test(3, consumer));
    }
}

//======================================================================================================================
TEST_CASE("DirtySet::assign marks every entry dirty, for every consumer", "[core]") {
    lmx::DirtySet dirty(3);
    dirty.resize(3);
    dirty.clear(0, 0);
    dirty.clear(1, 1);
    dirty.clear(2, 2);

    dirty.assign(3);

    for (uint32_t index = 0; index < 3; ++index) {
        for (uint32_t consumer = 0; consumer < 3; ++consumer) {
            REQUIRE(dirty.test(index, consumer));
        }
    }
}

//======================================================================================================================
TEST_CASE("DirtySet::clear affects one consumer alone", "[core]") {
    lmx::DirtySet dirty(3);
    dirty.resize(1);

    dirty.clear(0, 1);
    REQUIRE(dirty.test(0, 0));
    REQUIRE_FALSE(dirty.test(0, 1));
    REQUIRE(dirty.test(0, 2));

    dirty.markAll(0);
    REQUIRE(dirty.test(0, 0));
    REQUIRE(dirty.test(0, 1));
    REQUIRE(dirty.test(0, 2));
}

//======================================================================================================================
TEST_CASE("Interval::last saturates at UINT32_MAX rather than overflowing", "[core]") {
    using lmx::Interval;
    REQUIRE(lmx::last(Interval{.first = 0, .count = 0}) == 0);
    REQUIRE(lmx::last(Interval{.first = 10, .count = 0}) == 10);
    REQUIRE(lmx::last(Interval{.first = 0, .count = 5}) == 4);
    REQUIRE(lmx::last(Interval{.first = std::numeric_limits<uint32_t>::max() - 2, .count = 3}) ==
            std::numeric_limits<uint32_t>::max());
    REQUIRE(lmx::last(Interval{.first = std::numeric_limits<uint32_t>::max() - 2, .count = 10}) ==
            std::numeric_limits<uint32_t>::max());
    REQUIRE(lmx::last(Interval{.first = 1, .count = std::numeric_limits<uint32_t>::max()}) ==
            std::numeric_limits<uint32_t>::max());
}

//======================================================================================================================
TEST_CASE("empty intervals intersect nothing", "[core]") {
    using lmx::Interval;
    const Interval empty{.first = 5, .count = 0};
    const Interval nonEmpty{.first = 0, .count = 10};

    REQUIRE_FALSE(lmx::intersects(empty, nonEmpty));
    REQUIRE_FALSE(lmx::intersects(nonEmpty, empty));
    REQUIRE_FALSE(lmx::intersects(empty, empty));
    REQUIRE_FALSE(lmx::contains(empty, 5));

    const Interval overlap = lmx::intersection(empty, nonEmpty);
    REQUIRE(overlap.count == 0);
}

//======================================================================================================================
TEST_CASE("Interval::intersects, intersection and contains agree on an overlapping pair",
          "[core]") {
    using lmx::Interval;
    const Interval a{.first = 4, .count = 6};         // covers 4..9
    const Interval b{.first = 8, .count = 6};         // covers 8..13
    const Interval disjoint{.first = 20, .count = 3}; // covers 20..22

    REQUIRE(lmx::intersects(a, b));
    REQUIRE_FALSE(lmx::intersects(a, disjoint));

    const Interval overlap = lmx::intersection(a, b);
    REQUIRE(overlap.first == 8);
    REQUIRE(overlap.count == 2); // covers 8..9

    REQUIRE(lmx::contains(a, 4));
    REQUIRE(lmx::contains(a, 9));
    REQUIRE_FALSE(lmx::contains(a, 10));
}

//======================================================================================================================
TEST_CASE("RingBuffer evicts the oldest value at capacity; index 0 is oldest", "[core]") {
    lmx::RingBuffer<int> ring(3);
    REQUIRE(ring.capacity() == 3);
    REQUIRE(ring.size() == 0);

    ring.push(1);
    ring.push(2);
    REQUIRE(ring.size() == 2);
    REQUIRE(ring[0] == 1);
    REQUIRE(ring[1] == 2);
    REQUIRE(ring.back() == 2);

    ring.push(3);
    REQUIRE(ring.size() == 3);
    REQUIRE(ring[0] == 1);
    REQUIRE(ring[1] == 2);
    REQUIRE(ring[2] == 3);

    ring.push(4); // evicts 1
    REQUIRE(ring.size() == 3);
    REQUIRE(ring[0] == 2);
    REQUIRE(ring[1] == 3);
    REQUIRE(ring[2] == 4);
    REQUIRE(ring.back() == 4);

    ring.push(5); // evicts 2
    ring.push(6); // evicts 3
    REQUIRE(ring[0] == 4);
    REQUIRE(ring[1] == 5);
    REQUIRE(ring[2] == 6);

    std::vector<int> visited;
    for (int value : ring) {
        visited.push_back(value);
    }
    REQUIRE(visited == std::vector<int>{4, 5, 6});
}

//======================================================================================================================
TEST_CASE("RingBuffer::clear discards held values without changing capacity", "[core]") {
    lmx::RingBuffer<int> ring(2);
    ring.push(1);
    ring.push(2);
    ring.push(3); // wraps once before clearing

    ring.clear();
    REQUIRE(ring.size() == 0);
    REQUIRE(ring.capacity() == 2);

    ring.push(10);
    ring.push(20);
    REQUIRE(ring[0] == 10);
    REQUIRE(ring[1] == 20);

    ring.push(30); // evicts 10 after the clear, confirming the wrap state reset
    REQUIRE(ring[0] == 20);
    REQUIRE(ring[1] == 30);
}

//======================================================================================================================
TEST_CASE("RingBuffer's const_iterator works with std::minmax_element and std::vector's range "
          "constructor",
          "[core]") {
    lmx::RingBuffer<int> ring(4);
    for (int value : {5, 1, 9, 3, 7}) { // 5 wraps out once the fifth value pushes
        ring.push(value);
    }
    REQUIRE(ring.size() == 4);

    // std::minmax_element needs a real std::iterator_traits<const_iterator>: iterator_category,
    // value_type, difference_type, reference and pointer must all be present.
    const auto [minIt, maxIt] = std::minmax_element(ring.begin(), ring.end());
    REQUIRE(*minIt == 1);
    REQUIRE(*maxIt == 9);

    const std::vector<int> copied(ring.begin(), ring.end());
    REQUIRE(copied == std::vector<int>{1, 9, 3, 7});
}

//======================================================================================================================
TEST_CASE("RingBuffer's mutable iterator and operator[] write through to held values", "[core]") {
    lmx::RingBuffer<int> ring(3);
    ring.push(1);
    ring.push(2);
    ring.push(3);

    for (int& value : ring) { // requires non-const begin()/end() returning a mutable iterator
        value *= 10;
    }
    REQUIRE(ring[0] == 10);
    REQUIRE(ring[1] == 20);
    REQUIRE(ring[2] == 30);

    ring[1] = 99; // requires a non-const operator[]
    REQUIRE(ring[1] == 99);

    ring.back() = 100; // requires a non-const back()
    REQUIRE(ring.back() == 100);
    REQUIRE(ring[2] == 100);
}
