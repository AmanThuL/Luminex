//----------------------------------------------------------------------------------------------------------------------
/// @file FrameEmitter.h
/// @brief Declares the emitter that records a lowered frame on a command list.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Graph/LoweredFrame.h"

#include <rojoRHI/RHI.h>

#include <cstdint>
#include <functional>

namespace lmx::render {

/// Runs the body of the pass with the given declaration index.
using PassRunner = std::function<void(uint32_t)>;

/// Records the lowered physical operations, calling run for each pass body.
/// Each nonempty BarriersOp becomes one barrier batch. Raster passes set viewport/scissor;
/// compute/copy scopes use the recorded labels. All named transient resources must be placed.
/// The frame and its borrowed objects must outlive this call. This is the graph's only recorder.
void emit(const LoweredFrame& frame, rojoRHI::CommandList& commands, const PassRunner& run);

} // namespace lmx::render
