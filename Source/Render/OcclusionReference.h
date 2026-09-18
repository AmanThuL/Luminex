//----------------------------------------------------------------------------------------------------------------------
/// @file OcclusionReference.h
/// @brief Owns independent all-candidate ID rendering and paced reference readback.
//----------------------------------------------------------------------------------------------------------------------
#pragma once
#include "Render/OcclusionCheck.h"
#include "Render/RenderGraph.h"
#include "Render/SceneView.h"
#include <array>
#include <memory>
#include <optional>

namespace lmx::render {
/// Independent direct-draw visibility oracle, owned by Renderer until all GPU use has retired.
class OcclusionReference {
public:
    /// Loads opaque and masked reference pipelines; reports GPU allocation/compiler errors.
    static rhi::Result<std::unique_ptr<OcclusionReference>> create(rhi::Device& device);
    /// Declares private depth and exact byte IDs for every candidate, with no visibility inputs.
    /// Call only inside an open paced frame. Scene bindings outlive graph execution. Extents
    /// are the active render rectangle; viewProjection is its current jittered raster matrix.
    /// strictView indicates unchanged unjittered view, coverage and active extent.
    rhi::Result<void> declare(RenderGraph& graph, rhi::CommandList& commands, const SceneView& view,
                              const glm::mat4& viewProjection, uint32_t width, uint32_t height,
                              bool strictView = false);
    /// Copies every completed slot to owned CPU observations before any slot may be recycled.
    /// Caller guarantees GPU completion through completedFrame using pacing or waitIdle.
    void retireThrough(uint64_t completedFrame);
    /// Joins one retired production status by exact frame and scene generation, consuming it.
    /// Invoke in frame order after retireThrough; absent reference frames return no value.
    std::optional<OcclusionCheckResult> check(const VisibilityStatus& status);

private:
    explicit OcclusionReference(rhi::Device& device) : m_device(device) {}
    struct Slot {
        std::unique_ptr<rhi::Buffer> buffer;
        bool used = false;
    };
    struct Pending {
        uint64_t frameNumber = 0, sceneGeneration = 0;
        uint32_t width = 0, height = 0;
        bool strict = false;
        rhi::Buffer* buffer = nullptr;
        std::vector<OcclusionCheckObservation> observations;
        uint64_t invalidPixels = 0;
    };
    rhi::Device& m_device;
    std::unique_ptr<rhi::ShaderLibrary> m_library;
    std::array<std::unique_ptr<rhi::GraphicsPipeline>, 8> m_pipelines;
    std::unique_ptr<rhi::Sampler> m_sampler;
    std::unique_ptr<rhi::Texture> m_white;
    std::array<Slot, 3> m_slots;
    std::vector<Pending> m_pending, m_retired;
    OcclusionCheckHistory m_history;
};
} // namespace lmx::render
