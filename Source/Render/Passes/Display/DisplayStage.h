//----------------------------------------------------------------------------------------------------------------------
/// @file DisplayStage.h
/// @brief Declares the private display stage owner.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/Graph/RenderGraph.h"
#include "Render/Renderer/SceneView.h"

#include <memory>

namespace lmx::render {

// Frame values and graph versions borrowed by the declared pass.
struct DisplayInputs {
    GraphTexture displayInput;
    GraphTexture bloomResult;
    GraphTexture displayColor;
    bool bloomEnabled;
    float bloomIntensity;
};

// Renderer owns this stage for the full lifetime of its deferred graph callbacks.
class DisplayStage {
public:
    static rojoRHI::Result<std::unique_ptr<DisplayStage>> create(rojoRHI::Device& device);
    // Declares the output transform, with its input versions fixed before callbacks execute.
    void declare(RenderGraph& graph, rojoRHI::CommandList& commands, const DisplayInputs& inputs);

private:
    DisplayStage() = default;

    std::unique_ptr<rojoRHI::ShaderLibrary> m_displayLibrary;
    std::unique_ptr<rojoRHI::GraphicsPipeline> m_displayPipeline;
    std::unique_ptr<rojoRHI::Texture> m_blackBloomFallback;
};

} // namespace lmx::render
