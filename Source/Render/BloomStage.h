//----------------------------------------------------------------------------------------------------------------------
/// @file BloomStage.h
/// @brief Declares the private bloom stage owner.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/RenderGraph.h"
#include "Render/SceneView.h"

#include <memory>

namespace lmx::render {

// Renderer owns this stage for the full lifetime of its deferred graph callbacks.
class BloomStage {
public:
    // Initialization phases preserve Renderer's resource creation order.
    rojoRHI::Result<void> loadLibraries(rojoRHI::Device& device);
    rojoRHI::Result<void> createPipelines(rojoRHI::Device& device);
    // Declares threshold/downsample/upsample even when display will cull their unused result.
    GraphTexture declare(RenderGraph& graph, rojoRHI::CommandList& commands, GraphTexture displayInput,
                         uint32_t sceneWidth, uint32_t sceneHeight, float bloomThreshold);

private:
    std::unique_ptr<rojoRHI::ShaderLibrary> m_bloomThresholdLibrary;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_bloomDownsampleLibrary;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_bloomUpsampleLibrary;
    std::unique_ptr<rojoRHI::ComputePipeline> m_bloomThresholdPipeline;
    std::unique_ptr<rojoRHI::ComputePipeline> m_bloomDownsamplePipeline;
    std::unique_ptr<rojoRHI::ComputePipeline> m_bloomUpsamplePipeline;
};

} // namespace lmx::render
