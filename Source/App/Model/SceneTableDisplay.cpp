//----------------------------------------------------------------------------------------------------------------------
/// @file SceneTableDisplay.cpp
/// @brief Formats coherent read-only scene table diagnostics.
//----------------------------------------------------------------------------------------------------------------------
#include "App/Model/SceneTableDisplay.h"
#include <format>

namespace lmx::app {
//======================================================================================================================
std::array<SceneTableField, 9> sceneTableFields(const engine::SceneTableStats& stats) {
    return {
        {{"Instances", std::format("{} / {} rows", stats.instanceCount, stats.instanceCapacity)},
         {"Materials", std::format("{} / {} rows", stats.materialCount, stats.materialCapacity)},
         {"Meshes", std::format("{} / {} rows", stats.meshCount, stats.meshCapacity)},
         {"Lights", std::format("{} / {} rows", stats.lightCount, stats.lightCapacity)},
         {"Geometry",
          std::format("{} vertex bytes; {} index bytes", stats.vertexBytes, stats.indexBytes)},
         {"Last upload", std::format("{} rows; {} bytes", stats.rowsWritten, stats.bytesWritten)},
         {"Frame slot", std::format("{} / 3", stats.slot + 1)},
         {"Capacity growths", std::format("{}", stats.growthEvents)},
         {"Awaiting retirement", std::format("{} buffers", stats.pendingReleaseBuffers)}}};
}
} // namespace lmx::app
