//----------------------------------------------------------------------------------------------------------------------
/// @file SelectionOutline.h
/// @brief Declares optional visible-geometry selection presentation after scene rendering.
//----------------------------------------------------------------------------------------------------------------------
#pragma once

#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuTexture.h"
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
    static lmx::Result<std::unique_ptr<SelectionOutline>>
    create(engine::GpuContext& gpu, uint32_t width, uint32_t height, bool readback = false);
    /// Replaces the output even at unchanged extents, only after successful allocation; the
    /// replaced target loses its `states` entry. Caller must idle the device and forget UI
    /// bindings.
    lmx::Result<void> resize(uint32_t width, uint32_t height, TextureStateTable& states);
    /// Output texture for the UI, written by declare before its sampling pass executes.
    const engine::GpuTexture& target() const { return m_target; }
    /// Declares selected coverage, scene visibility and a depth-tested border for an in-range draw.
    /// Foreground occlusion cuts never become silhouette edges. Scene inputs are
    /// borrowed through execution. Thickness is 1.5 logical points at the supplied backing scale.
    /// When visible is false, refreshes the UI target with unmodified display texels.
    GraphTexture declare(RenderGraph& graph, rojoRHI::CommandList& commands, const SceneView& view,
                         const SelectionOutlineInputs& inputs);

private:
    explicit SelectionOutline(rojoRHI::Device& device, bool readback);
    GraphTexture importTarget(RenderGraph& graph);
    // Allocates the output and, once that succeeds, replaces the old one, dropping its entry from
    // `states` when one is given (create() has no table and no old target).
    lmx::Result<void> replaceTarget(uint32_t width, uint32_t height, TextureStateTable* states);
    rojoRHI::Device& m_device;
    bool m_readback;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_maskLibrary;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_outlineLibrary;
    engine::GpuOwned<rojoRHI::GraphicsPipeline> m_maskPipeline;
    engine::GpuOwned<rojoRHI::GraphicsPipeline> m_doubleSidedPipeline;
    engine::GpuOwned<rojoRHI::GraphicsPipeline> m_depthPipeline;
    engine::GpuOwned<rojoRHI::GraphicsPipeline> m_doubleSidedDepthPipeline;
    engine::GpuOwned<rojoRHI::GraphicsPipeline> m_outlinePipeline;
    engine::GpuOwned<rojoRHI::GraphicsPipeline> m_passthroughPipeline;
    engine::GpuOwned<rojoRHI::Sampler> m_sampler;
    engine::GpuTexture m_white;
    engine::GpuTexture m_target;
    // Set by resize(): the target is new and its state is seeded at its next import, through the
    // graph's table, since this utility holds none.
    bool m_targetUnseeded = true;
};

} // namespace lmx::render
