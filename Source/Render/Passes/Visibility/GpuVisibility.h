//----------------------------------------------------------------------------------------------------------------------
/// @file GpuVisibility.h
/// @brief Owns fixed-slot GPU visibility passes and frame-keyed retired diagnostics.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Render/Gpu/Readback.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Passes/Visibility/VisibilityTables.h"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace lmx::render {
class PreparedVisibilityRetirement;
struct VisibilitySlotBacking;
struct VisibilityRetirementAccess;
struct VisibilityDeclarationProjection;
class HzbHistoryBinding;
struct VisibilityReadbackTickets;
struct VisibilityPendingReadback;
/// Borrowed visibility inputs; CPU references need only survive declaration, graph handles this
/// frame.
struct GpuVisibilityInputs {
    const FrustumPlanes& planes;          ///< Current guarded camera frustum.
    const PreparedSubmission& submission; ///< Paced scene and shadow draw lists to classify.
    GraphBuffer instances; ///< Read-only instance rows, or absent for empty geometry.
    GraphBuffer meshes;    ///< Read-only mesh rows, or absent for empty geometry.
    GraphBuffer rows;      ///< Visible-row buffer version to write.
    GraphBuffer arguments; ///< Indirect-argument buffer version to write.
    std::shared_ptr<HzbHistoryBinding> history{}; ///< Retained canonical old-history observer.
    OcclusionParams occlusion{}; ///< Coverage and projection state for the optional pyramid.
    std::shared_ptr<std::optional<ReadbackTicket>>
        stateGuardReadback{};                    ///< Optional whole-state guard capture.
    std::optional<SyncPoint> rowPhysical{};      ///< Supplied physical contributors at emit entry.
    std::optional<SyncPoint> argumentPhysical{}; ///< Absent leaves external provenance uncertified.
};

/// Versioned GPU-produced draw inputs.
struct GpuVisibilityOutputs {
    GraphBuffer rows;      ///< Emitted row-list version.
    GraphBuffer arguments; ///< Emitted argument-buffer version.
    std::weak_ptr<const VisibilityReadbackTickets>
        readbackIntent{}; ///< Exact unrecorded declaration identity.
    std::shared_ptr<VisibilityDeclarationProjection> projection; ///< Owned declared status.
};
/// Owns three paced table slots, pipelines and retained oracle snapshots.
class GpuVisibility {
public:
    /// Loads safe-math visibility kernels and their fixed threadgroup pipelines.
    static lmx::Result<std::unique_ptr<GpuVisibility>> create(engine::GpuContext& gpu);
    /// Declares the four visibility passes and retains the declaration's readback context.
    Result<GpuVisibilityOutputs> declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                         const SceneView& view, const GpuVisibilityInputs& inputs,
                                         VisibilityStatus& status);
    /// Copies final declaration metrics after graph preparation completes.
    void recordDeclarationStatus(const GpuVisibilityOutputs& declaration,
                                 const VisibilityStatus& status);
    /// Prepares the entire retired take without changing live declaration state.
    Result<PreparedVisibilityRetirement> prepareRetiredTake(uint64_t completedFrame) const;
    /// Consumes a valid prepared take from this owner and mutation epoch exactly once.
    std::vector<VisibilityStatus> commitRetiredTake(PreparedVisibilityRetirement&&) noexcept;
    /// Reads every context whose submitted frame is at most completedFrame.
    Result<void> retireThrough(uint64_t completedFrame);
    /// Explicitly cancels this exact unentered, outcome-free intent, even with borrowed callbacks.
    /// A later callback is misuse; automatic retirement instead waits for callback expiry.
    void discardUnrecorded(const GpuVisibilityOutputs& declaration);
    /// Releases callbacks and pending storage before dependent pipelines and libraries.
    ~GpuVisibility();
    /// Moves all newly retired records to the caller, in frame order.
    std::vector<VisibilityStatus> takeRetired();
    /// Fixture-only physical write limits; zero values are valid truncation probes.
    void setCapacityOverride(std::optional<std::array<uint32_t, 3>> capacities) {
        m_capacityOverride = capacities;
    }

private:
    friend struct VisibilityRetirementAccess;
    friend struct HzbMatrixTestAccess;
    friend struct OcclusionTransactionTestAccess;
    using RecordingObserver = void (*)(void*, uint64_t, std::span<const std::byte>, rojoRHI::Addr,
                                       std::span<const std::byte>, rojoRHI::Addr,
                                       rojoRHI::ViewIndex, bool,
                                       std::span<const rojoRHI::ResourceAccess>);
    RecordingObserver m_recordingObserver = nullptr;
    void* m_recordingObserverContext = nullptr;
    explicit GpuVisibility(engine::GpuContext& gpu);
    Result<void> prepareSlot(VisibilitySlotBacking& slot, const VisibilityTables& tables);
    void validateRetiredTake(const PreparedVisibilityRetirement&) const;
    engine::GpuContext& m_gpu;
    uint64_t m_epoch = 0;
    std::array<engine::GpuOwned<rojoRHI::ShaderLibrary>, 5> m_libraries;
    std::array<engine::GpuOwned<rojoRHI::Pipeline>, 5> m_pipelines;
    engine::GpuBuffer m_emptyInstances, m_emptyMeshes;
    std::array<std::shared_ptr<VisibilitySlotBacking>, 3> m_slots;
    std::vector<VisibilityPendingReadback> m_pending;
    std::vector<VisibilityStatus> m_retired;
    std::optional<std::array<uint32_t, 3>> m_capacityOverride;
};
} // namespace lmx::render
