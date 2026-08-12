//----------------------------------------------------------------------------------------------------------------------
/// @file DeliverPerDrawData.h
/// @brief Declares the single per-draw dynamic-data delivery seam FrameDataBench measures.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "RHI/RHI.h"

#include <cstdint>

namespace lmx::bench {

/// The one production-owned seam every dynamic FrameDataBench workload delivers its per-draw block
/// through, and nothing else. Implemented today with `CommandList::setUniforms`, exactly matching
/// the pre-migration production renderer; M5.2 Stage 3 changes only this function's body to call
/// `CommandList::bindFrameData` once the candidate path exists, so the same benchmark binary keeps
/// measuring both the baseline and (source-unmodified elsewhere) the post-migration candidate.
///
/// `slot` is the argument-table buffer slot the caller's pipeline reads the block from; `data`/
/// `size` name the bytes to copy, which the caller may reuse or free immediately after the call
/// returns. Valid only inside a render or compute pass, per `setUniforms`' own contract.
inline void deliverPerDrawData(rhi::CommandList& commands, uint32_t slot, const void* data,
                               uint64_t size) {
    commands.setUniforms(slot, data, size);
}

} // namespace lmx::bench
