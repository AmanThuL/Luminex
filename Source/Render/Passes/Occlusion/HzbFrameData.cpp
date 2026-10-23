//----------------------------------------------------------------------------------------------------------------------
/// @file HzbFrameData.cpp
/// @brief Builds the HZB reduction and debug view frame data from plain values.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Occlusion/HzbFrameData.h"
#include "Render/Passes/Occlusion/HzbStage.h"

#include <algorithm>

namespace lmx::render {

//======================================================================================================================
HzbReduceParams buildHzbReduceParams(uint32_t activeWidth, uint32_t activeHeight, uint32_t level) {
    const bool fromDepth = level == 0;
    return HzbReduceParams{
        .sourceWidth = fromDepth ? activeWidth : hzbLevelExtent(activeWidth, level - 1),
        .sourceHeight = fromDepth ? activeHeight : hzbLevelExtent(activeHeight, level - 1),
        .destinationWidth = hzbLevelExtent(activeWidth, level),
        .destinationHeight = hzbLevelExtent(activeHeight, level),
        .sourceLevel = fromDepth ? 0 : level - 1};
}

//======================================================================================================================
std::array<uint32_t, 8> buildHzbDebugParams(const HzbDebugInputs& inputs) {
    return {std::min(inputs.requestedLevel, inputs.levelCount - 1),
            inputs.activeWidth,
            inputs.activeHeight,
            0,
            inputs.outputWidth,
            inputs.outputHeight,
            0,
            0};
}

} // namespace lmx::render
