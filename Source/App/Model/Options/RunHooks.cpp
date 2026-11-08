//----------------------------------------------------------------------------------------------------------------------
/// @file RunHooks.cpp
/// @brief Implements the frame-numbered automation hooks of a bounded windowed run.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Model/Options/RunHooks.h"

#include "Core/Util/Parse.h"

namespace lmx::app {

//======================================================================================================================
std::optional<uint64_t> parseFrameHook(std::string_view text) {
    uint64_t frame = 0;
    if (!parseNumber(text, frame)) {
        return std::nullopt;
    }
    return frame;
}

//======================================================================================================================
std::optional<WindowSize> resizeHookSize(uint64_t resizeAtFrame, uint64_t frame) {
    if (resizeAtFrame == 0 || frame < resizeAtFrame) {
        return std::nullopt;
    }
    // Subtracting keeps a resizeAtFrame near the uint64_t limit from wrapping the grow frame.
    const uint64_t sinceShrink = frame - resizeAtFrame;
    if (sinceShrink == 0) {
        return kResizeHookSmallSize;
    }
    if (sinceShrink == kResizeHookHoldFrames) {
        return kWindowedSize;
    }
    return std::nullopt;
}

} // namespace lmx::app
