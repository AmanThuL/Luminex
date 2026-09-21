//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityTables.cpp
/// @brief Builds absolute canonical ranges for deterministic GPU visibility.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/VisibilityTables.h"
#include "Render/Renderer/SceneView.h"
#include <algorithm>

namespace lmx::render {
//======================================================================================================================
VisibilityTables buildVisibilityTables(const SceneView& view, const PreparedSubmission& submission,
                                       const FrustumPlanes& planes) {
    VisibilityTables result;
    const std::array<const DrawList*, 2> lists{&submission.scene, &submission.shadow};
    for (uint32_t v = 0; v < lists.size(); ++v) {
        auto& params = result.views[v];
        params.planes = planes.planes;
        params.flags =
            (view.visibilityEnabled ? 1u : 0u) | (v == 1 ? 2u : 0u) | (planes.valid ? 4u : 0u);
        params.firstCandidate = static_cast<uint32_t>(result.candidates.size());
        params.firstRun = static_cast<uint32_t>(result.runs.size());
        params.firstChunk = static_cast<uint32_t>(result.chunks.size());
        for (const auto& run : lists[v]->runs) {
            const uint32_t runIndex = static_cast<uint32_t>(result.runs.size());
            const uint32_t first = static_cast<uint32_t>(result.candidates.size());
            result.runs.push_back({first, run.instanceCount, run.firstEntry, run.argumentIndex});
            for (uint32_t i = 0; i < run.instanceCount; ++i)
                result.candidates.push_back({submission.rows[run.firstEntry + i], runIndex});
            if (view.submission == SubmissionMode::Batched)
                for (uint32_t i = 0; i < run.instanceCount; i += kVisibilityChunkSize)
                    result.chunks.push_back({runIndex, first + i,
                                             std::min(kVisibilityChunkSize, run.instanceCount - i),
                                             0});
        }
        params.candidateCount =
            static_cast<uint32_t>(result.candidates.size()) - params.firstCandidate;
        params.runCount = static_cast<uint32_t>(result.runs.size()) - params.firstRun;
        if (view.submission != SubmissionMode::Batched)
            for (uint32_t i = 0; i < params.candidateCount; i += kVisibilityChunkSize)
                result.chunks.push_back({0, params.firstCandidate + i,
                                         std::min(kVisibilityChunkSize, params.candidateCount - i),
                                         0});
        params.chunkCount = static_cast<uint32_t>(result.chunks.size()) - params.firstChunk;
    }
    return result;
}
} // namespace lmx::render
