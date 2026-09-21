//----------------------------------------------------------------------------------------------------------------------
/// @file RendererFrame.cpp
/// @brief Derives frame state and imports persistent renderer resources.
//----------------------------------------------------------------------------------------------------------------------

#include "Core/Diagnostics/Assert.h"
#include "Render/Passes/Exposure/ExposureStage.h"
#include "Render/Passes/Temporal/VendorTemporalScaler.h"
#include "Render/Renderer/RendererInternal.h"
#include <utility>

namespace lmx::render {
//======================================================================================================================
RendererFrameState Renderer::deriveFrameState(const engine::Camera& camera, const SceneView& view) {
    LMX_ASSERT(m_hdrColor && m_color && m_shadowMap && m_motion && m_reactive && m_temporalResolve,
               "Renderer::declarePasses: targets are missing -- create() failed");
    LMX_ASSERT(view.boundingSphere.w > 0.0f,
               "SceneView::boundingSphere needs a positive radius -- it is what the shadow "
               "frustum is fitted to");
    if (view.autoExposureEnabled) {
        LMX_ASSERT(view.exposureLowPercentile >= 0.0f &&
                       view.exposureLowPercentile < view.exposureHighPercentile &&
                       view.exposureHighPercentile <= 100.0f,
                   "SceneView exposure percentiles must satisfy 0 <= low < high <= 100");
        LMX_ASSERT(view.exposureTargetGrey > 0.0f, "SceneView exposureTargetGrey must be positive");
        LMX_ASSERT(view.exposureEvMin <= view.exposureEvMax,
                   "SceneView exposure EV minimum must not exceed its maximum");
        LMX_ASSERT(view.exposureAdaptUpStopsPerSecond >= 0.0f &&
                       view.exposureAdaptDownStopsPerSecond >= 0.0f,
                   "SceneView exposure adaptation rates must not be negative");
    }

    const bool temporalEnabled = view.temporal.enabled;
    // The render scale is meaningful only with temporal on, the way jitterEnabled already is: a
    // frame with temporal off rasterises at the output extent whatever the field says.
    LMX_ASSERT(!temporalEnabled || (view.temporal.renderScale >= kMinRenderScale &&
                                    view.temporal.renderScale <= kMaxRenderScale),
               "SceneView temporal renderScale must lie within [kMinRenderScale, kMaxRenderScale]");
    const ReconstructionSelection selection =
        temporalEnabled
            ? m_temporalResolve->prepare(view.temporal.reconstruction, m_width, m_height)
            : ReconstructionSelection{view.temporal.reconstruction, VendorFallback::None};
    const ReconstructionMode reconstruction = selection.mode;
    const bool vendorTemporal =
        temporalEnabled && reconstruction == ReconstructionMode::VendorTemporal;
    const float renderScale =
        temporalEnabled
            ? (vendorTemporal ? vendorRenderScale(view.temporal.renderScale,
                                                  m_device.capabilities().temporalScaler)
                              : view.temporal.renderScale)
            : 1.0f;

    // Both extents of the frame. Every render-extent target keeps its output-extent allocation and
    // is used through an origin-anchored active rectangle, so a scale change allocates nothing.
    const FrameExtents extents = renderExtentsForScale(m_width, m_height, renderScale);
    const bool upscaled = extents.renderWidth != m_width || extents.renderHeight != m_height;
    const FrameSignature signature{.sceneGeneration = view.temporal.sceneGeneration,
                                   .extents = extents,
                                   .fovY = camera.fovY,
                                   .nearZ = camera.nearZ,
                                   .temporalEnabled = temporalEnabled};
    const HistoryResetReason resetReason =
        deriveHistoryReset(m_previousSignature, signature, view.temporal.cameraCut);
    const bool historyValid = temporalEnabled && resetReason == HistoryResetReason::None;
    // The extents the previous declared frame ran at, which is what addresses the previous depth
    // slot. A reset frame reads no history at all, so it stands in its own extents rather than a
    // predecessor's that describes another image.
    const FrameExtents previousExtents =
        resetReason == HistoryResetReason::None && m_previousSignature
            ? m_previousSignature->extents
            : extents;
    // Ping-pong by declared temporal frame parity: this frame renders depth into `slot` and writes
    // its colour there, and reads the other slot as the previous frame's depth and history. A
    // frame with temporal off renders depth into slot 0 and touches no colour history at all.
    const uint32_t slot = temporalEnabled ? m_temporalFrame % 2 : 0;
    const uint32_t previousSlot = 1 - slot;
    m_currentSlot = slot;
    const bool nativeTaa = temporalEnabled && reconstruction == ReconstructionMode::NativeTaa;
    const TemporalDebugView debugView =
        vendorTemporal && nativeOnlyTemporalView(view.temporal.debugView) ? TemporalDebugView::Off
                                                                          : view.temporal.debugView;

    const glm::vec2 jitterPixels = temporalEnabled && view.temporal.jitterEnabled
                                       ? haltonJitterPixels(m_temporalFrame)
                                       : glm::vec2{0.0f};
    const CameraFrameState cameraState = buildCameraFrameState(camera, extents, jitterPixels);
    // A frame with no predecessor reprojects onto itself, which is the only honest answer: its
    // history is being reset anyway, so a fabricated previous camera would only invent motion.
    const CameraFrameState previousCamera = m_previousCamera.value_or(cameraState);

    const ShadowMatrices shadow = fitShadowOrtho(view.boundingSphere, view.lights[0].direction);

    return {.temporalEnabled = temporalEnabled,
            .selection = selection,
            .reconstruction = reconstruction,
            .vendorTemporal = vendorTemporal,
            .renderScale = renderScale,
            .extents = extents,
            .upscaled = upscaled,
            .signature = signature,
            .resetReason = resetReason,
            .historyValid = historyValid,
            .previousExtents = previousExtents,
            .slot = slot,
            .previousSlot = previousSlot,
            .nativeTaa = nativeTaa,
            .debugView = debugView,
            .jitterPixels = jitterPixels,
            .cameraState = cameraState,
            .previousCamera = previousCamera,
            .shadow = shadow};
}

//======================================================================================================================
RendererFrameImports Renderer::importFrameResources(RenderGraph& graph,
                                                    rojoRHI::CommandList& commands,
                                                    const SceneView& view,
                                                    const RendererFrameState& state) {
    // The graph snapshots formats at import for attachment validation; these named constants are
    // the same ones resize() and create() used to build the persistent targets.
    // These targets persist across frames while three command buffers may be in flight. Their
    // previous frame's terminal uses seed the fresh graph so its first attachment writes cannot
    // overlap those earlier reads/writes on Metal 4's queue. Depth and display use ShaderRead
    // conservatively because their public targets may be sampled by a caller after this graph;
    // that stage set also covers their ordinary fragment attachment work.
    const GraphTexture shadowMap =
        graph.importTexture(*m_shadowMap, rojoRHI::Format::D32Float, "lmx.render.shadowMap",
                            rojoRHI::TextureUse::ShaderRead);
    // The scene colour's terminal use is the display pass's read on an ordinary frame and the
    // history commit's copy on a temporal one, so it is what the previous frame left rather than a
    // constant.
    const GraphTexture sceneColor = graph.importTexture(
        *m_hdrColor, kSceneColorFormat, "lmx.render.sceneColorHdr", m_previousSceneColorUse);
    const GraphTexture displayColor = graph.importTexture(
        *m_color, kDisplayFormat, "lmx.render.displayColor", rojoRHI::TextureUse::ShaderRead);
    // A frame with temporal off imports slot 0 under the pre-temporal name and use, which is what
    // keeps its declaration exactly the one M6.1 made; a temporal frame names the slot it uses.
    const GraphTexture sceneDepth =
        state.temporalEnabled
            ? m_temporalResolve->importDepth(graph, state.slot)
            : graph.importTexture(m_temporalResolve->depthSlot(0), rojoRHI::Format::D32Float,
                                  "lmx.render.sceneDepth", m_temporalResolve->depthUse(0));

    // The temporal pair, imported only by a frame that declares the temporal path. Motion's
    // terminal use is its attachment write on a frame that shows no debug view and the view's own
    // read on one that does, so the import states what the previous frame recorded. History's is
    // the commit copy at the end of the previous temporal frame, which is what this frame's
    // reprojection reads across the frame boundary; the reactive attachment's is its own
    // attachment write on a Raw frame and the resolve's read on a NativeTaa one.
    GraphTexture motionTargetHandle;
    GraphTexture reactiveTargetHandle;
    GraphTexture previousDepth;
    GraphTexture colorSlotImport;
    GraphTexture historyImport;
    if (state.temporalEnabled) {
        motionTargetHandle =
            graph.importTexture(*m_motion, kMotionFormat, "lmx.render.motion", m_previousMotionUse);
        reactiveTargetHandle = graph.importTexture(*m_reactive, kReactiveFormat,
                                                   "lmx.render.reactive", m_previousReactiveUse);
        previousDepth = m_temporalResolve->importDepth(graph, state.previousSlot);
        colorSlotImport = m_temporalResolve->importColor(graph, state.slot);
        historyImport = m_temporalResolve->importColor(graph, state.previousSlot);
    }

    // Exposure feedback (spec 9): the persistent exposure buffer is imported here, before the
    // scene pass, because -- when auto-exposure is on -- the scene and sky passes read it
    // directly (a GPU-persistent value with a one-frame lag; never a CPU readback, which would
    // stall the three-frames-in-flight pipeline every auto-exposure frame). A reset frame (first
    // frame, scene switch, auto-exposure enable, resize) seeds it with the manual EV first, as an
    // ordinary compute dispatch -- not a stall -- so shading and the histogram/resolve chain below
    // agree on "this frame's preExposure" even on the frame the loop restarts.
    //
    // In auto mode it is imported as produced by an earlier frame's resolve dispatch, which is what
    // it holds: that pass wrote it with a storage write, this frame's scene and sky passes read it,
    // and the edge between them crosses a frame boundary where nothing else orders it -- the frames
    // in flight are paced against a frame two back, not against the one before. Stating the prior
    // producer is what lets this frame's derivation put a barrier in front of its first reader. A
    // manual temporal frame states the same thing for the same reason: its seed dispatch below
    // overwrites what the previous frame's seed wrote. Manual with temporal off imports it plainly:
    // no declared pass touches it, so there is no edge to state.
    const bool exposureWrittenByEarlierFrame = view.autoExposureEnabled || view.temporal.enabled;
    const GraphBuffer exposureImport =
        exposureWrittenByEarlierFrame
            ? graph.importBuffer(m_exposureStage->buffer(), "lmx.render.exposureBuffer",
                                 rojoRHI::BufferUse::StorageWrite)
            : graph.importBuffer(m_exposureStage->buffer(), "lmx.render.exposureBuffer");
    const GraphBuffer exposureCurrent =
        m_exposureStage->declareSeed(graph, commands, view, exposureImport);

    std::vector<GraphBuffer> sceneBuffers;
    if (view.tables.vertices) {
        LMX_ASSERT(view.tables.indices && view.tables.meshes && view.tables.instances &&
                       view.tables.materials,
                   "scene table bindings must be complete");
        sceneBuffers = {graph.importBuffer(*view.tables.vertices, "lmx.scene.vertices"),
                        graph.importBuffer(*view.tables.indices, "lmx.scene.indices"),
                        graph.importBuffer(*view.tables.meshes, "lmx.scene.meshes"),
                        graph.importBuffer(*view.tables.instances, "lmx.scene.instances"),
                        graph.importBuffer(*view.tables.materials, "lmx.scene.materials")};
    } else {
        LMX_ASSERT(view.items.empty() && !view.skySphere, "geometry needs scene table bindings");
    }

    // Row count is a high-water mark; only live lights justify importing the sixth table.
    std::optional<GraphBuffer> lightsImport;
    if (view.tables.liveLightCount > 0) {
        LMX_ASSERT(view.tables.lights != nullptr, "a live local light needs its paced light table");
        LMX_ASSERT(view.tables.lightRows.size() >= view.tables.lightRowCount,
                   "the borrowed light rows must cover every addressable row slot");
        lightsImport = graph.importBuffer(*view.tables.lights, "lmx.scene.lights");
    }

    return {.shadowMap = shadowMap,
            .sceneColor = sceneColor,
            .displayColor = displayColor,
            .sceneDepth = sceneDepth,
            .motionTargetHandle = motionTargetHandle,
            .reactiveTargetHandle = reactiveTargetHandle,
            .previousDepth = previousDepth,
            .colorSlotImport = colorSlotImport,
            .historyImport = historyImport,
            .exposureCurrent = exposureCurrent,
            .sceneBuffers = std::move(sceneBuffers),
            .lightsImport = lightsImport};
}

} // namespace lmx::render
