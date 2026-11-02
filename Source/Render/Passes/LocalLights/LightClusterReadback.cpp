//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterReadback.cpp
/// @brief Decodes retired froxel-grid counters and, when asked, the grid and index list.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/LocalLights/LightClusterReadback.h"
#include "Render/Common/PacedSlots.h"

#include "Core/Diagnostics/Assert.h"

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <string_view>

namespace lmx::render::light_detail {
namespace {

//======================================================================================================================
Error missing(uint64_t frameNumber, std::string_view outcome) {
    return {ErrorCode::InvalidArgument,
            std::format("light-cluster frame {}: the {} readback was never recorded", frameNumber,
                        outcome)};
}

// Checks one required outcome: its own recorded error first, then that it holds `bytes`.
//======================================================================================================================
Result<const ReadbackTicket*> required(engine::GpuContext& gpu,
                                       const std::optional<ReadbackTicket>& ticket,
                                       uint64_t frameNumber, std::string_view outcome,
                                       uint64_t bytes) {
    if (!ticket) {
        return std::unexpected(missing(frameNumber, outcome));
    }
    // An empty destination still returns the recorded operation's stored error.
    const auto recorded = resolve(gpu, *ticket, {});
    if (!recorded) {
        return std::unexpected(recorded.error());
    }
    if (ticket->region.size < bytes) {
        return std::unexpected(Error{
            ErrorCode::Parse,
            std::format("light-cluster frame {}: the {} readback holds {} bytes, {} are required",
                        frameNumber, outcome, ticket->region.size, bytes)});
    }
    return &*ticket;
}

} // namespace

//======================================================================================================================
std::shared_ptr<ClusterReadbackTickets> LightClusterReadback::enqueue(uint64_t frameNumber,
                                                                      bool captureLists) {
    auto tickets = std::make_shared<ClusterReadbackTickets>();
    m_pending.push_back(
        {.frameNumber = frameNumber, .captureLists = captureLists, .tickets = tickets});
    return tickets;
}

//======================================================================================================================
bool LightClusterReadback::hasPendingSlot(uint64_t frameNumber) const {
    return std::ranges::any_of(m_pending, [frameNumber](const Pending& pending) {
        return pending.frameNumber % 3 == frameNumber % 3;
    });
}

//======================================================================================================================
Result<RetiredLightClusters> LightClusterReadback::readback(engine::GpuContext& gpu,
                                                            const Pending& pending) {
    RetiredLightClusters result{.frameNumber = pending.frameNumber};
    const ClusterReadbackTickets& tickets = *pending.tickets;
    const auto counters =
        required(gpu, tickets.counters, pending.frameNumber, "counters", kCounterBytes);
    if (!counters) {
        return std::unexpected(counters.error());
    }
    std::array<uint32_t, kLightClusterCounterWords> words{};
    const auto resolved = resolve(gpu, **counters, resolveBytes(std::span(words)));
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    result.counters = {.candidates = words[0],
                       .assigned = words[1],
                       .droppedPerCluster = words[2],
                       .droppedGlobal = words[3],
                       .truncatedFroxels = words[4],
                       .maxCount = words[5]};
    if (!pending.captureLists) {
        // Lists nobody asked for are never published, but a recorded failure is not ignored.
        for (const auto* ticket : {&tickets.grid, &tickets.indices}) {
            if (*ticket) {
                const auto recorded = resolve(gpu, **ticket, {});
                if (!recorded) {
                    return std::unexpected(recorded.error());
                }
            }
        }
        return result;
    }
    const auto grid = required(gpu, tickets.grid, pending.frameNumber, "grid", kGridBytes);
    if (!grid) {
        return std::unexpected(grid.error());
    }
    // Only the leading `assigned` entries are defined; the rest of the list is whatever the
    // previous occupant of this paced slot left behind, so it is never handed out. The index
    // outcome is checked even when that prefix is empty.
    const uint32_t assigned = std::min(result.counters.assigned, kLightClusterIndexCapacity);
    const auto indices = required(gpu, tickets.indices, pending.frameNumber, "indices",
                                  uint64_t{assigned} * sizeof(uint32_t));
    if (!indices) {
        return std::unexpected(indices.error());
    }
    result.grid.resize(kClusterCount);
    auto decoded = resolve(gpu, **grid, resolveBytes(std::span(result.grid)));
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    result.indices.resize(assigned);
    decoded = resolve(gpu, **indices, resolveBytes(std::span(result.indices)));
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    return result;
}

//======================================================================================================================
Result<void> LightClusterReadback::retireThrough(engine::GpuContext& gpu, uint64_t completedFrame) {
    for (auto entry = m_pending.begin(); entry != m_pending.end();) {
        if (entry->frameNumber > completedFrame) {
            ++entry;
            continue;
        }
        auto retired = readback(gpu, *entry);
        if (!retired) {
            return std::unexpected(retired.error());
        }
        m_retired.push_back(std::move(*retired));
        entry = m_pending.erase(entry);
    }
    return {};
}

//======================================================================================================================
void LightClusterReadback::discardUnrecorded(const ClusterReadbackTickets* tickets) {
    LMX_ASSERT(tickets != nullptr, "light-cluster discard requires a live exact declaration token");
    const auto found = std::ranges::find_if(
        m_pending, [tickets](const Pending& entry) { return entry.tickets.get() == tickets; });
    LMX_ASSERT(found != m_pending.end(), "light-cluster discard requires a pending declaration");
    LMX_ASSERT(!tickets->discarded && !tickets->entered && !tickets->counters && !tickets->grid &&
                   !tickets->indices,
               "light-cluster discard requires a wholly unrecorded declaration");
    found->tickets->discarded = true;
    m_pending.erase(found);
}

//======================================================================================================================
std::vector<RetiredLightClusters> LightClusterReadback::takeRetired() {
    return lmx::render::takeRetired(m_retired);
}

} // namespace lmx::render::light_detail
