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

inline GraphBuffer importPaced(RenderGraph& graph, rojoRHI::Buffer& buffer, std::string_view name,
                               rojoRHI::BufferUse use, bool used) {
    return used ? graph.importBuffer(buffer, name, use) : graph.importBuffer(buffer, name);
}
inline void declareZeroFill(RenderGraph& graph, rojoRHI::CommandList& commands,
                            std::string_view passName, GraphBuffer target, uint64_t bytes) {
    CopyPassDesc reset;
    reset.bufferDestinations.push_back(target);
    graph.addCopyPass(passName, std::move(reset),
                      [&commands, target, bytes](const PassResources& resources) {
                          commands.fillBuffer(lmx::render::buffer(resources, target), 0, bytes, 0);
                      });
}
// Process in insertion order, then erase the completed entries; projection remains valid after
// the callback (including when that callback moves the owned payload out of an entry).
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
template <class Retired>
std::vector<Retired> takeRetired(std::vector<Retired>& retired) {
    auto result = std::move(retired);
    retired.clear();
    return result;
}

} // namespace lmx::render
