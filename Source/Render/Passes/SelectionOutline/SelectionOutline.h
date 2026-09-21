//----------------------------------------------------------------------------------------------------------------------
/// @file SelectionOutline.h
/// @brief Declares optional visible-geometry selection presentation after scene rendering.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Render/Graph/RenderGraph.h"
#include "Render/Renderer/SceneView.h"

#include <memory>

namespace lmx::engine {
class Camera;
} // namespace lmx::engine

namespace lmx::render {

/// Frame values and graph versions for editor selection presentation.
struct SelectionOutlineInputs {
    GraphTexture display;         ///< Completed SDR display version to sample.
    const engine::Camera& camera; ///< Camera needed during declaration only.
    uint32_t selectedDraw;        ///< In-range index into this frame's scene draw list.
    float backingScale;           ///< Physical pixels per logical point.
    bool visible = true;          ///< False copies display without drawing a selection border.
};

/// Composites a selection border into a separate SDR target without modifying scene/history data.
/// The editor opts into this utility; ordinary Renderer and offscreen paths never invoke it.
class SelectionOutline {
public:
    /// Creates pipelines and a sampled BGRA8 target; readback is intended for GPU oracles.
    static rojoRHI::Result<std::unique_ptr<SelectionOutline>>
    create(rojoRHI::Device& device, uint32_t width, uint32_t height, bool readback = false);
    /// Replaces the output even at unchanged extents, only after successful allocation.
    /// Caller must idle the device and forget UI bindings.
    rojoRHI::Result<void> resize(uint32_t width, uint32_t height);
    /// Output texture for the UI, written by declare before its sampling pass executes.
    rojoRHI::Texture& target() { return *m_target; }
    /// Declares selected coverage, scene visibility and a depth-tested border for an in-range draw.
    /// Foreground occlusion cuts never become silhouette edges. Scene inputs are
    /// borrowed through execution. Thickness is 1.5 logical points at the supplied backing scale.
    /// When visible is false, refreshes the UI target with unmodified display texels.
    GraphTexture declare(RenderGraph& graph, rojoRHI::CommandList& commands, const SceneView& view,
                         const SelectionOutlineInputs& inputs);

private:
    explicit SelectionOutline(rojoRHI::Device& device, bool readback);
    rojoRHI::Device& m_device;
    bool m_readback;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_maskLibrary;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_outlineLibrary;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_maskPipeline;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_doubleSidedPipeline;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_depthPipeline;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_doubleSidedDepthPipeline;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_outlinePipeline;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_passthroughPipeline;
    std::unique_ptr<rojoRHI::Sampler> m_sampler;
    std::unique_ptr<rojoRHI::Texture> m_white;
    std::unique_ptr<rojoRHI::Texture> m_target;
};

} // namespace lmx::render
