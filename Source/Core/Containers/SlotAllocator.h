//----------------------------------------------------------------------------------------------------------------------
/// @file SlotAllocator.h
/// @brief Declares a generational slot allocator with slot reuse and retirement.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Assert.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace lmx {

/// Hands out stable slot indices, each with a generation that advances on every release.
///
/// A new slot starts live at generation 1. Releasing a slot makes it dead and advances its
/// generation, saturating at 0xFFFF; a slot at 0xFFFF is retired and never handed out again.
class SlotAllocator {
public:
    /// Returns the lowest dead, unexhausted slot at or after the search cursor and makes it live,
    /// or appends a new live slot at generation 1 when none exists.
    uint32_t allocate() {
        for (uint32_t i = m_searchStart; i < m_slots.size(); ++i) {
            if (!m_slots[i].live && m_slots[i].generation != std::numeric_limits<uint16_t>::max()) {
                m_slots[i].live = true;
                m_searchStart = i + 1;
                return i;
            }
        }
        LMX_ASSERT(m_slots.size() < std::numeric_limits<uint32_t>::max(),
                   "slot identities exhausted");
        m_slots.push_back({});
        m_searchStart = static_cast<uint32_t>(m_slots.size());
        return m_searchStart - 1;
    }

    /// Makes a live slot dead, advances its generation (saturating at 0xFFFF, which retires the
    /// slot) and lowers the search cursor to it.
    void release(uint32_t slot) {
        LMX_ASSERT(slot < m_slots.size() && m_slots[slot].live, "released slot is not live");
        auto& entry = m_slots[slot];
        m_searchStart = std::min(m_searchStart, slot);
        entry.live = false;
        if (entry.generation != std::numeric_limits<uint16_t>::max()) {
            ++entry.generation;
        }
    }

    /// Returns whether `slot` exists, is live and carries exactly `generation`.
    bool resolves(uint32_t slot, uint16_t generation) const {
        return slot < m_slots.size() && m_slots[slot].live &&
               m_slots[slot].generation == generation;
    }

    /// Returns the current generation of an existing slot, live or dead.
    uint16_t generation(uint32_t slot) const {
        LMX_ASSERT(slot < m_slots.size(), "slot is out of range");
        return m_slots[slot].generation;
    }

    /// Returns the number of slots ever created, live, dead or retired.
    uint32_t size() const { return static_cast<uint32_t>(m_slots.size()); }

private:
    /// Liveness and generation of one slot.
    struct Entry {
        uint16_t generation = 1; ///< Current generation; 0xFFFF once retired.
        bool live = true;        ///< Whether the slot is handed out.
    };

    std::vector<Entry> m_slots; ///< Every slot ever created, indexed by slot.
    uint32_t m_searchStart = 0; ///< First slot the next allocation examines.
};

} // namespace lmx
