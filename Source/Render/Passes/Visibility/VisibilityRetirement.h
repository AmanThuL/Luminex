//----------------------------------------------------------------------------------------------------------------------
/// @file VisibilityRetirement.h
/// @brief Owns the actual visibility backing and transactional readback state.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/Gpu/Readback.h"
#include "Render/Passes/Visibility/VisibilityTables.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace lmx::render {

struct OcclusionDeclarationAssociation {
    const engine::GpuContext* context;
    uint64_t frame;
    uint64_t sceneGeneration;
    OcclusionDeclarationAssociation(engine::GpuContext& owner, uint64_t frameNumber,
                                    uint64_t generation)
        : context(&owner), frame(frameNumber), sceneGeneration(generation) {}
};

struct VisibilityDeclarationProjection {
    VisibilityStatus status;
};

struct ReferenceBacking {
    engine::GpuBuffer buffer;
    uint64_t capacity = 0;
    SyncPoint physical{rojoRHI::Stage::None, rojoRHI::Access::None, rojoRHI::Layout::Undefined};
};

struct DeclarationRecordingState {
    bool entered = false;
    bool discarded = false;
};

struct ReferencePendingReadback {
    uint64_t frameNumber = 0, sceneGeneration = 0;
    uint32_t width = 0, height = 0, rowPitch = 0;
    bool strict = false;
    std::shared_ptr<DeclarationRecordingState> recording;
    std::weak_ptr<const uint8_t> callbackAnchor;
    std::shared_ptr<const OcclusionDeclarationAssociation> association;
    std::shared_ptr<std::optional<ReadbackTicket>> ticket =
        std::make_shared<std::optional<ReadbackTicket>>();
    std::shared_ptr<ReferenceBacking> backing;
    std::vector<OcclusionCheckObservation> observations;
    uint64_t invalidPixels = 0;
};

class GpuVisibility;
class OcclusionReference;
struct VisibilityRetirementAccess {
    static Result<std::vector<VisibilityStatus>> finish(GpuVisibility&, OcclusionReference*,
                                                        uint64_t, bool consume);
    static Result<void> retire(GpuVisibility&, OcclusionReference*, uint64_t);
    static Result<std::vector<VisibilityStatus>>
    take(GpuVisibility& production, OcclusionReference* reference, uint64_t completedFrame);
};

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
    bool entered = false;
};

struct VisibilityPendingReadback {
    VisibilityStatus status;
    std::shared_ptr<VisibilityDeclarationProjection> projection;
    VisibilityTables tables;
    VisibilityParams params{};
    std::vector<InstanceVisibility> expected;
    std::vector<rojoRHI::DrawIndexedIndirectArgs> geometry;
    std::shared_ptr<VisibilityReadbackTickets> tickets;
    std::weak_ptr<const uint8_t> callbackAnchor;
    std::shared_ptr<VisibilitySlotBacking> backing;
    rojoRHI::Buffer rowBuffer{};
    rojoRHI::Buffer argumentBuffer{};
    uint64_t frameNumber = 0;
};

struct VisibilityProjectionUpdate {
    std::shared_ptr<VisibilityDeclarationProjection> target;
    size_t readyIndex = 0;
    VisibilityStatus value;
};

// Staged entries are named by index into the live pending vector and moved out only at commit, so
// preparing a retirement never copies an entry that is still in flight.
struct VisibilityRetirementBatch {
    std::vector<size_t> retiring; // Ascending pending indices that retire or are discarded.
    // Newly retired statuses in frame order; capacity also holds the live retired records, so the
    // commit prepends them without allocating.
    std::vector<VisibilityStatus> ready;
    std::vector<std::shared_ptr<VisibilityReadbackTickets>> discards;
    std::vector<VisibilityProjectionUpdate> projections;
};

struct ReferenceRetirementBatch {
    std::vector<size_t> retiring; // Ascending pending indices that retire or are discarded.
    std::vector<ReferencePendingReadback> ready; // Live retired copy plus decoded entries, joined.
    std::vector<std::shared_ptr<DeclarationRecordingState>> discards;
    OcclusionCheckHistory history;
    std::vector<VisibilityStatus> results;
};

static_assert(std::is_nothrow_move_assignable_v<VisibilityPendingReadback> &&
              std::is_nothrow_move_assignable_v<ReferencePendingReadback> &&
              std::is_nothrow_move_constructible_v<VisibilityStatus>);

class PreparedVisibilityRetirement {
public:
    PreparedVisibilityRetirement(PreparedVisibilityRetirement&& other) noexcept
        : m_owner(std::exchange(other.m_owner, nullptr)), m_context(other.m_context),
          m_epoch(other.m_epoch), m_batch(std::move(other.m_batch)) {}
    PreparedVisibilityRetirement& operator=(PreparedVisibilityRetirement&& other) noexcept {
        if (this != &other) {
            m_owner = std::exchange(other.m_owner, nullptr);
            m_context = other.m_context;
            m_epoch = other.m_epoch;
            m_batch = std::move(other.m_batch);
        }
        return *this;
    }
    PreparedVisibilityRetirement(const PreparedVisibilityRetirement&) = delete;
    PreparedVisibilityRetirement& operator=(const PreparedVisibilityRetirement&) = delete;
    std::span<const VisibilityStatus> statuses() const { return m_batch.ready; }

private:
    friend class GpuVisibility;
    friend struct VisibilityRetirementAccess;
    PreparedVisibilityRetirement() = default;
    const GpuVisibility* m_owner = nullptr;
    const engine::GpuContext* m_context = nullptr;
    uint64_t m_epoch = 0;
    VisibilityRetirementBatch m_batch;
};

class PreparedReferenceJoin {
public:
    PreparedReferenceJoin(PreparedReferenceJoin&& other) noexcept
        : m_owner(std::exchange(other.m_owner, nullptr)), m_context(other.m_context),
          m_epoch(other.m_epoch), m_batch(std::move(other.m_batch)) {}
    PreparedReferenceJoin& operator=(PreparedReferenceJoin&& other) noexcept {
        if (this != &other) {
            m_owner = std::exchange(other.m_owner, nullptr);
            m_context = other.m_context;
            m_epoch = other.m_epoch;
            m_batch = std::move(other.m_batch);
        }
        return *this;
    }
    PreparedReferenceJoin(const PreparedReferenceJoin&) = delete;
    PreparedReferenceJoin& operator=(const PreparedReferenceJoin&) = delete;

private:
    friend class OcclusionReference;
    friend struct VisibilityRetirementAccess;
    PreparedReferenceJoin() = default;
    const OcclusionReference* m_owner = nullptr;
    const engine::GpuContext* m_context = nullptr;
    uint64_t m_epoch = 0;
    ReferenceRetirementBatch m_batch;
};

// Removes the staged indices in one pass; moves are nothrow and the tail is only destroyed.
template <typename T>
void eraseRetiring(std::vector<T>& pending, std::span<const size_t> retiring) noexcept {
    size_t next = 0;
    size_t write = 0;
    for (size_t read = 0; read < pending.size(); ++read) {
        if (next < retiring.size() && retiring[next] == read) {
            ++next;
            continue;
        }
        if (write != read)
            pending[write] = std::move(pending[read]);
        ++write;
    }
    pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(write), pending.end());
}

// Moves joined statuses into a prepared batch whose capacity the commit relies on.
inline void adoptJoinedStatuses(std::vector<VisibilityStatus>& ready,
                                std::vector<VisibilityStatus>& joined) noexcept {
    std::ranges::move(joined, ready.begin());
}

// All Result-bearing work reads live state and writes owned value staging only.
// Host allocation/destructor failures and fatal misuse are outside this guarantee.
Result<VisibilityRetirementBatch>
prepareVisibilityRetirement(engine::GpuContext&, const std::vector<VisibilityPendingReadback>&,
                            const std::vector<VisibilityStatus>&, uint64_t);
void commitVisibilityRetirement(VisibilityRetirementBatch&, std::vector<VisibilityPendingReadback>&,
                                std::vector<VisibilityStatus>&) noexcept;
Result<ReferenceRetirementBatch>
prepareReferenceRetirement(engine::GpuContext&, const std::vector<ReferencePendingReadback>&,
                           const std::vector<ReferencePendingReadback>&,
                           const OcclusionCheckHistory&, uint64_t,
                           std::span<const VisibilityStatus>);
void commitReferenceRetirement(ReferenceRetirementBatch&, std::vector<ReferencePendingReadback>&,
                               std::vector<ReferencePendingReadback>&,
                               OcclusionCheckHistory&) noexcept;
void prepareVisibilityProjections(VisibilityRetirementBatch&);
Result<void> retireVisibilityThrough(engine::GpuContext&, std::vector<VisibilityPendingReadback>&,
                                     std::vector<VisibilityStatus>&, uint64_t);
Result<void> retireReferenceThrough(engine::GpuContext&, std::vector<ReferencePendingReadback>&,
                                    std::vector<ReferencePendingReadback>&, uint64_t);
Result<std::optional<OcclusionCheckResult>> checkReference(std::vector<ReferencePendingReadback>&,
                                                           OcclusionCheckHistory&,
                                                           const VisibilityStatus&);
Result<std::vector<VisibilityStatus>> takeRetiredVisibilityWithReference(
    engine::GpuContext&, std::vector<VisibilityPendingReadback>&, std::vector<VisibilityStatus>&,
    std::vector<ReferencePendingReadback>*, std::vector<ReferencePendingReadback>*,
    OcclusionCheckHistory*, uint64_t);

} // namespace lmx::render
