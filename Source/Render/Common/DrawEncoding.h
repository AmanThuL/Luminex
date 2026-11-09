//----------------------------------------------------------------------------------------------------------------------
/// @file DrawEncoding.h
/// @brief Shares the direct and indirect draw-run loop with cache-aware value pipelines.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Assert.h"
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

/// PerRun selects a pipeline through the cache-aware callable, pushes a direct run's draw root,
/// declares actual shader accesses, and returns the representative item. Indirect and batched runs
/// share the root the pass pushed once and read drawId; direct runs read their root's firstEntry.
/// Both paths use zero base vertex and first instance. The list's leading counted draws come first:
/// PerRun runs for each of their runs, so one pipeline is bound and every declared access is
/// pending, then one drawIndexedIndirect covers their consecutive records with no count.
//======================================================================================================================
template <class PerRun>
void encodeDrawRuns(rojoRHI::CommandList& commands, const DrawEncodingInputs& draws,
                    PerRun&& perRun) {
    rojoRHI::Pipeline bound = draws.initialPipeline;
    bool pipelineFixed = false;
    const auto bindPipeline = [&](rojoRHI::Pipeline pipeline) {
        if (pipeline.bits != bound.bits) {
            LMX_ASSERT(!pipelineFixed, "a counted draw's runs must share one pipeline");
            commands.setPipeline(pipeline);
            bound = pipeline;
        }
    };
    const auto& runs = draws.list.runs;
    LMX_ASSERT(draws.list.countedDraws.empty() || draws.list.mode != SubmissionMode::Direct,
               "direct submission has no counted draws");
    size_t next = 0;
    for (const uint32_t count : draws.list.countedDraws) {
        LMX_ASSERT(count != 0 && count <= runs.size() - next,
                   "counted draws must cover leading runs");
        const uint32_t firstArgument = runs[next].argumentIndex;
        for (uint32_t i = 0; i < count; ++i, ++next) {
            LMX_ASSERT(runs[next].argumentIndex == firstArgument + i,
                       "a counted draw's arguments must be consecutive");
            perRun(runs[next], bindPipeline);
            pipelineFixed = true;
        }
        pipelineFixed = false;
        commands.drawIndexedIndirect(draws.indices->rhi(), rojoRHI::IndexType::UInt32,
                                     draws.list.arguments->address(uint64_t{firstArgument} * 24),
                                     count);
    }
    for (; next < runs.size(); ++next) {
        const auto& run = runs[next];
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
