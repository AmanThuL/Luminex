//----------------------------------------------------------------------------------------------------------------------
/// @file DisplayStage.h
/// @brief Declares the private display stage owner.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuContext.h"
#include "Engine/Gpu/GpuTexture.h"
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
    static lmx::Result<std::unique_ptr<DisplayStage>> create(engine::GpuContext& gpu);
    // Declares the output transform, with its input versions fixed before callbacks execute.
    void declare(RenderGraph& graph, rojoRHI::CommandList& commands, const DisplayInputs& inputs);

private:
    DisplayStage() = default;

    engine::GpuOwned<rojoRHI::ShaderLibrary> m_displayLibrary;
    engine::GpuOwned<rojoRHI::Pipeline> m_displayPipeline;
    engine::GpuTexture m_blackBloomFallback;
};

} // namespace lmx::render
