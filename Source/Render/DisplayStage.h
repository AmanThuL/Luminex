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
    rojoRHI::Result<void> loadLibraries(rojoRHI::Device& device);
    rojoRHI::Result<void> createPipelines(rojoRHI::Device& device);
    // Allocates the valid black texture bound when bloom is disabled.
    rojoRHI::Result<void> createResources(rojoRHI::Device& device);
    // Declares the output transform, with its input versions fixed before callbacks execute.
    void declare(RenderGraph& graph, rojoRHI::CommandList& commands, GraphTexture displayInput,
                 GraphTexture bloomResult, GraphTexture displayColor, bool bloomEnabled,
                 float bloomIntensity);

private:
    std::unique_ptr<rojoRHI::ShaderLibrary> m_displayLibrary;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_displayPipeline;
    std::unique_ptr<rojoRHI::Texture> m_blackBloomFallback;
};

} // namespace lmx::render
