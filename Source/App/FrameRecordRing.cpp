//----------------------------------------------------------------------------------------------------------------------
/// @file FrameRecordRing.cpp
/// @brief Implements retention of compiled frame records and their join to retired GPU timings.
//----------------------------------------------------------------------------------------------------------------------

#include "App/FrameRecordRing.h"

#include <utility>

namespace lmx::app {

//======================================================================================================================
void FrameRecordRing::retain(render::CompiledFrameRecord record) {
    if (m_frames.size() == kCapacity) {
        m_frames.erase(m_frames.begin());
    }
    m_frames.push_back({.record = std::move(record), .timings = {}, .timed = false});
}

//======================================================================================================================
bool FrameRecordRing::joinTimings(uint64_t frameId, std::span<const rhi::PassTiming> timings) {
    if (frameId == 0) {
        return false;
    }
    for (RetainedFrame& frame : m_frames) {
        if (frame.record.frameId != frameId) {
            continue;
        }
        frame.timings.assign(timings.begin(), timings.end());
        frame.timed = true;
        return true;
    }
    return false;
}

//======================================================================================================================
const RetainedFrame* FrameRecordRing::newestTimedFrame() const {
    for (size_t index = m_frames.size(); index > 0; --index) {
        if (m_frames[index - 1].timed) {
            return &m_frames[index - 1];
        }
    }
    return nullptr;
}

//======================================================================================================================
const RetainedFrame* FrameRecordRing::find(uint64_t frameId) const {
    for (const RetainedFrame& frame : m_frames) {
        if (frame.record.frameId == frameId) {
            return &frame;
        }
    }
    return nullptr;
}

} // namespace lmx::app
