//----------------------------------------------------------------------------------------------------------------------
/// @file GpuVisibilityReadback.cpp
/// @brief Decodes retired GPU states and independently verifies rows, arguments and counters.
//----------------------------------------------------------------------------------------------------------------------
#include "Render/GpuVisibility.h"
#include <algorithm>
#include <numeric>
#include <unordered_map>

namespace lmx::render {
namespace {
//======================================================================================================================
VisibilityCounters decodeCounters(const uint32_t* words) {
    VisibilityCounters result{.candidates = words[0], .visible = words[1], .rejected = words[2]};
    for (uint32_t reason = 1; reason <= 4; ++reason)
        result.bypassed[reason] = words[2 + reason];
    result.emittedRows = words[7];
    result.emittedCommands = words[8];
    result.overflowedRows = words[9];
    result.overflowedCommands = words[10];
    return result;
}
//======================================================================================================================
uint32_t difference(uint32_t actual, uint32_t expected) {
    return actual != expected ? 1u : 0u;
}
} // namespace

//======================================================================================================================
VisibilityStatus GpuVisibility::readback(Pending& pending) {
    auto result = std::move(pending.status);
    result.isRetired = true;
    const auto& params = pending.params;
    std::array<uint32_t, 24> rawCounters{};
    pending.counters->readback(rawCounters.data(), sizeof(rawCounters));
    result.sceneCounters = decodeCounters(rawCounters.data());
    result.shadowCounters = decodeCounters(rawCounters.data() + 12);
    result.submission.listBytes =
        (uint64_t{result.sceneCounters.emittedRows} + result.shadowCounters.emittedRows) *
        sizeof(uint32_t);
    result.overflow = result.sceneCounters.overflowedRows || result.shadowCounters.overflowedRows ||
                      result.sceneCounters.overflowedCommands ||
                      result.shadowCounters.overflowedCommands;
    std::vector<uint32_t> states(std::min(params.candidateCount, params.stateCapacity));
    std::vector<uint32_t> rows(
        result.checkEnabled ? std::min(params.candidateCount, params.rowCapacity) : 0);
    std::vector<rhi::DrawIndexedIndirectArgs> arguments(
        result.checkEnabled ? std::min<uint32_t>(static_cast<uint32_t>(pending.tables.runs.size()),
                                                 params.argumentCapacity)
                            : 0);
    if (!states.empty())
        pending.states->readback(states.data(), states.size() * 4);
    if (!rows.empty())
        pending.rows->readback(rows.data(), rows.size() * 4);
    if (!arguments.empty())
        pending.arguments->readback(arguments.data(), arguments.size() * 20);
    std::array<VisibilityResult*, 2> views{&result.scene, &result.shadow};
    for (uint32_t v = 0; v < 2; ++v) {
        auto& view = *views[v];
        const auto& range = pending.tables.views[v];
        const auto& counts = v == 0 ? result.sceneCounters : result.shadowCounters;
        view.visible = counts.visible;
        view.rejected = counts.rejected;
        view.bypassed = counts.bypassed;
        view.visibleItems.clear();
        std::unordered_map<uint32_t, uint32_t> itemIndices;
        for (uint32_t i = 0; i < view.candidates.size(); ++i)
            itemIndices.emplace(view.candidates[i].instanceRow, i);
        // Candidate metadata stays in original object order for generation-safe editor mapping.
        for (uint32_t i = 0; i < range.candidateCount; ++i) {
            const uint32_t absolute = range.firstCandidate + i;
            const uint32_t row = pending.tables.candidates[absolute].instanceRow;
            const auto item = itemIndices.find(row);
            if (item == itemIndices.end() || absolute >= states.size())
                continue;
            auto found = view.candidates.begin() + item->second;
            const uint32_t state = states[absolute];
            found->state = static_cast<VisibilityState>(state & 3);
            found->reason = static_cast<VisibilityReason>(state >> 2);
            if (found->state != VisibilityState::Rejected)
                view.visibleItems.push_back(static_cast<uint32_t>(found - view.candidates.begin()));
            if (result.checkEnabled) {
                const auto& expected = pending.expected[absolute];
                result.stateMismatches +=
                    difference(state, static_cast<uint32_t>(expected.state) |
                                          (static_cast<uint32_t>(expected.reason) << 2));
            }
        }
        std::sort(view.visibleItems.begin(), view.visibleItems.end());
        const uint32_t bypassed =
            std::accumulate(counts.bypassed.begin(), counts.bypassed.end(), 0u);
        result.counterMismatches +=
            difference(counts.candidates, counts.visible + counts.rejected + bypassed);
        result.counterMismatches +=
            difference(counts.emittedRows + counts.overflowedRows, counts.visible + bypassed);
        if (!result.checkEnabled)
            continue;
        VisibilityCounters expectedCounters;
        expectedCounters.candidates = range.candidateCount;
        for (uint32_t i = 0; i < range.candidateCount; ++i) {
            const auto& expected = pending.expected[range.firstCandidate + i];
            if (expected.state == VisibilityState::Visible)
                ++expectedCounters.visible;
            else if (expected.state == VisibilityState::Rejected)
                ++expectedCounters.rejected;
            else
                ++expectedCounters.bypassed[static_cast<uint32_t>(expected.reason)];
            if (range.firstCandidate + i >= params.stateCapacity &&
                expected.state != VisibilityState::Rejected)
                ++expectedCounters.overflowedRows;
        }
        for (uint32_t r = 0; r < range.runCount; ++r) {
            const uint32_t index = range.firstRun + r;
            const auto& run = pending.tables.runs[index];
            std::vector<uint32_t> retained;
            for (uint32_t i = 0; i < run.candidateCount; ++i) {
                const uint32_t candidate = run.firstCandidate + i;
                if (candidate < params.stateCapacity &&
                    pending.expected[candidate].state != VisibilityState::Rejected)
                    retained.push_back(pending.tables.candidates[candidate].instanceRow);
            }
            const uint32_t available =
                run.firstSlot < params.rowCapacity ? params.rowCapacity - run.firstSlot : 0;
            const uint32_t emitted =
                run.argumentIndex < params.argumentCapacity
                    ? std::min<uint32_t>(static_cast<uint32_t>(retained.size()), available)
                    : 0;
            expectedCounters.emittedRows += emitted;
            expectedCounters.overflowedRows += static_cast<uint32_t>(retained.size()) - emitted;
            expectedCounters.emittedCommands += emitted != 0;
            expectedCounters.overflowedCommands +=
                !retained.empty() && run.argumentIndex >= params.argumentCapacity;
            for (uint32_t i = 0; i < emitted; ++i)
                result.rowMismatches += difference(rows[run.firstSlot + i], retained[i]);
            if (run.argumentIndex >= arguments.size())
                continue;
            auto expected = pending.geometry[run.argumentIndex];
            if (emitted == 0 && params.layout == 0)
                expected = {.instanceCount = 0};
            else {
                expected.instanceCount = emitted;
                expected.firstInstance = run.firstSlot;
            }
            const auto& actual = arguments[run.argumentIndex];
            result.argumentMismatches += difference(actual.indexCount, expected.indexCount);
            result.argumentMismatches += difference(actual.instanceCount, expected.instanceCount);
            result.argumentMismatches += difference(actual.firstIndex, expected.firstIndex);
            result.argumentMismatches += actual.baseVertex != expected.baseVertex;
            result.argumentMismatches += difference(actual.firstInstance, expected.firstInstance);
        }
        result.counterMismatches += difference(counts.visible, expectedCounters.visible);
        result.counterMismatches += difference(counts.rejected, expectedCounters.rejected);
        for (uint32_t i = 1; i <= 4; ++i)
            result.counterMismatches +=
                difference(counts.bypassed[i], expectedCounters.bypassed[i]);
        result.counterMismatches += difference(counts.emittedRows, expectedCounters.emittedRows);
        result.counterMismatches +=
            difference(counts.emittedCommands, expectedCounters.emittedCommands);
        result.counterMismatches +=
            difference(counts.overflowedRows, expectedCounters.overflowedRows);
        result.counterMismatches +=
            difference(counts.overflowedCommands, expectedCounters.overflowedCommands);
    }
    return result;
}

//======================================================================================================================
void GpuVisibility::retireThrough(uint64_t completedFrame) {
    for (auto& pending : m_pending)
        if (pending.status.frameNumber <= completedFrame)
            m_retired.push_back(readback(pending));
    std::erase_if(m_pending, [completedFrame](const auto& pending) {
        return pending.status.frameNumber <= completedFrame;
    });
}

//======================================================================================================================
std::vector<VisibilityStatus> GpuVisibility::takeRetired() {
    auto result = std::move(m_retired);
    m_retired.clear();
    return result;
}
} // namespace lmx::render
