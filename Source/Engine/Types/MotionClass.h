//----------------------------------------------------------------------------------------------------------------------
/// @file MotionClass.h
/// @brief Declares how a draw's motion vectors are produced.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>

namespace lmx::render {

/// How a draw's motion is produced.
enum class MotionClass : uint8_t {
    Rigid,  ///< Reprojected through the item's previous model matrix.
    Invalid ///< Writes the kMotionInvalid sentinel; history must not be reprojected.
};

} // namespace lmx::render
