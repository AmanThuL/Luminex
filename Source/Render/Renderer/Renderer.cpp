//----------------------------------------------------------------------------------------------------------------------
/// @file Renderer.cpp
/// @brief Wires the frame stages and executes the renderer-owned graph.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer/Renderer.h"
#include "Render/Renderer/RendererStandIns.h"

#include "Core/Diagnostics/Assert.h"
#ifndef LMX_PENDING_POST
#include "Render/Passes/Bloom/BloomStage.h"
#endif
#include "Render/Passes/Display/DisplayStage.h"
#ifndef LMX_PENDING_POST
#include "Render/Passes/Exposure/ExposureStage.h"
#endif
#include "Render/Renderer/RendererInternal.h"

namespace lmx::render {
//======================================================================================================================
GraphTexture Renderer::declarePasses(RenderGraph& graph, rojoRHI::CommandList& commands,
                                     const engine::Camera& camera, const SceneView& view) {
    // Bound first, so every persistent import of the frame -- the renderer's, its stages' and any a
    // caller declares into the same graph afterwards -- reads the uses earlier frames left.
    graph.bindTextureStates(m_textureStates);
    const RendererFrameState state = deriveFrameState(camera, view);
    const RendererFrameImports imports = importFrameResources(graph, commands, view, state);

#ifndef LMX_PENDING_LIGHTS
    const auto lightClusters = prepareLighting(graph, commands, view, imports.lightsImport,
                                               state.extents, state.cameraState);
#endif

#ifndef LMX_PENDING_SCENE
    const auto planes =
        extractFrustumPlanes(state.temporalEnabled ? state.cameraState.viewProjectionJittered
                                                   : state.cameraState.viewProjection);
#endif

#ifndef LMX_PENDING_VISIBILITY
    m_previousPyramid = prepareOcclusion(graph, camera, view, state.extents, state.cameraState);
#endif

#ifndef LMX_PENDING_SCENE
    const auto drawBuffers = prepareVisibility(graph, commands, view, planes, imports.sceneBuffers);
    const auto drawRows = drawBuffers[0];
    const auto drawArguments = drawBuffers[1];

    const GraphTexture shadowRead =
        m_shadowStage->declare(graph, commands, view,
                               {.draws = m_drawSubmission.shadow(),
                                .drawRows = drawRows,
                                .drawArguments = drawArguments,
                                .sceneBuffers = imports.sceneBuffers,
                                .shadowMap = imports.shadowMap,
                                .lightViewProj = state.shadow.viewProj,
                                .whiteTexture = &m_whiteTexture,
                                .linearSampler = m_linearSampler.get()});
#ifdef LMX_PENDING_LIGHTS
    SceneView unlitView = view;
    unlitView.localLightMode = engine::LocalLightMode::Off;
#endif
    const GraphTexture sceneColorRead = m_sceneStage->declare(
#ifndef LMX_PENDING_LIGHTS
        graph, commands, view,
#else
        graph, commands, unlitView,
#endif
        {.draws = m_drawSubmission.scene(),
         .drawRows = drawRows,
         .drawArguments = drawArguments,
         .sceneBuffers = imports.sceneBuffers,
         .lights = imports.lightsImport,
#ifndef LMX_PENDING_LIGHTS
         .lightGrid = lightClusters.declared ? std::optional{lightClusters.grid} : std::nullopt,
         .lightIndices =
             lightClusters.declared ? std::optional{lightClusters.indices} : std::nullopt,
#else
         .lightGrid = std::nullopt,
         .lightIndices = std::nullopt,
#endif
         .sceneColor = imports.sceneColor,
         .sceneDepth = imports.sceneDepth,
         .shadowRead = shadowRead,
         .motion = imports.motionTargetHandle,
         .reactive = imports.reactiveTargetHandle,
         .exposure = imports.exposureCurrent,
         .camera = state.cameraState,
         .previousCamera = state.previousCamera,
         .eyePosition = camera.position,
         .shadowTransform = state.shadow.shadowTransform,
         .extents = state.extents,
         .jitterPixels = state.jitterPixels,
         .clearColor = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]},
         .timeSeconds = timeSeconds,
         .temporalEnabled = state.temporalEnabled,
         .whiteTexture = &m_whiteTexture,
         .flatNormalTexture = &m_flatNormalTexture,
         .blackCubeTexture = &m_blackCubeTexture,
         .zeroDfgTexture = &m_zeroDfgTexture,
         .linearSampler = m_linearSampler.get(),
         .shadowSampler = m_shadowSampler.get(),
         .iblSampler = m_iblSampler.get()});
#else
    const GraphTexture sceneColorRead =
        declareSceneClear(graph, imports.sceneColor, imports.sceneDepth, clearColor);
#endif
    const uint32_t sceneWidth = m_width;
    const uint32_t sceneHeight = m_height;
#ifndef LMX_PENDING_TEMPORAL
    const GraphTexture motionRead =
        state.temporalEnabled ? nextVersion(imports.motionTargetHandle) : GraphTexture{};
#endif

    // ---- Temporal reconstruction (M6.2 spec 6). The stage declares the reprojection diagnostic,
    // this frame's accumulation or its raw commit, and the debug view; this frame routes what it
    // produced into bloom and display. The debug view draws over the version the display pass
    // produces, which is declared further down -- the graph's schedule is topological, so naming
    // that version here still orders the view behind its producer.
    GraphTexture displayResult = nextVersion(imports.displayColor);
#ifndef LMX_PENDING_TEMPORAL
    TemporalResolveOutputs temporalOutputs;
    if (state.temporalEnabled) {
        TemporalInputs temporalInputs;
        temporalInputs.sceneColor = sceneColorRead;
        temporalInputs.depth = nextVersion(imports.sceneDepth);
        temporalInputs.previousDepth = imports.previousDepth;
        temporalInputs.motion = motionRead;
        temporalInputs.reactive = nextVersion(imports.reactiveTargetHandle);
        temporalInputs.history = imports.historyImport;
        temporalInputs.colorSlot = imports.colorSlotImport;
        // The version the scene pass and the histogram read: after the seed, so "applied this
        // frame" means the same thing to shading, metering and the exposure correction alike.
        temporalInputs.exposure = imports.exposureCurrent;
        temporalInputs.camera = state.cameraState;
        temporalInputs.previousCamera = state.previousCamera;
        temporalInputs.extents = state.extents;
        temporalInputs.previousExtents = state.previousExtents;
        temporalInputs.resetReason = state.resetReason;
        temporalInputs.mode = state.reconstruction;
        temporalOutputs = m_temporalResolve->declare(graph, commands, temporalInputs,
                                                     state.debugView, displayResult);
        displayResult = temporalOutputs.displayResult;
    }
#endif

    // What bloom and the display transform read: the colour slot whenever the frame accumulated or
    // upscaled into it -- both leave the finished output-extent picture there -- and the raw
    // jittered frame otherwise. Only a Raw frame that rasterised at the output extent still reads
    // scene colour. The histogram deliberately keeps metering the raw scene colour, so metering
    // stays independent of the accumulation it corrects.
#ifndef LMX_PENDING_TEMPORAL
    const GraphTexture displayInput = state.nativeTaa || state.vendorTemporal || state.upscaled
                                          ? temporalOutputs.resolved
                                          : sceneColorRead;
#else
    const GraphTexture displayInput = sceneColorRead;
#endif

#ifndef LMX_PENDING_POST
    m_exposureStage->declareMetering(graph, commands, view,
                                     {.extents = state.extents,
                                      .sceneColorRead = sceneColorRead,
                                      .exposureCurrent = imports.exposureCurrent});

    const GraphTexture bloomResult = m_bloomStage->declare(graph, commands,
                                                           {.displayInput = displayInput,
                                                            .sceneWidth = sceneWidth,
                                                            .sceneHeight = sceneHeight,
                                                            .bloomThreshold = view.bloomThreshold});
#else
    const GraphTexture bloomResult;
#endif

#ifndef LMX_PENDING_LIGHTS
    const bool lightDebugEnabled =
        view.lightDebugView != engine::LightDebugView::Off && lightClusters.declared;
    LMX_ASSERT(view.lightDebugView == engine::LightDebugView::Off ||
                   (view.localLightMode == engine::LocalLightMode::Clustered &&
                    state.debugView == TemporalDebugView::Off && view.hzbDebugLevel < 0),
               "light debug views require clustered lighting and no other diagnostic view");
    // Diagnostics sample the display result without feeding back into HDR or temporal history.
    // A separate transient source preserves the public display target and needs only one extra
    // pass.
    const auto displayDestination = lightDebugEnabled
                                        ? graph.createTexture({.width = m_width,
                                                               .height = m_height,
                                                               .format = kDisplayFormat,
                                                               .renderTarget = true,
                                                               .sampled = true},
                                                              "lmx.render.lightDebugSource")
                                        : imports.displayColor;
#else
    const GraphTexture displayDestination = imports.displayColor;
#endif
    m_displayStage->declare(graph, commands,
                            {.displayInput = displayInput,
                             .bloomResult = bloomResult,
                             .displayColor = displayDestination,
#ifndef LMX_PENDING_POST
                             .bloomEnabled = view.bloomEnabled,
#else
                             .bloomEnabled = false,
#endif
                             .bloomIntensity = view.bloomIntensity});
#ifndef LMX_PENDING_LIGHTS
    if (lightDebugEnabled) {
        if (!m_lightDebug) {
            auto stage = LightDebugStage::create(m_gpu);
            LMX_ASSERT(stage.has_value(), stage.error().message);
            m_lightDebug = std::move(*stage);
        }
        LightClusterParams clusterParams;
        clusterParams.rowCount = view.tables.lightRowCount;
        clusterParams.activeWidth = state.extents.renderWidth;
        clusterParams.activeHeight = state.extents.renderHeight;
        clusterParams.sliceDepth = clusterSliceDepths(camera.nearZ);
        displayResult = m_lightDebug->declare(
            graph, commands,
            {.mode = view.lightDebugView,
             .depth = nextVersion(imports.sceneDepth),
             .display = nextVersion(displayDestination),
             .output = imports.displayColor,
             .lights = *imports.lightsImport,
             .grid = lightClusters.grid,
             .indices = lightClusters.indices,
             .clusters = clusterParams,
             .inverseViewProjection =
                 glm::inverse(state.temporalEnabled ? state.cameraState.viewProjectionJittered
                                                    : state.cameraState.viewProjection),
             .outputWidth = m_width,
             .outputHeight = m_height});
    }
#endif

#ifndef LMX_PENDING_VISIBILITY
    // HZB follows every temporal depth consumer in the stable graph schedule.
    declareOcclusion(graph, commands, view, nextVersion(imports.sceneDepth), displayResult);
#endif

    recordFrameStatus(state);
    return displayResult;
}

//======================================================================================================================
void Renderer::render(rojoRHI::CommandList& commands, const engine::Camera& camera,
                      const SceneView& view) {
    // declarePasses() always declares bloom's transients (spec 10: it is an ordinary graph pass
    // whether or not a caller's own graph has a pool), so this convenience path needs one of its
    // own -- the caller already opened this frame with Device::beginFrame() before calling here,
    // which is what m_transientPool.beginFrame() requires.
    m_transientPool.beginFrame();
    RenderGraph graph(m_transientPool);
    const GraphTexture displayColor = declarePasses(graph, commands, camera, view);
    // The display-transformed image is this frame's whole result, so it is what the graph roots.
    graph.exportTexture(displayColor);
    graph.execute(commands, m_gpu.frameNumber());
}

} // namespace lmx::render
