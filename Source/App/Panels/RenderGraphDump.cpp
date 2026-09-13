//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphDump.cpp
/// @brief Writes the displayed compiled frame to its deterministic text dump.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/RenderGraphPanelInternal.h"

#include "Core/Log.h"
#include "Render/GraphDump.h"

#include <filesystem>
#include <format>
#include <fstream>

namespace lmx::app::graph_panel {

//======================================================================================================================
void dumpFrame(const render::CompiledFrameRecord& record, uint64_t frameId) {
    const std::string filename = std::format("graph-dump-frame-{}.txt", frameId);
    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    if (file) {
        file << render::dumpCompiledFrame(record);
        LMX_LOG_INFO("render-graph frame {} dumped to '{}'", frameId,
                     std::filesystem::absolute(filename).string());
    } else {
        LMX_LOG_ERROR("Render Graph panel: cannot open '{}' for writing", filename);
    }
}

} // namespace lmx::app::graph_panel
