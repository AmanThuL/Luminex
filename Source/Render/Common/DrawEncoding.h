//----------------------------------------------------------------------------------------------------------------------
/// @file DrawEncoding.h
/// @brief Shares the direct and indirect draw-run loop with cache-aware value pipelines.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Scene/DrawItem.h"
#include "Render/Passes/Scene/DrawSubmission.h"

#include <rojoRHI/RHI.h>

namespace lmx::render {

/// Borrowed geometry and prepared runs for one raster pass.
struct DrawEncodingInputs {
    const DrawList& list;                ///< Current paced runs and argument destination.
    const engine::GpuBuffer* indices;    ///< Index owner, alive through execution.
    rojoRHI::Pipeline initialPipeline{}; ///< Pipeline already selected by the pass, or null.
};

/// PerRun selects a pipeline through the cache-aware callable, pushes the concrete draw root,
/// declares actual shader accesses, and returns the representative item. Draw roots choose either
/// direct firstEntry or indirect drawId; both paths use zero base vertex and first instance.
//======================================================================================================================
template <class PerRun>
void encodeDrawRuns(rojoRHI::CommandList& commands, const DrawEncodingInputs& draws,
                    PerRun&& perRun) {
    rojoRHI::Pipeline bound = draws.initialPipeline;
    const auto bindPipeline = [&](rojoRHI::Pipeline pipeline) {
        if (pipeline.bits != bound.bits) {
            commands.setPipeline(pipeline);
            bound = pipeline;
        }
    };
    for (const auto& run : draws.list.runs) {
        const engine::DrawItem& item = perRun(run, bindPipeline);
        if (draws.list.mode == SubmissionMode::Direct) {
            commands.drawIndexed(draws.indices->rhi(), rojoRHI::IndexType::UInt32,
                                 item.mesh.indexCount, run.instanceCount, item.mesh.firstIndex, 0,
                                 0);
        } else {
            commands.drawIndexedIndirect(
                draws.indices->rhi(), rojoRHI::IndexType::UInt32,
                draws.list.arguments->address(uint64_t{run.argumentIndex} * 24), 1);
        }
    }
}

} // namespace lmx::render
