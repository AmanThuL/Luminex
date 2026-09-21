//----------------------------------------------------------------------------------------------------------------------
/// @file RendererOcclusion.cpp
/// @brief Coordinates source-space depth evidence and independent retired visibility checks.
//----------------------------------------------------------------------------------------------------------------------
#include "Core/Diagnostics/Assert.h"
#include "Render/Passes/Occlusion/OcclusionReference.h"
#include "Render/Renderer/Renderer.h"
#include <algorithm>
namespace lmx::render {
//======================================================================================================================
GraphTexture Renderer::prepareOcclusion(RenderGraph& graph, const engine::Camera& camera,
                                        const SceneView& view, const FrameExtents& extents,
                                        const CameraFrameState& cameraState) {
    LMX_ASSERT(!view.occlusionEnabled ||
                   (view.classifyMode == ClassifyMode::Gpu && view.visibilityEnabled),
               "occlusion requires GPU classification and culling");
    LMX_ASSERT(!view.occlusionCheck || view.occlusionEnabled, "occlusion check requires occlusion");
    const OcclusionFrameFacts current{.frameNumber = m_device.frameNumber(),
                                      .sceneGeneration = view.temporal.sceneGeneration,
                                      .coverageEpoch = view.coverageEpoch,
                                      .outputWidth = m_width,
                                      .outputHeight = m_height,
                                      .cameraPosition = camera.position,
                                      .cameraForward = camera.forward(),
                                      .enabled = view.occlusionEnabled,
                                      .wireframe = view.wireframe,
                                      .cameraCut = view.temporal.cameraCut};
    std::optional<OcclusionFrameFacts> previous;
    if (m_occlusionSource.built)
        previous = OcclusionFrameFacts{.frameNumber = m_occlusionSource.frameNumber,
                                       .sceneGeneration = m_occlusionSource.sceneGeneration,
                                       .coverageEpoch = m_occlusionSource.coverageEpoch,
                                       .outputWidth = m_occlusionSource.outputWidth,
                                       .outputHeight = m_occlusionSource.outputHeight,
                                       .cameraPosition = m_occlusionSource.cameraPosition,
                                       .cameraForward = m_occlusionSource.cameraForward,
                                       .enabled = true};
    m_occlusionReason = occlusionHistoryReason(previous, current, m_occlusionPreviouslyEnabled);
    const auto rasterMatrix =
        view.temporal.enabled ? cameraState.viewProjectionJittered : cameraState.viewProjection;
    m_currentOcclusionSource = {.frameNumber = current.frameNumber,
                                .viewProjection = rasterMatrix,
                                .cameraPosition = current.cameraPosition,
                                .cameraForward = current.cameraForward,
                                .coverageEpoch = current.coverageEpoch,
                                .sceneGeneration = current.sceneGeneration,
                                .activeWidth = extents.renderWidth,
                                .activeHeight = extents.renderHeight,
                                .outputWidth = m_width,
                                .outputHeight = m_height};
    m_occlusionStrictView = m_occlusionReason == OcclusionInvalidReason::None &&
                            m_previousOcclusionViewProjection &&
                            *m_previousOcclusionViewProjection == cameraState.viewProjection &&
                            m_occlusionSource.activeWidth == extents.renderWidth &&
                            m_occlusionSource.activeHeight == extents.renderHeight;
    m_previousOcclusionViewProjection = cameraState.viewProjection;
    m_occlusionPreviouslyEnabled = view.occlusionEnabled;
    m_occlusionParams = makeOcclusionParams(
        m_occlusionSource.viewProjection, m_occlusionSource.activeWidth,
        m_occlusionSource.activeHeight, m_occlusionSource.levelCount, view.occlusionEnabled,
        m_occlusionReason == OcclusionInvalidReason::None);
    if (!view.occlusionEnabled)
        return {};
    if (!m_hzbStage) {
        auto stage = HzbStage::create(m_device, m_cpuReadback);
        LMX_ASSERT(stage.has_value(), stage.error().message);
        m_hzbStage = std::move(*stage);
        const auto resized = m_hzbStage->resize(m_width, m_height);
        LMX_ASSERT(resized.has_value(), resized.error().message);
    }
    return m_hzbStage->importPrevious(graph);
}
//======================================================================================================================
void Renderer::declareOcclusion(RenderGraph& graph, rojoRHI::CommandList& commands,
                                const SceneView& view, GraphTexture depth, GraphTexture& display) {
    if (!view.occlusionEnabled)
        return;
    const auto pyramid = m_hzbStage->declare(graph, commands, depth, m_currentOcclusionSource);
    m_occlusionSource = m_hzbStage->previousSource();
    if (view.occlusionCheck) {
        if (!m_occlusionReference) {
            auto reference = OcclusionReference::create(m_device);
            LMX_ASSERT(reference.has_value(), reference.error().message);
            m_occlusionReference = std::move(*reference);
        }
        const auto declared = m_occlusionReference->declare(
            graph, commands, view,
            {.viewProjection = m_currentOcclusionSource.viewProjection,
             .width = m_currentOcclusionSource.activeWidth,
             .height = m_currentOcclusionSource.activeHeight,
             .strictView = m_occlusionStrictView});
        LMX_ASSERT(declared.has_value(), declared.error().message);
    }
    if (view.hzbDebugLevel < 0)
        return;
    if (!m_hzbDebugPipeline) {
        auto library = m_device.loadShaderLibrary("Shaders/HzbDebugView");
        LMX_ASSERT(library.has_value(), library.error().message);
        m_hzbDebugLibrary = std::move(*library);
        auto pipeline = m_device.createGraphicsPipeline({.library = m_hzbDebugLibrary.get(),
                                                         .vertexEntry = "vertexMain",
                                                         .fragmentEntry = "fragmentMain",
                                                         .colorFormat = kDisplayFormat,
                                                         .depthFormat = rojoRHI::Format::Unknown,
                                                         .cullMode = rojoRHI::CullMode::None,
                                                         .label = "lmx.render.hzbDebugPipeline"});
        LMX_ASSERT(pipeline.has_value(), pipeline.error().message);
        m_hzbDebugPipeline = std::move(*pipeline);
    }
    PassDesc pass;
    pass.color = ColorAttachment{.handle = display, .load = LoadOp::Load, .store = StoreOp::Store};
    pass.textureReads.push_back(pyramid);
    const std::array<uint32_t, 8> params{
        std::min(uint32_t(view.hzbDebugLevel), m_occlusionSource.levelCount - 1),
        m_occlusionSource.activeWidth,
        m_occlusionSource.activeHeight,
        0,
        m_width,
        m_height,
        0,
        0};
    graph.addPass("lmx.pass.hzb.debug", std::move(pass),
                  [this, &commands, pyramid, params](const PassResources& resources) {
                      commands.bindPipeline(*m_hzbDebugPipeline);
                      commands.bindTexture(0, **resources.texture(pyramid));
                      commands.bindFrameData(0, params);
                      commands.draw(3);
                  });
    display = nextVersion(display);
}
} // namespace lmx::render
