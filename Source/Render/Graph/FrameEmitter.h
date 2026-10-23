//----------------------------------------------------------------------------------------------------------------------
/// @file FrameEmitter.h
/// @brief Declares the emitter that records a lowered frame on a command list.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Render/Graph/LoweredFrame.h"

#include <rojoRHI/CommandList.h>

#include <cstdint>
#include <functional>

namespace lmx::render {

/// Runs the body of the pass with the given declaration index.
using PassRunner = std::function<void(uint32_t)>;

/// Records `frame` on `commands`, calling `run` for each RunOp. It is the only graph code that
/// records commands.
///
/// Each BarriersOp becomes one barrier per entry, in order, requesting ResourceAlias visibility
/// for an alias entry; a BeginRenderPassOp becomes a render pass over its attachments; a compute
/// or copy RunOp is wrapped in a compute or copy pass labelled like the pass. Scopes record
/// nothing, because the command list times passes itself. The command list's attachment rules
/// are asserted with the pass named: a colour attachment is always stored, a depth attachment
/// always clears, a pass carrying both clears both, a depth-only pass stores its depth, and a pass
/// has at most rojoRHI::kMaxExtraColorTargets extra colour attachments. A resource an op names
/// with no device object, such as an unplaced transient, asserts with its name.
///
/// @param frame The lowered frame; every resource an op names must be placed.
/// @param commands The command list to record on.
/// @param run The pass body runner.
void emit(const LoweredFrame& frame, rojoRHI::CommandList& commands, const PassRunner& run);

} // namespace lmx::render
