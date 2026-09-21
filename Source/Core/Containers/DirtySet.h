//----------------------------------------------------------------------------------------------------------------------
/// @file DirtySet.h
/// @brief Declares a per-row, per-consumer dirty mask for paced table updates.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Assert.h"

#include <cstdint>
#include <vector>

namespace lmx {

/// Tracks, for each row index, which of up to eight independent consumers still owes that row a
/// write. One mask byte per index; bit `consumer` set means `consumer` has not yet cleared it.
class DirtySet {
public:
    /// Constructs a set for `consumers` independent readers. `consumers` is one to eight, since
    /// each index's mask fits one byte.
    explicit DirtySet(uint32_t consumers)
        : m_consumers(consumers), m_allMask(allMaskFor(consumers)) {}

    /// Grows or shrinks to `count` entries. An entry beyond the previous size starts dirty for
    /// every consumer; a retained entry's mask is unchanged.
    void resize(uint32_t count) { m_masks.resize(count, m_allMask); }

    /// Grows or shrinks to `count` entries, marking every entry dirty for every consumer.
    void assign(uint32_t count) { m_masks.assign(count, m_allMask); }

    /// Marks `index` dirty for every consumer.
    void markAll(uint32_t index) {
        LMX_ASSERT(index < m_masks.size(), "DirtySet::markAll index out of range");
        m_masks[index] = m_allMask;
    }

    /// Returns whether `consumer` still has `index` marked dirty.
    bool test(uint32_t index, uint32_t consumer) const {
        LMX_ASSERT(index < m_masks.size(), "DirtySet::test index out of range");
        LMX_ASSERT(consumer < m_consumers, "DirtySet::test consumer out of range");
        return (m_masks[index] & static_cast<uint8_t>(1u << consumer)) != 0;
    }

    /// Clears `index` for `consumer` alone, leaving other consumers' bits unchanged.
    void clear(uint32_t index, uint32_t consumer) {
        LMX_ASSERT(index < m_masks.size(), "DirtySet::clear index out of range");
        LMX_ASSERT(consumer < m_consumers, "DirtySet::clear consumer out of range");
        m_masks[index] &= static_cast<uint8_t>(~(1u << consumer));
    }

private:
    // Validates the range first: computing `1u << consumers` for an unvalidated `consumers` would
    // be undefined behaviour once it reaches 32, so the assert must run before the shift, not
    // after it in a constructor body that a member-initializer-list mask already evaluated.
    static uint8_t allMaskFor(uint32_t consumers) {
        LMX_ASSERT(consumers >= 1 && consumers <= 8, "DirtySet supports one to eight consumers");
        return static_cast<uint8_t>((1u << consumers) - 1);
    }

    uint32_t m_consumers;
    uint8_t m_allMask;
    std::vector<uint8_t> m_masks;
};

} // namespace lmx
