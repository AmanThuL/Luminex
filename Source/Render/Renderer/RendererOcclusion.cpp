//----------------------------------------------------------------------------------------------------------------------
/// @file RendererOcclusion.cpp
/// @brief Coordinates source-space depth evidence and independent retired visibility checks.
//----------------------------------------------------------------------------------------------------------------------
#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/Occlusion/HzbFrameData.h"
#include "Render/Passes/Occlusion/HzbHistoryBinding.h"
#include "Render/Passes/Occlusion/OcclusionReference.h"
#include "Render/Passes/Occlusion/OcclusionRoots.h"
#include "Render/Renderer/Renderer.h"
#include "Render/Renderer/RendererInternal.h"
#include <array>
#include <bit>
#include <cstddef>
#include <string>
#include <string_view>

namespace lmx::render {
namespace {
//======================================================================================================================
void registerHzbDebugLayoutsForCapture() {
    const auto field = [](std::string_view name, size_t offset, std::string_view type) {
        return engine::SchemaUniformField{.name = std::string(name),
                                          .offsetBytes = static_cast<uint32_t>(offset),
                                          .type = std::string(type)};
    };
    engine::registerUniformStruct(
        {.name = "HzbDebugRoot",
         .sizeBytes = sizeof(HzbDebugRoot),
         .fields = {field("paramsAddr", offsetof(HzbDebugRoot, paramsAddr), "Addr"),
                    field("pyramid", offsetof(HzbDebugRoot, pyramid), "uint"),
                    field("padding", offsetof(HzbDebugRoot, padding), "uint")}});
    engine::registerUniformStruct(
        {.name = "HzbDebugViewParams",
         .sizeBytes = sizeof(HzbDebugViewParams),
         .fields = {field("level", offsetof(HzbDebugViewParams, level), "uint"),
                    field("activeWidth", offsetof(HzbDebugViewParams, activeWidth), "uint"),
                    field("activeHeight", offsetof(HzbDebugViewParams, activeHeight), "uint"),
                    field("padding", offsetof(HzbDebugViewParams, padding), "uint"),
                    field("outputWidth", offsetof(HzbDebugViewParams, outputWidth), "uint"),
                    field("outputHeight", offsetof(HzbDebugViewParams, outputHeight), "uint"),
                    field("padding2", offsetof(HzbDebugViewParams, padding2), "uint"),
                    field("padding3", offsetof(HzbDebugViewParams, padding3), "uint")}});
}
} // namespace

//======================================================================================================================
lmx::Result<RendererOcclusionFrame>
Renderer::prepareOcclusion(RenderGraph& graph, rojoRHI::CommandList& commands,
                           const engine::Camera& camera, const SceneView& view,
                           const FrameExtents& extents, const CameraFrameState& cameraState) {
    LMX_ASSERT(!view.occlusionEnabled ||
                   (view.classifyMode == ClassifyMode::Gpu && view.visibilityEnabled),
               "occlusion requires GPU classification and culling");
    LMX_ASSERT(!view.occlusionCheck || view.occlusionEnabled, "occlusion check requires occlusion");
    RendererOcclusionFrame result;
    if (view.occlusionEnabled) {
        // Every fallible owner is created before any pass is declared, so the only failure after
        // the visibility declaration is the reference's own allocation.
        if (!m_hzbStage) {
            auto stage = HzbStage::create(m_gpu, m_cpuReadback);
            if (!stage)
                return std::unexpected(stage.error());
            const auto resized = (*stage)->resize(m_width, m_height, m_textureStates);
            if (!resized)
                return std::unexpected(resized.error());
            m_hzbStage = std::move(*stage);
        }
        if (view.occlusionCheck && !m_occlusionReference) {
            auto reference = OcclusionReference::create(m_gpu);
            if (!reference)
                return std::unexpected(reference.error());
            m_occlusionReference = std::move(*reference);
        }
        if (view.hzbDebugLevel >= 0 && !m_hzbDebugPipeline) {
            auto library = m_gpu.loadShaderLibrary("HzbDebugView");
            if (!library)
                return std::unexpected(library.error());
            auto pipeline = createFullscreenPipeline(m_gpu.device(), library->get(), "fragmentMain",
                                                     kDisplayFormat, "lmx.render.hzbDebugPipeline");
            if (!pipeline)
                return std::unexpected(pipeline.error());
            m_hzbDebugLibrary = std::move(*library);
            m_hzbDebugPipeline = std::move(*pipeline);
            registerHzbDebugLayoutsForCapture();
        }
        result.graph = m_hzbStage->beginGraph(graph, commands);
        result.history = m_hzbStage->importPrevious(*result.graph);
        result.association = createOcclusionDeclarationAssociation(m_gpu, m_gpu.frameNumber(),
                                                                   view.temporal.sceneGeneration);
    }
    const OcclusionFrameFacts current{.frameNumber = m_gpu.frameNumber(),
                                      .sceneGeneration = view.temporal.sceneGeneration,
                                      .coverageEpoch = view.coverageEpoch,
                                      .outputWidth = m_width,
                                      .outputHeight = m_height,
                                      .cameraPosition = camera.position,
                                      .cameraForward = camera.forward(),
                                      .enabled = view.occlusionEnabled,
                                      .wireframe = view.wireframe,
                                      .cameraCut = view.temporal.cameraCut};
    // The declared facts of the last committed build. Whether its pyramid actually recorded is
    // the history binding's decision at final compile, which can only withdraw this validity.
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
    result.reason = occlusionHistoryReason(previous, current, m_occlusionPreviouslyEnabled);
    const auto rasterMatrix =
        view.temporal.enabled ? cameraState.viewProjectionJittered : cameraState.viewProjection;
    result.current = {.frameNumber = current.frameNumber,
                      .viewProjection = rasterMatrix,
                      .cameraPosition = current.cameraPosition,
                      .cameraForward = current.cameraForward,
                      .coverageEpoch = current.coverageEpoch,
                      .sceneGeneration = current.sceneGeneration,
                      .activeWidth = extents.renderWidth,
                      .activeHeight = extents.renderHeight,
                      .outputWidth = m_width,
                      .outputHeight = m_height};
    result.strictView = result.reason == OcclusionInvalidReason::None &&
                        m_previousOcclusionViewProjection &&
                        *m_previousOcclusionViewProjection == cameraState.viewProjection &&
                        m_occlusionSource.activeWidth == extents.renderWidth &&
                        m_occlusionSource.activeHeight == extents.renderHeight;
    result.viewProjection = cameraState.viewProjection;
    result.params =
        makeOcclusionParams(m_occlusionSource.viewProjection, m_occlusionSource.activeWidth,
                            m_occlusionSource.activeHeight, m_occlusionSource.levelCount,
                            view.occlusionEnabled, result.reason == OcclusionInvalidReason::None);
    return result;
}

//======================================================================================================================
lmx::Result<void> Renderer::declareOcclusion(RenderGraph& graph, rojoRHI::CommandList& commands,
                                             const SceneView& view, GraphTexture depth,
                                             GraphTexture& display,
                                             RendererOcclusionFrame& occlusion) {
    if (!view.occlusionEnabled)
        return {};
    const auto pyramid = m_hzbStage->declare(*occlusion.graph, depth, occlusion.current);
    // The stage completes the facts with its allocation's output extent and level count.
    occlusion.current = m_hzbStage->previousSource();
    if (view.occlusionCheck) {
        // Retirement joins the reference with production, so it never retires on its own here.
        const auto declared = m_occlusionReference->declareCombined(
            graph, commands, view,
            {.viewProjection = occlusion.current.viewProjection,
             .width = occlusion.current.activeWidth,
             .height = occlusion.current.activeHeight,
             .association = occlusion.association,
             .strictView = occlusion.strictView});
        if (!declared)
            return std::unexpected(declared.error());
    }
    if (view.hzbDebugLevel < 0)
        return {};
    PassDesc pass;
    pass.color = ColorAttachment{.handle = display, .load = LoadOp::Load, .store = StoreOp::Store};
    pass.textureReads.emplace_back(pyramid);
    const auto params = std::bit_cast<HzbDebugViewParams>(
        buildHzbDebugParams({.requestedLevel = static_cast<uint32_t>(view.hzbDebugLevel),
                             .levelCount = occlusion.current.levelCount,
                             .activeWidth = occlusion.current.activeWidth,
                             .activeHeight = occlusion.current.activeHeight,
                             .outputWidth = m_width,
                             .outputHeight = m_height}));
    graph.addPass(
        "lmx.pass.hzb.debug", std::move(pass),
        [this, &commands, pyramid, params](const PassResources& resources) {
            commands.setPipeline(m_hzbDebugPipeline.get());
            const auto address = frameData(commands, params);
            engine::registerFrameRoot("HzbDebugViewParams", address);
            engine::registerFrameRoot(
                "HzbDebugRoot", commands.pushRoot(HzbDebugRoot{
                                    .paramsAddr = address,
                                    .pyramid = static_cast<uint32_t>(resources.view(pyramid))}));
#ifndef NDEBUG
            const std::array<rojoRHI::ResourceAccess, 1> accesses{
                {{.texture = resources.texture(pyramid).texture,
                  .range =
                      {.baseMip = params.level, .mipCount = 1, .baseLayer = 0, .layerCount = 1},
                  .stage = rojoRHI::Stage::FragmentShader,
                  .access = rojoRHI::Access::ShaderRead}}};
            commands.declareAccess(accesses);
#endif
            commands.draw(3);
        });
    display = nextVersion(display);
    return {};
}

//======================================================================================================================
void Renderer::commitVisibility(RendererVisibilityFrame&& visibility,
                                RendererOcclusionFrame&& occlusion) {
    m_visibilityStatus = std::move(visibility.status);
    if (occlusion.current.built)
        m_occlusionSource = occlusion.current;
    m_occlusionPreviouslyEnabled = m_visibilityStatus.occlusionEnabled;
    m_previousOcclusionViewProjection = occlusion.viewProjection;
}

} // namespace lmx::render
