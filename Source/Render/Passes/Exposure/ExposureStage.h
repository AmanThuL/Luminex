//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureStage.h
/// @brief Declares the private exposure stage owner.
//----------------------------------------------------------------------------------------------------------------------

#pragma once
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
    static rojoRHI::Result<std::unique_ptr<ExposureStage>> create(rojoRHI::Device& device,
                                                                  bool cpuReadback);
    // The frame composition root imports the persistent pair before scene declaration.
    rojoRHI::Buffer& buffer() { return *m_exposureBuffer; }
    // Seeds manual/reset exposure before the scene reads it; callbacks live through execution.
    GraphBuffer declareSeed(RenderGraph& graph, rojoRHI::CommandList& commands,
                            const SceneView& view, GraphBuffer exposureImport);
    // Declares metering after the raw scene, exporting feedback only in auto mode.
    void declareMetering(RenderGraph& graph, rojoRHI::CommandList& commands, const SceneView& view,
                         const ExposureMeteringInputs& inputs);

private:
    ExposureStage() = default;

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
