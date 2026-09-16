//----------------------------------------------------------------------------------------------------------------------
/// @file GpuVisibility.h
/// @brief Owns fixed-slot GPU visibility passes and frame-keyed retired diagnostics.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "Render/RenderGraph.h"
#include "Render/VisibilityTables.h"

namespace lmx::render {
/// Versioned GPU-produced draw inputs.
struct GpuVisibilityOutputs {
    GraphBuffer rows;      ///< Emitted row-list version.
    GraphBuffer arguments; ///< Emitted argument-buffer version.
};
/// Owns three paced table slots, pipelines and retained oracle snapshots.
class GpuVisibility {
public:
    /// Loads safe-math visibility kernels and their fixed threadgroup pipelines.
    static rhi::Result<std::unique_ptr<GpuVisibility>> create(rhi::Device& device);
    /// Declares the four visibility passes and retains the declaration's readback context.
    GpuVisibilityOutputs declare(RenderGraph& graph, rhi::CommandList& commands,
                                 const SceneView& view, const FrustumPlanes& planes,
                                 const PreparedSubmission& submission, GraphBuffer instances,
                                 GraphBuffer meshes, GraphBuffer rows, GraphBuffer arguments,
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
    explicit GpuVisibility(rhi::Device& device) : m_device(device) {}
    struct Slot {
        std::unique_ptr<rhi::Buffer> candidates, runs, chunks, views, states, counters;
        std::vector<std::byte> candidateBytes, runBytes, chunkBytes, viewBytes;
        uint32_t capacity = 0;
        bool used = false;
    };
    struct Pending {
        VisibilityStatus status;
        VisibilityTables tables;
        VisibilityParams params;
        std::vector<InstanceVisibility> expected;
        std::vector<rhi::DrawIndexedIndirectArgs> geometry;
        rhi::Buffer *states = nullptr, *counters = nullptr, *rows = nullptr, *arguments = nullptr;
    };
    rhi::Result<void> prepareSlot(Slot& slot, const VisibilityTables& tables);
    VisibilityStatus readback(Pending& pending);
    rhi::Device& m_device;
    std::array<std::unique_ptr<rhi::ShaderLibrary>, 4> m_libraries;
    std::array<std::unique_ptr<rhi::ComputePipeline>, 4> m_pipelines;
    std::unique_ptr<rhi::Buffer> m_emptyInstances, m_emptyMeshes;
    std::array<Slot, 3> m_slots;
    std::vector<Pending> m_pending;
    std::vector<VisibilityStatus> m_retired;
    std::optional<std::array<uint32_t, 3>> m_capacityOverride;
};
} // namespace lmx::render
