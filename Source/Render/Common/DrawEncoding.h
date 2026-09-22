//----------------------------------------------------------------------------------------------------------------------
/// @file DrawEncoding.h
/// @brief Shares scene table binding and the existing direct or indirect draw-run loop.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Engine/Scene/DrawItem.h"
#include "Engine/Scene/SceneTables.h"
#include "Render/Passes/Scene/DrawSubmission.h"

#include <rojoRHI/RHI.h>

namespace lmx::render {

struct SceneTableSlots {
    uint32_t vertices;
    uint32_t instances;
    uint32_t materials;
};

inline void bindSceneTables(rojoRHI::CommandList& commands, const engine::SceneTables& tables,
                            const SceneTableSlots& slots) {
    commands.bindBuffer(slots.vertices, *tables.vertices);
    commands.bindBuffer(slots.instances, *tables.instances);
    commands.bindBuffer(slots.materials, *tables.materials);
}

struct DrawEncodingInputs {
    const DrawList& list;
    rojoRHI::Buffer* indices;
    rojoRHI::GraphicsPipeline* initialPipeline = nullptr;
};

// PerRun selects and binds the pipeline through the supplied cache-aware callable, binds its
// material state, then returns the representative item. All callbacks inline into the pass body.
template <class PerRun>
void encodeDrawRuns(rojoRHI::CommandList& commands, const DrawEncodingInputs& draws,
                    PerRun&& perRun) {
    rojoRHI::GraphicsPipeline* bound = draws.initialPipeline;
    const auto bindPipeline = [&](rojoRHI::GraphicsPipeline& pipeline) {
        if (&pipeline != bound) {
            commands.bindPipeline(pipeline);
            bound = &pipeline;
        }
    };
    commands.bindBuffer(engine::kVisibleRowsSlot, *draws.list.rows);
    if (draws.list.mode != SubmissionMode::Direct) {
        commands.bindFrameData(engine::kDrawUniformsSlot, engine::DrawUniforms{0});
    }
    for (const auto& run : draws.list.runs) {
        const engine::DrawItem& item = perRun(run, bindPipeline);
        if (draws.list.mode == SubmissionMode::Direct) {
            commands.bindFrameData(engine::kDrawUniformsSlot, engine::DrawUniforms{run.firstEntry});
            commands.drawIndexed(*draws.indices, item.mesh.indexCount, item.mesh.firstIndex);
        } else {
            commands.drawIndexedIndirect(*draws.indices, *draws.list.arguments,
                                         uint64_t{run.argumentIndex} *
                                             sizeof(rojoRHI::DrawIndexedIndirectArgs));
        }
    }
}

} // namespace lmx::render
