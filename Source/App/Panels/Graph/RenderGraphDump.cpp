//----------------------------------------------------------------------------------------------------------------------
/// @file RenderGraphDump.cpp
/// @brief Writes the displayed compiled frame to its deterministic text dump.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Panels/Graph/RenderGraphPanelInternal.h"

#include "Core/Diagnostics/Log.h"
#include "Render/Graph/GraphDump.h"

#include <filesystem>
#include <format>
#include <fstream>

namespace lmx::app::graph_panel {

//======================================================================================================================
ActionResult dumpFrame(const render::CompiledFrameRecord& record, uint64_t frameId) {
    const std::string filename = std::format("graph-dump-frame-{}.txt", frameId);
    std::error_code error;
    const auto absolute = std::filesystem::absolute(filename, error);
    if (error) {
        return {ActionStatus::Failed, "Cannot resolve output directory: " + error.message(),
                filename};
    }
    std::ofstream file(absolute, std::ios::binary | std::ios::trunc);
    if (!file) {
        return {ActionStatus::Failed, "Cannot open output. Check directory permissions and retry.",
                absolute.string()};
    }
    file << render::dumpCompiledFrame(record);
    file.close();
    if (!file) {
        return {ActionStatus::Failed, "Writing the graph dump failed. Check free space and retry.",
                absolute.string()};
    }
    LMX_LOG_INFO("render-graph frame {} dumped to '{}'", frameId, absolute.string());
    return {ActionStatus::Succeeded, std::format("Saved displayed frame {}.", frameId),
            absolute.string()};
}

} // namespace lmx::app::graph_panel
