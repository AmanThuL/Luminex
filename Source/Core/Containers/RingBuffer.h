//----------------------------------------------------------------------------------------------------------------------
/// @file RingBuffer.h
/// @brief Declares a fixed-capacity FIFO retaining the most recently pushed values.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Assert.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

namespace lmx {

/// A fixed-capacity ring of the most recently pushed values. Once `capacity` values are held, the
/// next push evicts the oldest. Index 0 is always the oldest held value.
template <typename T>
class RingBuffer {
public:
    /// Constructs a buffer retaining at most `capacity` values.
    explicit RingBuffer(uint32_t capacity) : m_capacity(capacity) {
        LMX_ASSERT(capacity > 0, "RingBuffer capacity must be positive");
        m_storage.reserve(capacity);
    }

    /// Appends one value, evicting the oldest held value once `capacity` are already held.
    void push(T value) {
        if (m_storage.size() < m_capacity) {
            m_storage.push_back(std::move(value));
        } else {
            m_storage[m_oldest] = std::move(value);
            m_oldest = (m_oldest + 1) % m_capacity;
        }
    }

    /// Values currently held, at most `capacity`.
    uint32_t size() const { return static_cast<uint32_t>(m_storage.size()); }

    /// Values retained before the oldest is evicted.
    uint32_t capacity() const { return m_capacity; }

    /// The most recently pushed value.
    const T& back() const {
        LMX_ASSERT(!m_storage.empty(), "RingBuffer::back on an empty buffer");
        return (*this)[size() - 1];
    }

    /// The most recently pushed value, mutable.
    T& back() { return const_cast<T&>(std::as_const(*this).back()); }

    /// Discards every held value; `capacity` is unchanged.
    void clear() {
        m_storage.clear();
        m_oldest = 0;
    }

    /// Indexes chronologically: 0 is the oldest held value, `size() - 1` the newest.
    const T& operator[](uint32_t index) const {
        LMX_ASSERT(index < size(), "RingBuffer index out of range");
        const uint32_t offset =
            (m_storage.size() < m_capacity) ? index : (m_oldest + index) % m_capacity;
        return m_storage[offset];
    }

    /// Indexes chronologically, mutable: 0 is the oldest held value, `size() - 1` the newest.
    T& operator[](uint32_t index) { return const_cast<T&>(std::as_const(*this)[index]); }

    /// A `std::forward_iterator`-conforming iterator visiting held values from oldest to newest.
    /// `IsConst` selects between the two forms `RingBuffer` exposes as `const_iterator` and
    /// `iterator`; both index through the owning `RingBuffer` rather than holding storage
    /// directly, so they stay valid across a `push` that has not yet evicted the index they hold.
    template <bool IsConst>
    class Iterator {
        using Owner = std::conditional_t<IsConst, const RingBuffer, RingBuffer>;

    public:
        /// Marks this a standard forward iterator to `std::iterator_traits` and the algorithms
        /// and range adaptors that query it.
        using iterator_category = std::forward_iterator_tag;
        /// The iterated value's type, ignoring const-ness.
        using value_type = T;
        /// The type `std::distance` and similar algorithms compute between two iterators.
        using difference_type = std::ptrdiff_t;
        /// What `operator*` returns: `T&` for `iterator`, `const T&` for `const_iterator`.
        using reference = std::conditional_t<IsConst, const T&, T&>;
        /// What `operator->` returns: `T*` for `iterator`, `const T*` for `const_iterator`.
        using pointer = std::conditional_t<IsConst, const T*, T*>;

        /// Constructs a singular iterator: default-constructible per the forward-iterator
        /// requirement, comparing equal only to another default-constructed iterator.
        Iterator() = default;
        /// Constructs an iterator at `owner`'s chronological `index`.
        Iterator(Owner* owner, uint32_t index) : m_owner(owner), m_index(index) {}

        /// Returns the value at the current chronological index.
        reference operator*() const { return (*m_owner)[m_index]; }
        /// Returns a pointer to the value at the current chronological index.
        pointer operator->() const { return &(*m_owner)[m_index]; }

        /// Advances to the next-newer value and returns this iterator.
        Iterator& operator++() {
            ++m_index;
            return *this;
        }
        /// Advances to the next-newer value, returning the pre-increment position.
        Iterator operator++(int) {
            Iterator before = *this;
            ++(*this);
            return before;
        }

        /// Returns whether both iterators are at the same chronological index of the same owner.
        bool operator==(const Iterator& other) const {
            return m_owner == other.m_owner && m_index == other.m_index;
        }
        /// Returns whether the iterators are not at the same chronological index of the same
        /// owner.
        bool operator!=(const Iterator& other) const { return !(*this == other); }

    private:
        Owner* m_owner = nullptr;
        uint32_t m_index = 0;
    };

    /// A mutable iterator visiting held values from oldest to newest.
    using iterator = Iterator<false>;
    /// A read-only iterator visiting held values from oldest to newest.
    using const_iterator = Iterator<true>;

    /// The oldest held value, or `end()` when empty.
    iterator begin() { return iterator(this, 0); }
    /// One past the newest held value.
    iterator end() { return iterator(this, size()); }
    /// The oldest held value, or `end()` when empty.
    const_iterator begin() const { return const_iterator(this, 0); }
    /// One past the newest held value.
    const_iterator end() const { return const_iterator(this, size()); }

private:
    uint32_t m_capacity;
    uint32_t m_oldest = 0; ///< Storage index of the oldest value once `capacity` is reached.
    std::vector<T> m_storage;
};

} // namespace lmx
