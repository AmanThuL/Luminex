//----------------------------------------------------------------------------------------------------------------------
/// @file DeliverPerDrawData.h
/// @brief Declares the single per-draw dynamic-data delivery seam FrameDataBench measures.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include <rojoRHI/RHI.h>

#include <cstddef>
#include <span>
#include <type_traits>

namespace lmx::bench {

/// The root block FrameDataQuad reads: one address of the draw's parameter block (must match
/// Shaders/Tests/FrameDataQuad.slang's FrameDataQuadRoot layout exactly).
struct FrameDataQuadRoot {
    rojoRHI::Addr params{}; ///< The draw's QuadParams, followed by any unread padding.
};
static_assert(sizeof(FrameDataQuadRoot) == 8 && alignof(FrameDataQuadRoot) == 8,
              "must match Shaders/Tests/FrameDataQuad.slang's FrameDataQuadRoot layout");
static_assert(std::is_trivially_copyable_v<FrameDataQuadRoot> &&
              std::is_standard_layout_v<FrameDataQuadRoot>);

/// The one seam every dynamic FrameDataBench workload delivers its per-draw block through, and
/// nothing else: the block is copied into the open frame's arena by `uploadFrameData`, and its
/// address reaches the shader through a pushed root. The caller may reuse or free `block` as soon
/// as the call returns. Valid only while a frame is open, since the address dies with the frame.
inline void deliverPerDrawData(rojoRHI::CommandList& commands, std::span<const std::byte> block) {
    commands.pushRoot(FrameDataQuadRoot{.params = commands.uploadFrameData(block)});
}

} // namespace lmx::bench
