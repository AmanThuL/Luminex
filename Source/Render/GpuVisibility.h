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
    static rojoRHI::Result<std::unique_ptr<GpuVisibility>> create(rojoRHI::Device& device);
    /// Declares the four visibility passes and retains the declaration's readback context.
    GpuVisibilityOutputs declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                 const SceneView& view, const FrustumPlanes& planes,
                                 const PreparedSubmission& submission, GraphBuffer instances,
                                 GraphBuffer meshes, GraphBuffer rows, GraphBuffer arguments,
                                 VisibilityStatus& status, GraphTexture pyramid = {},
                                 OcclusionParams occlusion = {});
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
    explicit GpuVisibility(rojoRHI::Device& device) : m_device(device) {}
    struct Slot {
        std::unique_ptr<rojoRHI::Buffer> candidates, runs, chunks, views, states, counters;
        std::vector<std::byte> candidateBytes, runBytes, chunkBytes, viewBytes;
        uint32_t capacity = 0;
        bool used = false;
    };
    struct Pending {
        VisibilityStatus status;
        VisibilityTables tables;
        VisibilityParams params;
        std::vector<InstanceVisibility> expected;
        std::vector<rojoRHI::DrawIndexedIndirectArgs> geometry;
        rojoRHI::Buffer *states = nullptr, *counters = nullptr, *rows = nullptr, *arguments = nullptr;
    };
    rojoRHI::Result<void> prepareSlot(Slot& slot, const VisibilityTables& tables);
    VisibilityStatus readback(Pending& pending);
    rojoRHI::Device& m_device;
    std::array<std::unique_ptr<rojoRHI::ShaderLibrary>, 5> m_libraries;
    std::array<std::unique_ptr<rojoRHI::ComputePipeline>, 5> m_pipelines;
    std::unique_ptr<rojoRHI::Buffer> m_emptyInstances, m_emptyMeshes;
    std::array<Slot, 3> m_slots;
    std::vector<Pending> m_pending;
    std::vector<VisibilityStatus> m_retired;
    std::optional<std::array<uint32_t, 3>> m_capacityOverride;
};
} // namespace lmx::render
