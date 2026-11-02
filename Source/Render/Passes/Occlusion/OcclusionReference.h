//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionReference.h
/// @brief Owns independent all-candidate ID rendering and paced reference readback.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Gpu/Readback.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Passes/Occlusion/OcclusionCheck.h"
#include "Render/Renderer/SceneView.h"
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace lmx::render {
class PreparedReferenceJoin;
class Renderer;
struct ReferenceBacking;
struct ReferencePendingReadback;
struct VisibilityRetirementAccess;
/// Raster facts for the independent visibility oracle; graph resources belong to this frame.
struct OcclusionReferenceInputs {
    glm::mat4 viewProjection; ///< Current jittered world-to-clip transform.
    uint32_t width;           ///< Active render rectangle width, nonzero.
    uint32_t height;          ///< Active render rectangle height, nonzero.
    std::shared_ptr<const OcclusionDeclarationAssociation>
        association;         ///< Exact shared production declaration identity.
    bool strictView = false; ///< Unchanged unjittered view, coverage and active extent.
};

/// Independent direct-draw visibility oracle, owned by Renderer until all GPU use has retired.
class OcclusionReference {
public:
    /// Loads opaque and masked reference pipelines; reports GPU allocation/compiler errors.
    static lmx::Result<std::unique_ptr<OcclusionReference>> create(engine::GpuContext& gpu);
    /// Declares private depth and exact byte IDs for every candidate, with no visibility inputs.
    /// Call only inside an open paced frame. Scene bindings outlive graph execution. Extents
    /// are the active render rectangle; viewProjection is its current jittered raster matrix.
    /// strictView indicates unchanged unjittered view, coverage and active extent.
    lmx::Result<void> declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                              const SceneView& view, const OcclusionReferenceInputs& inputs);
    /// Prepares all reference decodes and exact-identity joins on copied mutable state.
    Result<PreparedReferenceJoin> prepareJoin(uint64_t completedFrame,
                                              std::span<const VisibilityStatus> statuses) const;
    /// Publishes one prepared join from this owner and mutation epoch exactly once.
    void commitJoin(PreparedReferenceJoin&&) noexcept;
    /// Copies every completed slot to owned CPU observations before any slot may be recycled.
    /// Caller guarantees GPU completion through completedFrame using pacing or waitIdle.
    Result<void> retireThrough(uint64_t completedFrame);
    /// Joins one retired production status by exact declaration, frame and scene, consuming it.
    /// Invoke in frame order after retireThrough; absent reference frames return no value.
    Result<std::optional<OcclusionCheckResult>> check(const VisibilityStatus& status);
    /// Releases pending readback leases before dependent GPU owners.
    ~OcclusionReference();

private:
    friend struct VisibilityRetirementAccess;
    friend struct OcclusionTransactionTestAccess;
    friend class Renderer;
    Result<void> declareCombined(RenderGraph&, rojoRHI::CommandList&, const SceneView&,
                                 const OcclusionReferenceInputs&);
    Result<void> declareImpl(RenderGraph&, rojoRHI::CommandList&, const SceneView&,
                             const OcclusionReferenceInputs&, bool autoRetire);
    void validateJoin(const PreparedReferenceJoin&) const;
    explicit OcclusionReference(engine::GpuContext& gpu);
    engine::GpuContext& m_gpu;
    uint64_t m_epoch = 0;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_library;
    std::array<engine::GpuOwned<rojoRHI::Pipeline>, 8> m_pipelines;
    engine::GpuOwned<rojoRHI::SamplerIndex> m_sampler;
    engine::GpuTexture m_white;
    std::array<std::shared_ptr<ReferenceBacking>, 3> m_slots;
    std::vector<ReferencePendingReadback> m_pending, m_retired;
    OcclusionCheckHistory m_history;
};
} // namespace lmx::render
