//----------------------------------------------------------------------------------------------------------------------
/// @file FrameData.h
/// @brief Uploads trivially copyable frame values into the command list's frame arena.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <rojoRHI/RHI.h>

#include <span>
#include <type_traits>

namespace lmx::render {

/// Copies one value into frame-owned storage; its address expires when the frame slot recycles.
template <typename T>
rojoRHI::Addr frameData(rojoRHI::CommandList& commands, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    return commands.uploadFrameData(std::as_bytes(std::span{&value, 1}), alignof(T));
}

} // namespace lmx::render
