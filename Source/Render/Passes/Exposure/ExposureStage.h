//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureStage.h
/// @brief Declares the private exposure stage owner.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Core/Diagnostics/Result.h"
#include "Engine/Gpu/GpuBuffer.h"
#include "Engine/Gpu/GpuContext.h"
#include "Render/Graph/RenderGraph.h"
#include "Render/Renderer/SceneView.h"

#include <memory>

namespace lmx::render {

// Frame values and graph versions borrowed by the declared pass.
struct ExposureMeteringInputs {
    FrameExtents extents;
    GraphTexture sceneColorRead;
    GraphBuffer exposureCurrent;
};

// Renderer owns this stage for the full lifetime of its deferred graph callbacks.
class ExposureStage {
public:
    static lmx::Result<std::unique_ptr<ExposureStage>> create(engine::GpuContext& gpu,
                                                              bool cpuReadback);
    /// Borrows the persistent exposure owner until the stage is destroyed.
    const engine::GpuBuffer& bufferOwner() const { return m_exposureBuffer; }
    // Seeds manual/reset exposure before the scene reads it; callbacks live through execution.
    GraphBuffer declareSeed(RenderGraph& graph, rojoRHI::CommandList& commands,
                            const SceneView& view, GraphBuffer exposureImport);
    // Declares metering after the raw scene, exporting feedback only in auto mode.
    void declareMetering(RenderGraph& graph, rojoRHI::CommandList& commands, const SceneView& view,
                         const ExposureMeteringInputs& inputs);

private:
    ExposureStage() = default;

    engine::GpuOwned<rojoRHI::ShaderLibrary> m_histogramLibrary;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_exposureResolveLibrary;
    engine::GpuOwned<rojoRHI::ShaderLibrary> m_exposureSeedLibrary;
    engine::GpuOwned<rojoRHI::ComputePipeline> m_histogramPipeline;
    engine::GpuOwned<rojoRHI::ComputePipeline> m_exposureResolvePipeline;
    engine::GpuOwned<rojoRHI::ComputePipeline> m_exposureSeedPipeline;
    engine::GpuBuffer m_histogramBuffer;
    engine::GpuBuffer m_exposureBuffer;
};

} // namespace lmx::render
