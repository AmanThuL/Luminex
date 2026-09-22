//----------------------------------------------------------------------------------------------------------------------
/// @file Dispatch.h
/// @brief Computes threadgroup counts for the shared eight-by-eight compute kernels.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Core/Math/Scalar.h"

#include <array>
#include <cstdint>

namespace lmx::render {

/// Thread count on each axis of the shared two-dimensional compute group.
constexpr uint32_t kComputeThreadsPerGroup2D = 8;
/// Returns the X and Y group counts covering the extent; a zero axis needs no groups.
/// Each extent plus seven must fit uint32_t, as required by Core divRoundUp.
constexpr std::array<uint32_t, 2> dispatchGroups2D(uint32_t width, uint32_t height) {
    return {divRoundUp(width, kComputeThreadsPerGroup2D),
            divRoundUp(height, kComputeThreadsPerGroup2D)};
}

} // namespace lmx::render
