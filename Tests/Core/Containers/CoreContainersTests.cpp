#include "Core/Containers/DirtySet.h"
#include "Core/Containers/Interval.h"
#include "Core/Containers/RingBuffer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

static_assert(std::forward_iterator<lmx::RingBuffer<int>::iterator>);
static_assert(std::forward_iterator<lmx::RingBuffer<int>::const_iterator>);

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

    // Iterating through a const view is what selects const_iterator.
    const auto& view = ring;
    static_assert(std::is_same_v<decltype(view.begin()), lmx::RingBuffer<int>::const_iterator>);

    // std::minmax_element needs a real std::iterator_traits<const_iterator>: iterator_category,
    // value_type, difference_type, reference and pointer must all be present.
    const auto [minIt, maxIt] = std::minmax_element(view.begin(), view.end());
    REQUIRE(*minIt == 1);
    REQUIRE(*maxIt == 9);

    const std::vector<int> copied(view.begin(), view.end());
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

//======================================================================================================================
TEST_CASE("RingBuffer::popOldest preserves partial, full and wrapped FIFO order", "[core]") {
    lmx::RingBuffer<int> ring(3);
    ring.push(1);
    ring.push(2);
    ring.popOldest();
    REQUIRE(ring.size() == 1);
    REQUIRE(ring[0] == 2);
    ring.push(3);
    ring.push(4);
    REQUIRE(std::vector<int>(ring.begin(), ring.end()) == std::vector<int>{2, 3, 4});
    ring.popOldest();
    ring.push(5);
    ring.push(6);
    REQUIRE(std::vector<int>(ring.begin(), ring.end()) == std::vector<int>{4, 5, 6});
    ring.popOldest();
    REQUIRE(ring[0] == 5);
    REQUIRE(ring.back() == 6);
    ring.popOldest();
    ring.popOldest();
    REQUIRE(ring.size() == 0);
    REQUIRE(ring.begin() == ring.end());
    REQUIRE(ring.capacity() == 3);
    ring.push(7);
    REQUIRE(ring[0] == 7);
    REQUIRE(ring.back() == 7);
    ring.clear();
    ring.push(8);
    REQUIRE(ring[0] == 8);
}

//======================================================================================================================
TEST_CASE("RingBuffer mixed push, pop and clear operations match a deque", "[core]") {
    for (const uint32_t capacity : {1u, 2u, 3u, 7u}) {
        CAPTURE(capacity);
        lmx::RingBuffer<int> ring(capacity);
        std::deque<int> expected;
        uint32_t state = 12345;
        for (int step = 0; step < 512; ++step) {
            state = state * 1664525u + 1013904223u;
            const uint32_t operation = (state >> 16) % 8;
            if (operation == 0) {
                ring.clear();
                expected.clear();
            } else if (operation < 3 && !expected.empty()) {
                ring.popOldest();
                expected.pop_front();
            } else {
                ring.push(step);
                expected.push_back(step);
                if (expected.size() > capacity)
                    expected.pop_front();
            }
            CAPTURE(step, operation);
            REQUIRE(ring.capacity() == capacity);
            REQUIRE(ring.size() == expected.size());
            const auto& view = ring;
            REQUIRE(std::vector<int>(view.begin(), view.end()) ==
                    std::vector<int>(expected.begin(), expected.end()));
            for (uint32_t index = 0; index < ring.size(); ++index)
                REQUIRE(ring[index] == expected[index]);
            if (!expected.empty())
                REQUIRE(ring.back() == expected.back());
        }
    }
}

//======================================================================================================================
TEST_CASE("RingBuffer releases move-only values on pop, eviction, clear and destruction",
          "[core]") {
    std::vector<int> released;
    const auto release = [&released](int* value) {
        released.push_back(*value);
        delete value;
    };
    using Value = std::unique_ptr<int, decltype(release)>;
    static_assert(!std::is_default_constructible_v<Value>);
    static_assert(!std::is_copy_constructible_v<Value>);
    const auto value = [&](int number) { return Value(new int(number), release); };
    {
        lmx::RingBuffer<Value> ring(3);
        ring.push(value(1));
        ring.push(value(2));
        ring.popOldest();
        REQUIRE(released == std::vector<int>{1});
        REQUIRE(*ring[0] == 2);
        ring.push(value(3));
        ring.push(value(4));
        ring.push(value(5));
        REQUIRE(released == std::vector<int>{1, 2});
        REQUIRE(*ring[0] == 3);
        ring.popOldest();
        REQUIRE(released == std::vector<int>{1, 2, 3});
        REQUIRE(*ring.back() == 5);
        ring.clear();
        REQUIRE(released.size() == 5);
        std::sort(released.begin(), released.end());
        REQUIRE(released == std::vector<int>{1, 2, 3, 4, 5});
        ring.push(value(6));
        ring.popOldest();
        REQUIRE(released.back() == 6);
        ring.push(value(7));
        REQUIRE(*ring.back() == 7);
    }
    REQUIRE(released == std::vector<int>{1, 2, 3, 4, 5, 6, 7});
}
