//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureStage.h
/// @brief Declares the private exposure stage owner.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
#include "Render/RenderGraph.h"
#include "Render/SceneView.h"

#include <memory>

namespace lmx::render {

// Renderer owns this stage for the full lifetime of its deferred graph callbacks.
class ExposureStage {
public:
    // Initialization phases preserve Renderer's resource creation order.
    rhi::Result<void> loadLibraries(rhi::Device& device);
    rhi::Result<void> createPipelines(rhi::Device& device);
    // Allocates persistent metering and {applied, previous} exposure feedback.
    rhi::Result<void> createResources(rhi::Device& device, bool cpuReadback);
    // The frame composition root imports the persistent pair before scene declaration.
    rhi::Buffer& buffer() { return *m_exposureBuffer; }
    // Seeds manual/reset exposure before the scene reads it; callbacks live through execution.
    GraphBuffer declareSeed(RenderGraph& graph, rhi::CommandList& commands, const SceneView& view,
                            GraphBuffer exposureImport);
    // Declares metering after the raw scene, exporting feedback only in auto mode.
    void declareMetering(RenderGraph& graph, rhi::CommandList& commands, const SceneView& view,
                         const FrameExtents& extents, GraphTexture sceneColorRead,
                         GraphBuffer exposureCurrent);

private:
    std::unique_ptr<rhi::ShaderLibrary> m_histogramLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_exposureResolveLibrary;
    std::unique_ptr<rhi::ShaderLibrary> m_exposureSeedLibrary;
    std::unique_ptr<rhi::ComputePipeline> m_histogramPipeline;
    std::unique_ptr<rhi::ComputePipeline> m_exposureResolvePipeline;
    std::unique_ptr<rhi::ComputePipeline> m_exposureSeedPipeline;
    std::unique_ptr<rhi::Buffer> m_histogramBuffer;
    std::unique_ptr<rhi::Buffer> m_exposureBuffer;
};

} // namespace lmx::render
