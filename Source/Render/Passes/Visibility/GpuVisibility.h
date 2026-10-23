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

#include <memory>
#include <optional>

namespace lmx::render {
/// Borrowed visibility inputs; CPU references need only survive declaration, graph handles this
/// frame.
struct GpuVisibilityInputs {
    const FrustumPlanes& planes;          ///< Current guarded camera frustum.
    const PreparedSubmission& submission; ///< Paced scene and shadow draw lists to classify.
    GraphBuffer instances;       ///< Read-only instance rows, or absent for empty geometry.
    GraphBuffer meshes;          ///< Read-only mesh rows, or absent for empty geometry.
    GraphBuffer rows;            ///< Visible-row buffer version to write.
    GraphBuffer arguments;       ///< Indirect-argument buffer version to write.
    GraphTexture pyramid{};      ///< Optional previous-frame depth pyramid.
    OcclusionParams occlusion{}; ///< Coverage and projection state for the optional pyramid.
};

/// Versioned GPU-produced draw inputs.
struct GpuVisibilityOutputs {
    GraphBuffer rows;      ///< Emitted row-list version.
    GraphBuffer arguments; ///< Emitted argument-buffer version.
};
/// Owns three paced table slots, pipelines and retained oracle snapshots.
class GpuVisibility {
public:
    /// Loads safe-math visibility kernels and their fixed threadgroup pipelines.
    static lmx::Result<std::unique_ptr<GpuVisibility>> create(engine::GpuContext& gpu);
    /// Declares the four visibility passes and retains the declaration's readback context.
    GpuVisibilityOutputs declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                 const SceneView& view, const GpuVisibilityInputs& inputs,
                                 VisibilityStatus& status);
    /// Copies final declaration metrics after graph preparation completes.
    void recordDeclarationStatus(const VisibilityStatus& status) {
        m_pending.back().status = status;
    }
    /// Reads every context whose submitted frame is at most completedFrame.
    void retireThrough(uint64_t completedFrame);
    /// Moves all newly retired records to the caller, in frame order.
    std::vector<VisibilityStatus> takeRetired();
    /// Fixture-only physical write limits; zero values are valid truncation probes.
    void setCapacityOverride(std::optional<std::array<uint32_t, 3>> capacities) {
        m_capacityOverride = capacities;
    }

private:
    explicit GpuVisibility(engine::GpuContext& gpu) : m_gpu(gpu) {}
    struct Slot {
        engine::GpuBuffer candidates, runs, chunks, views, states, counters;
        std::vector<std::byte> candidateBytes, runBytes, chunkBytes, viewBytes;
        uint32_t capacity = 0;
        bool used = false;
    };
    // Recorded by the passes that write each buffer, after the write. A ticket names the buffer
    // the frame wrote, not its owner: DrawSubmission growth move-assigns new owners into the slots
    // the row and argument buffers came from while this frame is still pending. Pending entries
    // and pass bodies share the tickets; their storage remains alive until its final owner releases
    // it. A ticket stays empty when its graph pass did not execute.
    struct Tickets {
        std::optional<ReadbackTicket> states, counters, rows, arguments;
    };
    struct Pending {
        VisibilityStatus status;
        VisibilityTables tables;
        VisibilityParams params;
        std::vector<InstanceVisibility> expected;
        std::vector<rojoRHI::DrawIndexedIndirectArgs> geometry;
        std::shared_ptr<Tickets> tickets = std::make_shared<Tickets>();
    };
    lmx::Result<void> prepareSlot(Slot& slot, const VisibilityTables& tables);
    VisibilityStatus readback(Pending& pending);
    engine::GpuContext& m_gpu;
    std::array<engine::GpuOwned<rojoRHI::ShaderLibrary>, 5> m_libraries;
    std::array<engine::GpuOwned<rojoRHI::ComputePipeline>, 5> m_pipelines;
    engine::GpuBuffer m_emptyInstances, m_emptyMeshes;
    std::array<Slot, 3> m_slots;
    std::vector<Pending> m_pending;
    std::vector<VisibilityStatus> m_retired;
    std::optional<std::array<uint32_t, 3>> m_capacityOverride;
};
} // namespace lmx::render
