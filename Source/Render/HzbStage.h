//----------------------------------------------------------------------------------------------------------------------
/// @file HzbStage.h
/// @brief Declares exact reversed-depth pyramid allocation and cross-frame ownership.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/RenderGraph.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <memory>

namespace lmx::render {

/// Allocated pyramid dimensions; each level covers twice as many source texels per axis.
struct HzbLayout {
    uint32_t width = 0;      ///< Padded base width, independent of render scale.
    uint32_t height = 0;     ///< Padded base height, independent of render scale.
    uint32_t levelCount = 0; ///< First count placing both top active dimensions at most 16.
    uint64_t bytes = 0;      ///< Exact texel bytes for both pyramids, excluding driver padding.
};

/// Returns the ceil-sized active dimension at pyramid level; source extent is nonzero.
uint32_t hzbLevelExtent(uint32_t sourceExtent, uint32_t level);
/// Pads half the output extent so every floor-sized mip contains its ceil-sized active region.
HzbLayout hzbLayout(uint32_t outputWidth, uint32_t outputHeight);

/// Immutable facts of the declared frame that rasterized one pyramid's source depth.
struct HzbSource {
    uint64_t frameNumber = 0; ///< Device declaration number; consumption requires exact adjacency.
    glm::mat4 viewProjection{1.0f}; ///< Jittered world-to-clip matrix used by source rasterization.
    glm::vec3 cameraPosition{0.0f}; ///< Source world-space eye position, metres.
    glm::vec3 cameraForward{0.0f, 0.0f, -1.0f}; ///< Source normalized view direction.
    uint64_t coverageEpoch = 0;                 ///< Scene-wide depth coverage mutation epoch.
    uint64_t sceneGeneration = 0;               ///< Scene identity generation.
    uint32_t activeWidth = 0;                   ///< Rasterized source rectangle width.
    uint32_t activeHeight = 0;                  ///< Rasterized source rectangle height.
    uint32_t outputWidth = 0;                   ///< Source output allocation width.
    uint32_t outputHeight = 0;                  ///< Source output allocation height.
    uint32_t levelCount = 0;                    ///< Number of available pyramid levels.
    bool built = false;                         ///< A build was declared and rooted in the graph.
};

/// Owns two alternating pyramids and pipelines through all deferred graph callbacks.
/// Destroy or resize only after the device is idle. GPU queue ordering protects reuse.
class HzbStage {
public:
    /// Loads reduction/publish pipelines. Readback enables only diagnostic texture readback.
    static rhi::Result<std::unique_ptr<HzbStage>> create(rhi::Device& device,
                                                         bool cpuReadback = false);
    /// Allocates from output extent only; unchanged dimensions preserve identity and history.
    rhi::Result<void> resize(uint32_t outputWidth, uint32_t outputHeight);
    /// Returns the last declared source, or an unbuilt record before the first build.
    const HzbSource& previousSource() const { return m_sources[m_lastBuilt]; }
    /// Returns the last declared pyramid, or an allocated unbuilt texture initially.
    rhi::Texture& previousTexture() const { return *m_textures[m_lastBuilt]; }
    /// Imports the last declared pyramid with its recorded shader-read terminal state.
    GraphTexture importPrevious(RenderGraph& graph) const;
    /// Declares each reduction and a rooted publish read; alternates by build count.
    /// The caller must execute this graph before declaring another build. Source depth is sampled.
    GraphTexture build(RenderGraph& graph, rhi::CommandList& commands, GraphTexture depth,
                       HzbSource source);
    /// Returns allocated dimensions and combined pyramid texel bytes.
    const HzbLayout& layout() const { return m_layout; }

private:
    rhi::Device* m_device = nullptr;
    bool m_cpuReadback = false;
    uint32_t m_outputWidth = 0;
    uint32_t m_outputHeight = 0;
    uint32_t m_next = 0;
    uint32_t m_lastBuilt = 1;
    HzbLayout m_layout;
    std::array<std::unique_ptr<rhi::Texture>, 2> m_textures;
    std::array<HzbSource, 2> m_sources;
    std::unique_ptr<rhi::ShaderLibrary> m_reduceLibrary;
    std::unique_ptr<rhi::ComputePipeline> m_reducePipeline;
    std::unique_ptr<rhi::ShaderLibrary> m_publishLibrary;
    std::unique_ptr<rhi::ComputePipeline> m_publishPipeline;
    std::unique_ptr<rhi::Buffer> m_publishBuffer;
    bool m_published = false;
};

} // namespace lmx::render
