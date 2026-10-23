//----------------------------------------------------------------------------------------------------------------------
/// @file PacedSlots.h
/// @brief Shares paced-buffer imports, counter reset declarations and retirement drains.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Common/GraphResources.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

namespace lmx::render {

/// Imports an owned paced buffer, retaining the previous use only when that slot was used.
inline GraphBuffer importPaced(RenderGraph& graph, const engine::GpuBuffer& buffer,
                               std::string_view name, BufferUse use, bool used) {
    return used ? graph.importBuffer(buffer, name, use) : graph.importBuffer(buffer, name);
}
/// Declares a word-aligned zero fill; target must be Gpu memory with ShaderWrite usage.
inline void declareZeroFill(RenderGraph& graph, rojoRHI::CommandList& commands,
                            std::string_view passName, GraphBuffer target, uint64_t bytes) {
    CopyPassDesc reset;
    reset.bufferDestinations.push_back(target);
    graph.addCopyPass(passName, std::move(reset),
                      [&commands, target, bytes](const PassResources& resources) {
                          commands.fillBuffer(lmx::render::buffer(resources, target), 0, bytes, 0);
                      });
}
/// Processes in insertion order, then erases the completed entries; projection remains valid after
/// the callback (including when that callback moves the owned payload out of an entry).
template <class Pending, class FrameOf, class Retire>
void retireThrough(std::vector<Pending>& pending, uint64_t completedFrame, FrameOf frameOf,
                   Retire retire) {
    for (auto& entry : pending) {
        if (frameOf(entry) <= completedFrame) {
            retire(entry);
        }
    }
    std::erase_if(pending, [completedFrame, &frameOf](const auto& entry) {
        return frameOf(entry) <= completedFrame;
    });
}
/// Moves every retired payload out, leaving the retirement collection empty.
template <class Retired>
std::vector<Retired> takeRetired(std::vector<Retired>& retired) {
    auto result = std::move(retired);
    retired.clear();
    return result;
}

} // namespace lmx::render
