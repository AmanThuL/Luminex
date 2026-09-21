//----------------------------------------------------------------------------------------------------------------------
/// @file BloomStage.h
/// @brief Declares the private bloom stage owner.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Graph/RenderGraph.h"
#include "Render/Renderer/SceneView.h"

#include <memory>

namespace lmx::render {

// Frame values and graph versions borrowed by the declared pass.
struct BloomInputs {
    GraphTexture displayInput;
    uint32_t sceneWidth;
    uint32_t sceneHeight;
    float bloomThreshold;
};

// Renderer owns this stage for the full lifetime of its deferred graph callbacks.
class BloomStage {
public:
    static rojoRHI::Result<std::unique_ptr<BloomStage>> create(rojoRHI::Device& device);
    // Declares threshold/downsample/upsample even when display will cull their unused result.
    GraphTexture declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                         const BloomInputs& inputs);

private:
    BloomStage() = default;

    std::unique_ptr<rojoRHI::ShaderLibrary> m_bloomThresholdLibrary;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_bloomDownsampleLibrary;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_bloomUpsampleLibrary;
    std::unique_ptr<rojoRHI::ComputePipeline> m_bloomThresholdPipeline;
    std::unique_ptr<rojoRHI::ComputePipeline> m_bloomDownsamplePipeline;
    std::unique_ptr<rojoRHI::ComputePipeline> m_bloomUpsamplePipeline;
};

} // namespace lmx::render
