//----------------------------------------------------------------------------------------------------------------------
/// @file Math.h
/// @brief Provides integer work-count arithmetic for dispatches and compressed image blocks.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include <cstdint>

namespace lmx {

/// Rounds the quotient up; divisor must be non-zero and value + divisor - 1 must fit uint32_t.
constexpr uint32_t divRoundUp(uint32_t value, uint32_t divisor) {
    return (value + divisor - 1) / divisor;
}

} // namespace lmx
