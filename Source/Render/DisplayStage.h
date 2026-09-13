//----------------------------------------------------------------------------------------------------------------------
/// @file DisplayStage.h
/// @brief Declares the private display stage owner.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/RenderGraph.h"
#include "Render/SceneView.h"

#include <memory>

namespace lmx::render {

// Renderer owns this stage for the full lifetime of its deferred graph callbacks.
class DisplayStage {
public:
    // Initialization phases preserve Renderer's resource creation order.
    rhi::Result<void> loadLibraries(rhi::Device& device);
    rhi::Result<void> createPipelines(rhi::Device& device);
    // Allocates the valid black texture bound when bloom is disabled.
    rhi::Result<void> createResources(rhi::Device& device);
    // Declares the output transform, with its input versions fixed before callbacks execute.
    void declare(RenderGraph& graph, rhi::CommandList& commands, GraphTexture displayInput,
                 GraphTexture bloomResult, GraphTexture displayColor, bool bloomEnabled,
                 float bloomIntensity);

private:
    std::unique_ptr<rhi::ShaderLibrary> m_displayLibrary;
    std::unique_ptr<rhi::GraphicsPipeline> m_displayPipeline;
    std::unique_ptr<rhi::Texture> m_blackBloomFallback;
};

} // namespace lmx::render
