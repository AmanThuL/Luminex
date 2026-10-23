//----------------------------------------------------------------------------------------------------------------------
/// @file BloomStage.h
/// @brief Declares the private bloom stage owner.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#ifndef LMX_PENDING_POST
#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuContext.h"
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
    static lmx::Result<std::unique_ptr<BloomStage>> create(engine::GpuContext& gpu);
    // Declares threshold/downsample/upsample even when display will cull their unused result.
    GraphTexture declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                         const BloomInputs& inputs);

private:
    BloomStage() = default;

    engine::GpuOwned<rojoRHI::ShaderLibrary> m_bloomThresholdLibrary;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_bloomDownsampleLibrary;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_bloomUpsampleLibrary;
    engine::GpuOwned<rojoRHI::ComputePipeline> m_bloomThresholdPipeline;
    engine::GpuOwned<rojoRHI::ComputePipeline> m_bloomDownsamplePipeline;
    engine::GpuOwned<rojoRHI::ComputePipeline> m_bloomUpsamplePipeline;
};

} // namespace lmx::render

#endif
