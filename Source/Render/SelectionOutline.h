//----------------------------------------------------------------------------------------------------------------------
/// @file SelectionOutline.h
/// @brief Declares optional visible-geometry selection presentation after scene rendering.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/RenderGraph.h"
#include "Render/SceneView.h"

#include <memory>

namespace lmx::render {
class Camera;

/// Composites a selection border into a separate SDR target without modifying scene/history data.
/// The editor opts into this utility; ordinary Renderer and offscreen paths never invoke it.
class SelectionOutline {
public:
    /// Creates pipelines and a sampled BGRA8 target; readback is intended for GPU oracles.
    static rhi::Result<std::unique_ptr<SelectionOutline>>
    create(rhi::Device& device, uint32_t width, uint32_t height, bool readback = false);
    /// Replaces the output only after success; caller must idle the device and forget UI bindings.
    rhi::Result<void> resize(uint32_t width, uint32_t height);
    /// Output texture for the UI, written by declare before its sampling pass executes.
    rhi::Texture& target() { return *m_target; }
    /// Declares selected coverage, scene visibility and a depth-tested border for an in-range draw.
    /// Foreground occlusion cuts never become silhouette edges. Scene inputs are
    /// borrowed through execution. Thickness is 1.5 logical points at the supplied backing scale.
    /// When visible is false, refreshes the UI target with unmodified display texels.
    GraphTexture declare(RenderGraph& graph, rhi::CommandList& commands, GraphTexture display,
                         const Camera& camera, const SceneView& view, uint32_t selectedDraw,
                         float backingScale, bool visible = true);

private:
    explicit SelectionOutline(rhi::Device& device, bool readback);
    rhi::Device& m_device;
    bool m_readback;
    std::unique_ptr<rhi::ShaderLibrary> m_maskLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_outlineLibrary;
    std::unique_ptr<rhi::GraphicsPipeline> m_maskPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_doubleSidedPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_depthPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_doubleSidedDepthPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_outlinePipeline;
    std::unique_ptr<rhi::GraphicsPipeline> m_passthroughPipeline;
    std::unique_ptr<rhi::Sampler> m_sampler;
    std::unique_ptr<rhi::Texture> m_white;
    std::unique_ptr<rhi::Texture> m_target;
};

} // namespace lmx::render
