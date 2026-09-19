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
    rojoRHI::Result<void> loadLibraries(rojoRHI::Device& device);
    rojoRHI::Result<void> createPipelines(rojoRHI::Device& device);
    // Allocates persistent metering and {applied, previous} exposure feedback.
    rojoRHI::Result<void> createResources(rojoRHI::Device& device, bool cpuReadback);
    // The frame composition root imports the persistent pair before scene declaration.
    rojoRHI::Buffer& buffer() { return *m_exposureBuffer; }
    // Seeds manual/reset exposure before the scene reads it; callbacks live through execution.
    GraphBuffer declareSeed(RenderGraph& graph, rojoRHI::CommandList& commands,
                            const SceneView& view, GraphBuffer exposureImport);
    // Declares metering after the raw scene, exporting feedback only in auto mode.
    void declareMetering(RenderGraph& graph, rojoRHI::CommandList& commands, const SceneView& view,
                         const FrameExtents& extents, GraphTexture sceneColorRead,
                         GraphBuffer exposureCurrent);

private:
    std::unique_ptr<rojoRHI::ShaderLibrary> m_histogramLibrary;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_exposureResolveLibrary;
    std::unique_ptr<rojoRHI::ShaderLibrary> m_exposureSeedLibrary;
    std::unique_ptr<rojoRHI::ComputePipeline> m_histogramPipeline;
    std::unique_ptr<rojoRHI::ComputePipeline> m_exposureResolvePipeline;
    std::unique_ptr<rojoRHI::ComputePipeline> m_exposureSeedPipeline;
    std::unique_ptr<rojoRHI::Buffer> m_histogramBuffer;
    std::unique_ptr<rojoRHI::Buffer> m_exposureBuffer;
};

} // namespace lmx::render
