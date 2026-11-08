//----------------------------------------------------------------------------------------------------------------------
/// @file HzbFrameData.h
/// @brief Declares the HZB frame data and the pure functions that build it.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>

namespace lmx::render {

/// Returns the ceil-sized active dimension at pyramid level; source extent is nonzero.
uint32_t hzbLevelExtent(uint32_t sourceExtent, uint32_t level);

/// Mirrors Shaders/Passes/Occlusion/HzbReduce.slang's constant block.
struct HzbReduceParams {
    uint32_t sourceWidth;       ///< Source mip width, pixels.
    uint32_t sourceHeight;      ///< Source mip height, pixels.
    uint32_t destinationWidth;  ///< Destination mip width, pixels.
    uint32_t destinationHeight; ///< Destination mip height, pixels.
    uint32_t sourceLevel;       ///< Source mip level; the depth texture reads level zero.
};
static_assert(sizeof(HzbReduceParams) == 20);

/// Builds the reduction of pyramid `level` for an active source rectangle. Level zero reads the
/// depth texture at the active extent itself; a deeper level reads the previous level, whose extent
/// is the active extent halved `level` times, rounding up. The destination is always the active
/// extent halved `level + 1` times, rounding up.
HzbReduceParams buildHzbReduceParams(uint32_t activeWidth, uint32_t activeHeight, uint32_t level);

/// Plain values the HZB debug view's constants are built from.
struct HzbDebugInputs {
    uint32_t requestedLevel = 0; ///< Mip the caller asked to see.
    uint32_t levelCount = 1;     ///< Mips the pyramid holds; at least one.
    uint32_t activeWidth = 0;    ///< Pyramid source active width, pixels.
    uint32_t activeHeight = 0;   ///< Pyramid source active height, pixels.
    uint32_t outputWidth = 0;    ///< Debug target width, pixels.
    uint32_t outputHeight = 0;   ///< Debug target height, pixels.
};

/// Builds the HZB debug view's eight-word constant block: the requested mip clamped to the last
/// built mip, the active extent, a zero, the output extent, then two zero words.
std::array<uint32_t, 8> buildHzbDebugParams(const HzbDebugInputs& inputs);

} // namespace lmx::render
