//----------------------------------------------------------------------------------------------------------------------
/// @file GpuVisibilityReadback.cpp
/// @brief Decodes retired GPU states and independently verifies rows, arguments and counters.
//----------------------------------------------------------------------------------------------------------------------
#include "Core/Diagnostics/Assert.h"
#include "Render/Passes/Visibility/GpuVisibility.h"
#include "Render/Passes/Visibility/VisibilityRetirement.h"
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
    result.occluded = words[11];
    result.occlusionTested = words[12];
    result.historyInvalid = words[13];
    result.nearCrossing = words[14];
    result.outsideSource = words[15];
    result.rectTooLarge = words[16];
    result.disabled = words[17];
    return result;
}
//======================================================================================================================
uint32_t difference(uint32_t actual, uint32_t expected) {
    return actual != expected ? 1u : 0u;
}

//======================================================================================================================
Result<VisibilityStatus> stageReadback(engine::GpuContext& gpu,
                                       const VisibilityPendingReadback& pending) {
    auto result = pending.projection ? pending.projection->status : pending.status;
    auto expectedStates = pending.expected;
    result.isRetired = true;
    const auto& params = pending.params;
    const VisibilityReadbackTickets& tickets = *pending.tickets;
    auto resolveInto = [&](const std::optional<ReadbackTicket>& ticket, auto& values,
                           rojoRHI::Buffer identity, uint64_t bytes) -> Result<void> {
        const auto resolved = resolve(gpu, *ticket, resolveBytes(std::span(values)));
        if (!resolved)
            return std::unexpected(resolved.error());
        LMX_ASSERT(ticket->frame == pending.frameNumber && ticket->buffer == identity &&
                       ticket->texture.bits == 0 && ticket->region.offset == 0 &&
                       ticket->region.size == bytes,
                   "visibility retirement: ticket identity or region differs from declaration");
        return {};
    };
    std::array<uint32_t, 2 * kVisibilityCounterWords> rawCounters{};
    auto resolved = resolveInto(tickets.counters, rawCounters, pending.backing->counters.rhi(),
                                2 * kVisibilityCounterWords * sizeof(uint32_t));
    if (!resolved)
        return std::unexpected(resolved.error());
    result.sceneCounters = decodeCounters(rawCounters.data());
    result.shadowCounters = decodeCounters(rawCounters.data() + kVisibilityCounterWords);
    result.submission.listBytes =
        (uint64_t{result.sceneCounters.emittedRows} + result.shadowCounters.emittedRows) *
        sizeof(uint32_t);
    result.overflow = result.sceneCounters.overflowedRows || result.shadowCounters.overflowedRows ||
                      result.sceneCounters.overflowedCommands ||
                      result.shadowCounters.overflowedCommands;
    std::vector<uint32_t> states(std::min(params.candidateCount, params.stateCapacity));
    std::vector<uint32_t> rows(
        result.checkEnabled ? std::min(params.candidateCount, params.rowCapacity) : 0);
    std::vector<rojoRHI::DrawIndexedIndirectArgs> arguments(
        result.checkEnabled ? std::min<uint32_t>(static_cast<uint32_t>(pending.tables.runs.size()),
                                                 params.argumentCapacity)
                            : 0);
    resolved = resolveInto(tickets.states, states, pending.backing->states.rhi(),
                           uint64_t{std::min(params.candidateCount, params.stateCapacity)} * 4);
    if (!resolved)
        return std::unexpected(resolved.error());
    resolved = resolveInto(tickets.rows, rows, pending.rowBuffer,
                           uint64_t{std::min(params.candidateCount, params.rowCapacity)} * 4);
    if (!resolved)
        return std::unexpected(resolved.error());
    resolved =
        resolveInto(tickets.arguments, arguments, pending.argumentBuffer,
                    uint64_t{std::min<uint32_t>(static_cast<uint32_t>(pending.tables.runs.size()),
                                                params.argumentCapacity)} *
                        sizeof(rojoRHI::DrawIndexedIndirectArgs));
    if (!resolved)
        return std::unexpected(resolved.error());
    LMX_ASSERT(result.frameNumber == pending.frameNumber,
               "visibility retirement: status frame identity differs from declaration");
    std::array<VisibilityResult*, 2> views{&result.scene, &result.shadow};
    for (uint32_t v = 0; v < 2; ++v) {
        auto& view = *views[v];
        const auto& range = pending.tables.views[v];
        const auto& counts = v == 0 ? result.sceneCounters : result.shadowCounters;
        view.visible = counts.visible;
        view.rejected = counts.rejected;
        view.disabled = counts.disabled;
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
            found->reason = static_cast<VisibilityReason>((state >> 2) & 63u);
            found->occlusion = projectOcclusionBounds(found->worldBounds, result.occlusionParams);
            found->occlusion.outcome = static_cast<OcclusionOutcome>((state >> 8) & 15u);
            found->occlusion.occluded = found->reason == VisibilityReason::Occluded;
            if (found->state != VisibilityState::Rejected)
                view.visibleItems.push_back(static_cast<uint32_t>(found - view.candidates.begin()));
            if (result.checkEnabled) {
                auto& expected = expectedStates[absolute];
                if (found->reason == VisibilityReason::Occluded) {
                    result.stateMismatches += expected.state != VisibilityState::Visible ||
                                              found->state != VisibilityState::Rejected;
                    expected.state = VisibilityState::Rejected;
                    expected.reason = VisibilityReason::Occluded;
                } else {
                    result.stateMismatches +=
                        difference(state & 255u, static_cast<uint32_t>(expected.state) |
                                                     (static_cast<uint32_t>(expected.reason) << 2));
                }
            }
        }
        std::sort(view.visibleItems.begin(), view.visibleItems.end());
        const uint32_t bypassed =
            std::accumulate(counts.bypassed.begin(), counts.bypassed.end(), 0u);
        result.counterMismatches +=
            difference(counts.candidates + counts.disabled, range.candidateCount);
        result.counterMismatches +=
            difference(counts.candidates, counts.visible + counts.rejected + bypassed);
        result.counterMismatches +=
            difference(counts.emittedRows + counts.overflowedRows, counts.visible + bypassed);
        uint32_t occluded = 0, tested = 0, invalid = 0, near = 0, outside = 0, large = 0;
        for (const auto& candidate : view.candidates) {
            occluded += candidate.occlusion.occluded;
            const auto outcome = candidate.occlusion.outcome;
            invalid += outcome == OcclusionOutcome::HistoryInvalid;
            near += outcome == OcclusionOutcome::NearCrossing;
            outside += outcome == OcclusionOutcome::OutsideSource;
            large += outcome == OcclusionOutcome::RectTooLarge;
            tested += candidate.occlusion.occluded || (outcome != OcclusionOutcome::NotTested &&
                                                       outcome != OcclusionOutcome::HistoryInvalid);
        }
        result.counterMismatches += difference(counts.occluded, occluded);
        result.counterMismatches += difference(counts.occlusionTested, tested);
        result.counterMismatches += difference(counts.historyInvalid, invalid);
        result.counterMismatches += difference(counts.nearCrossing, near);
        result.counterMismatches += difference(counts.outsideSource, outside);
        result.counterMismatches += difference(counts.rectTooLarge, large);
        if (!result.checkEnabled)
            continue;
        VisibilityCounters expectedCounters;
        expectedCounters.candidates = range.candidateCount;
        for (uint32_t i = 0; i < range.candidateCount; ++i) {
            const auto& expected = expectedStates[range.firstCandidate + i];
            if (expected.reason == VisibilityReason::AuthoredOff) {
                ++expectedCounters.disabled;
                --expectedCounters.candidates;
            } else if (expected.state == VisibilityState::Visible)
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
                    expectedStates[candidate].state != VisibilityState::Rejected)
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
                expected.drawId = run.firstSlot;
                expected.baseVertex = 0;
                expected.firstInstance = 0;
            }
            const auto& actual = arguments[run.argumentIndex];
            result.argumentMismatches += difference(actual.drawId, expected.drawId);
            result.argumentMismatches += difference(actual.indexCount, expected.indexCount);
            result.argumentMismatches += difference(actual.instanceCount, expected.instanceCount);
            result.argumentMismatches += difference(actual.firstIndex, expected.firstIndex);
            result.argumentMismatches += actual.baseVertex != expected.baseVertex;
            result.argumentMismatches += difference(actual.firstInstance, expected.firstInstance);
        }
        result.counterMismatches += difference(counts.candidates, expectedCounters.candidates);
        result.counterMismatches += difference(counts.disabled, expectedCounters.disabled);
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

} // namespace

//======================================================================================================================
Result<VisibilityRetirementBatch>
prepareVisibilityRetirement(engine::GpuContext& gpu,
                            const std::vector<VisibilityPendingReadback>& pending,
                            const std::vector<VisibilityStatus>& retired, uint64_t completedFrame) {
    VisibilityRetirementBatch staged;
    for (size_t index = 0; index < pending.size(); ++index) {
        const auto& entry = pending[index];
        if (entry.frameNumber > completedFrame)
            continue;
        staged.retiring.push_back(index);
        LMX_ASSERT(entry.tickets && entry.backing && !entry.tickets->discarded,
                   "visibility retirement: missing live declaration backing");
        const auto& tickets = *entry.tickets;
        const uint32_t mask = uint32_t(tickets.counters.has_value()) |
                              (uint32_t(tickets.states.has_value()) << 1) |
                              (uint32_t(tickets.rows.has_value()) << 2) |
                              (uint32_t(tickets.arguments.has_value()) << 3);
        if (mask != 15) {
            // A recorded operation's own error has priority over incomplete recording.
            const std::array<const std::optional<ReadbackTicket>*, 4> outcomes{
                &tickets.counters, &tickets.states, &tickets.rows, &tickets.arguments};
            for (const auto* outcome : outcomes)
                if (*outcome) {
                    std::vector<uint8_t> bytes((**outcome).region.size);
                    const auto resolved = resolve(gpu, **outcome, bytes);
                    if (!resolved)
                        return std::unexpected(resolved.error());
                }
            LMX_ASSERT(
                mask == 0 || tickets.entered || !entry.callbackAnchor.expired(),
                "visibility retirement: partial readback has no recording entry or callback owner");
            if (mask != 0 || tickets.entered || !entry.callbackAnchor.expired())
                return std::unexpected(Error{ErrorCode::InvalidArgument,
                                             "visibility recording is incomplete or borrowed"});
            LMX_ASSERT(entry.status.frameNumber == entry.frameNumber,
                       "visibility retirement: unrecorded frame identity differs");
            staged.discards.push_back(entry.tickets);
            continue;
        }
        auto result = stageReadback(gpu, entry);
        if (!result)
            return std::unexpected(result.error());
        const auto readyIndex = staged.ready.size();
        staged.ready.push_back(std::move(*result));
        if (entry.projection)
            staged.projections.push_back({entry.projection, readyIndex, {}});
    }
    // The commit prepends the live records into this capacity, so it cannot allocate.
    staged.ready.reserve(retired.size() + staged.ready.size());
    return staged;
}

//======================================================================================================================
void prepareVisibilityProjections(VisibilityRetirementBatch& staged) {
    for (auto& update : staged.projections)
        update.value = staged.ready[update.readyIndex];
}

//======================================================================================================================
void commitVisibilityRetirement(VisibilityRetirementBatch& staged,
                                std::vector<VisibilityPendingReadback>& pending,
                                std::vector<VisibilityStatus>& retired) noexcept {
    for (const auto& tickets : staged.discards)
        tickets->discarded = true;
    for (auto& update : staged.projections)
        std::swap(update.target->status, update.value);
    eraseRetiring(pending, staged.retiring);
    LMX_ASSERT(staged.ready.capacity() >= retired.size() + staged.ready.size(),
               "visibility retirement: prepared capacity cannot hold the live records");
    staged.ready.insert(staged.ready.begin(), std::make_move_iterator(retired.begin()),
                        std::make_move_iterator(retired.end()));
    retired.swap(staged.ready);
    staged.ready.clear();
}

//======================================================================================================================
Result<void> retireVisibilityThrough(engine::GpuContext& gpu,
                                     std::vector<VisibilityPendingReadback>& pending,
                                     std::vector<VisibilityStatus>& retired,
                                     uint64_t completedFrame) {
    auto staged = prepareVisibilityRetirement(gpu, pending, retired, completedFrame);
    if (!staged)
        return std::unexpected(staged.error());
    prepareVisibilityProjections(*staged);
    commitVisibilityRetirement(*staged, pending, retired);
    return {};
}

//======================================================================================================================
Result<PreparedVisibilityRetirement>
GpuVisibility::prepareRetiredTake(uint64_t completedFrame) const {
    auto batch = prepareVisibilityRetirement(m_gpu, m_pending, m_retired, completedFrame);
    if (!batch)
        return std::unexpected(batch.error());
    PreparedVisibilityRetirement prepared;
    prepared.m_owner = this;
    prepared.m_context = &m_gpu;
    prepared.m_epoch = m_epoch;
    prepared.m_batch = std::move(*batch);
    prepareVisibilityProjections(prepared.m_batch);
    return prepared;
}

//======================================================================================================================
void GpuVisibility::validateRetiredTake(const PreparedVisibilityRetirement& prepared) const {
    LMX_ASSERT(prepared.m_owner == this && prepared.m_context == &m_gpu &&
                   prepared.m_epoch == m_epoch,
               "visibility prepared take is foreign, stale, or consumed");
}

//======================================================================================================================
std::vector<VisibilityStatus>
GpuVisibility::commitRetiredTake(PreparedVisibilityRetirement&& prepared) noexcept {
    validateRetiredTake(prepared);
    commitVisibilityRetirement(prepared.m_batch, m_pending, m_retired);
    prepared.m_owner = nullptr;
    ++m_epoch;
    auto result = std::move(m_retired);
    m_retired.clear();
    return result;
}

//======================================================================================================================
Result<void> GpuVisibility::retireThrough(uint64_t completedFrame) {
    auto staged = prepareRetiredTake(completedFrame);
    if (!staged)
        return std::unexpected(staged.error());
    validateRetiredTake(*staged);
    commitVisibilityRetirement(staged->m_batch, m_pending, m_retired);
    staged->m_owner = nullptr;
    ++m_epoch;
    return {};
}

//======================================================================================================================
std::vector<VisibilityStatus> GpuVisibility::takeRetired() {
    auto result = std::move(m_retired);
    m_retired.clear();
    ++m_epoch;
    return result;
}
} // namespace lmx::render
