//----------------------------------------------------------------------------------------------------------------------
/// @file LightClusterReadback.h
/// @brief Owns light-cluster readback outcomes and retires them one declaration at a time.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuContext.h"
#include "Render/Gpu/Readback.h"
#include "Render/Passes/LocalLights/LightClusterStage.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace lmx::render::light_detail {

/// Byte sizes of one paced slot's four buffers.
inline constexpr uint64_t kGridBytes = uint64_t{kClusterCount} * sizeof(ClusterRecord);
inline constexpr uint64_t kIndexBytes = uint64_t{kLightClusterIndexCapacity} * sizeof(uint32_t);
inline constexpr uint64_t kCountBytes = uint64_t{kClusterCount} * sizeof(uint32_t);
inline constexpr uint64_t kCounterBytes = kLightClusterCounterWords * sizeof(uint32_t);

/// The outcomes one declaration's passes record, after the dispatch that writes each buffer. An
/// absent outcome means no operation was recorded, never a successful zero.
struct ClusterReadbackTickets {
    std::optional<ReadbackTicket> counters; ///< Required for every declaration.
    std::optional<ReadbackTicket> grid;     ///< Required only when the lists are captured.
    std::optional<ReadbackTicket> indices;  ///< Required only when the lists are captured.
    bool discarded = false; ///< Set by discardUnrecorded; a later callback is misuse.
};

/// Pending declarations and their retired records, in declaration order.
class LightClusterReadback {
public:
    /// Adds one declaration; its graph callbacks write the returned outcome set.
    std::shared_ptr<ClusterReadbackTickets> enqueue(uint64_t frameNumber, bool captureLists);

    /// Whether a pending declaration still owns the paced slot `frameNumber` selects.
    bool hasPendingSlot(uint64_t frameNumber) const;

    /// Decodes each declaration whose frame is at most `completedFrame` and moves it to the
    /// retired records. The first failure returns its error and leaves that declaration and every
    /// later one pending; a recorded failure is returned unchanged.
    Result<void> retireThrough(engine::GpuContext& gpu, uint64_t completedFrame);

    /// Cancels the declaration of `frameNumber` whose graph never executed, releasing its paced
    /// slot. It must exist and hold no recorded outcome: a partly recorded declaration is not
    /// discardable and keeps failing retirement. Its callbacks must never run afterwards.
    void discardUnrecorded(uint64_t frameNumber);

    /// Moves all retired records to the caller; a second call returns only newer records.
    std::vector<RetiredLightClusters> takeRetired();

private:
    struct Pending {
        uint64_t frameNumber = 0;
        bool captureLists = false;
        std::shared_ptr<ClusterReadbackTickets> tickets;
    };

    static Result<RetiredLightClusters> readback(engine::GpuContext& gpu, const Pending& pending);

    std::vector<Pending> m_pending;
    std::vector<RetiredLightClusters> m_retired;
};

} // namespace lmx::render::light_detail
