//----------------------------------------------------------------------------------------------------------------------
/// @file LightCheckCapture.cpp
/// @brief Serializes checked light lists without native padding or host-endian assumptions.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/Capture/LightCheckCapture.h"

#include <ostream>

namespace lmx::app {
namespace {
//======================================================================================================================
void word(std::ostream& stream, uint32_t value) {
    for (uint32_t shift = 0; shift < 32; shift += 8)
        stream.put(static_cast<char>((value >> shift) & 0xff));
}

//======================================================================================================================
void counters(std::ostream& stream, const render::LightClusterCounters& value) {
    word(stream, value.candidates);
    word(stream, value.assigned);
    word(stream, value.droppedPerCluster);
    word(stream, value.droppedGlobal);
    word(stream, value.truncatedFroxels);
    word(stream, value.maxCount);
}
} // namespace

//======================================================================================================================
bool writeLightCheckHeader(std::ostream& stream) {
    stream.write("LMXLC01\0", 8);
    return stream.good();
}

//======================================================================================================================
bool writeLightCheckFrame(std::ostream& stream, const render::LightClusterCheckFrame& frame) {
    if (frame.gpu.grid.size() != render::kClusterCount ||
        frame.cpu.grid.size() != render::kClusterCount ||
        frame.gpu.indices.size() != frame.gpu.counters.assigned ||
        frame.cpu.indices.size() != frame.cpu.counters.assigned)
        return false;
    word(stream, static_cast<uint32_t>(frame.frameNumber));
    word(stream, static_cast<uint32_t>(frame.frameNumber >> 32));
    word(stream, frame.params.activeWidth);
    word(stream, frame.params.activeHeight);
    word(stream, frame.params.rowCount);
    word(stream, frame.params.perClusterCap);
    word(stream, frame.params.globalCapacity);
    word(stream, render::kClusterCount);
    word(stream, static_cast<uint32_t>(frame.gpu.indices.size()));
    word(stream, static_cast<uint32_t>(frame.cpu.indices.size()));
    counters(stream, frame.gpu.counters);
    counters(stream, frame.cpu.counters);
    for (const auto& grid : {&frame.gpu.grid, &frame.cpu.grid})
        for (const auto& record : *grid) {
            word(stream, record.offset);
            word(stream, record.count);
        }
    for (const auto& indices : {&frame.gpu.indices, &frame.cpu.indices})
        for (uint32_t index : *indices)
            word(stream, index);
    return stream.good();
}
} // namespace lmx::app
