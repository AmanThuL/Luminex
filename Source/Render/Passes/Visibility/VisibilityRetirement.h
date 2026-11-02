//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityRetirement.h
/// @brief Owns the actual visibility backing and transactional readback state.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/Gpu/Readback.h"
#include "Render/Passes/Visibility/VisibilityTables.h"

#include <memory>
#include <optional>
#include <vector>

namespace lmx::render {

struct VisibilitySlotBacking {
    engine::GpuBuffer candidates, runs, chunks, views, states, counters;
    std::vector<std::byte> candidateBytes, runBytes, chunkBytes, viewBytes;
    uint32_t capacity = 0;
    bool used = false;
    SyncPoint statesPhysical{rojoRHI::Stage::None, rojoRHI::Access::None,
                             rojoRHI::Layout::Undefined};
    SyncPoint countersPhysical{rojoRHI::Stage::None, rojoRHI::Access::None,
                               rojoRHI::Layout::Undefined};
};

struct VisibilityReadbackTickets {
    std::optional<ReadbackTicket> states, counters, rows, arguments;
    bool discarded = false;
};

struct VisibilityPendingReadback {
    VisibilityStatus status;
    VisibilityTables tables;
    VisibilityParams params{};
    std::vector<InstanceVisibility> expected;
    std::vector<rojoRHI::DrawIndexedIndirectArgs> geometry;
    std::shared_ptr<VisibilityReadbackTickets> tickets;
    std::shared_ptr<VisibilitySlotBacking> backing;
    rojoRHI::Buffer rowBuffer{};
    rojoRHI::Buffer argumentBuffer{};
    uint64_t frameNumber = 0;
};

// Expected Result failures preserve the entire eligible batch and all prior retired output.
// Host allocation/destructor failures and fatal misuse are outside that guarantee.
Result<void> retireVisibilityThrough(engine::GpuContext& gpu,
                                     std::vector<VisibilityPendingReadback>& pending,
                                     std::vector<VisibilityStatus>& retired,
                                     uint64_t completedFrame);

} // namespace lmx::render
