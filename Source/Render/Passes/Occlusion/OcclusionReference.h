//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionReference.h
/// @brief Owns independent all-candidate ID rendering and paced reference readback.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuTexture.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Passes/Occlusion/OcclusionCheck.h"
#include "Render/Renderer/SceneView.h"
#include <array>
#include <memory>
#include <optional>

namespace lmx::render {
/// Raster facts for the independent visibility oracle; graph resources belong to this frame.
struct OcclusionReferenceInputs {
    glm::mat4 viewProjection; ///< Current jittered world-to-clip transform.
    uint32_t width;           ///< Active render rectangle width, nonzero.
    uint32_t height;          ///< Active render rectangle height, nonzero.
    bool strictView = false;  ///< Unchanged unjittered view, coverage and active extent.
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
    /// Copies every completed slot to owned CPU observations before any slot may be recycled.
    /// Caller guarantees GPU completion through completedFrame using pacing or waitIdle.
    void retireThrough(uint64_t completedFrame);
    /// Joins one retired production status by exact frame and scene generation, consuming it.
    /// Invoke in frame order after retireThrough; absent reference frames return no value.
    std::optional<OcclusionCheckResult> check(const VisibilityStatus& status);

private:
    explicit OcclusionReference(engine::GpuContext& gpu) : m_gpu(gpu), m_device(gpu.device()) {}
    struct Slot {
        engine::GpuBuffer buffer;
        bool used = false;
    };
    struct Pending {
        uint64_t frameNumber = 0, sceneGeneration = 0;
        uint32_t width = 0, height = 0;
        bool strict = false;
        const engine::GpuBuffer* buffer = nullptr;
        std::vector<OcclusionCheckObservation> observations;
        uint64_t invalidPixels = 0;
    };
    engine::GpuContext& m_gpu;
    rojoRHI::Device& m_device;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_library;
    std::array<std::unique_ptr<rojoRHI::GraphicsPipeline>, 8> m_pipelines;
    std::unique_ptr<rojoRHI::Sampler> m_sampler;
    engine::GpuTexture m_white;
    std::array<Slot, 3> m_slots;
    std::vector<Pending> m_pending, m_retired;
    OcclusionCheckHistory m_history;
};
} // namespace lmx::render
