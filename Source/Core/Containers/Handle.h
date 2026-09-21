//----------------------------------------------------------------------------------------------------------------------
/// @file Handle.h
/// @brief Declares a tagged generational handle whose tags keep unrelated identities apart.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>

namespace lmx {

/// Generational identity of one slot in one store; store zero is invalid.
///
/// `Tag` only distinguishes types: handles with different tags neither compare nor convert.
template <class Tag>
struct Handle {
    uint32_t slot = 0;       ///< Stable table slot, never a draw-list position.
    uint16_t generation = 0; ///< Reuse generation; exhausted slots are permanently retired.
    uint16_t store = 0;      ///< Per-process store identity, never reused.
    /// Compares the complete identity, including owner and generation.
    bool operator==(const Handle&) const = default;
};

} // namespace lmx
