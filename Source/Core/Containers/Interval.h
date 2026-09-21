//----------------------------------------------------------------------------------------------------------------------
/// @file Interval.h
/// @brief Declares a half-defined contiguous index range with saturating bounds.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace lmx {

/// A contiguous run of `count` indices starting at `first`. `count == 0` is empty and carries no
/// indices, regardless of `first`.
struct Interval {
    uint32_t first = 0; ///< First index the interval covers.
    uint32_t count = 0; ///< Number of indices covered, zero for an empty interval.
};

/// The last index the interval covers, saturating at `UINT32_MAX` rather than overflowing when
/// `first + count` would exceed the representable range. An empty interval returns `first`.
inline uint32_t last(const Interval& interval) {
    if (interval.count == 0) {
        return interval.first;
    }
    const uint64_t lastIndex = uint64_t{interval.first} + interval.count - 1;
    return lastIndex > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                            : static_cast<uint32_t>(lastIndex);
}

/// Returns whether the two intervals share at least one index. An empty interval intersects
/// nothing, including another empty interval.
inline bool intersects(const Interval& a, const Interval& b) {
    if (a.count == 0 || b.count == 0) {
        return false;
    }
    return a.first <= last(b) && b.first <= last(a);
}

/// Returns the shared run of indices, or an empty interval when the two do not intersect.
inline Interval intersection(const Interval& a, const Interval& b) {
    if (!intersects(a, b)) {
        return Interval{};
    }
    const uint32_t first = std::max(a.first, b.first);
    const uint32_t lastIndex = std::min(last(a), last(b));
    return Interval{.first = first, .count = lastIndex - first + 1};
}

/// Returns whether `index` falls within the interval. An empty interval contains nothing.
inline bool contains(const Interval& interval, uint32_t index) {
    return interval.count != 0 && interval.first <= index && index <= last(interval);
}

} // namespace lmx
