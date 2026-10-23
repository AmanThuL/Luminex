//----------------------------------------------------------------------------------------------------------------------
/// @file RendererFrame.cpp
/// @brief Derives frame state and imports persistent renderer resources.
//----------------------------------------------------------------------------------------------------------------------

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/GpuContext.h"
#ifndef LMX_PENDING_POST
#include "Render/Passes/Exposure/ExposureStage.h"
#endif

#ifndef LMX_PENDING_TEMPORAL
#include "Render/Passes/Temporal/VendorTemporalScaler.h"
#endif
#include "Render/Renderer/RendererInternal.h"
#include <utility>

namespace lmx::render {
//======================================================================================================================
RendererFrameState Renderer::deriveFrameState(const engine::Camera& camera, const SceneView& view) {
#ifndef LMX_PENDING_SCENE
#ifndef LMX_PENDING_TEMPORAL
    LMX_ASSERT(m_hdrColor && m_color && m_shadowMap && m_motion && m_reactive && m_temporalResolve,
               "Renderer::declarePasses: targets are missing -- create() failed");
#else
    LMX_ASSERT(m_hdrColor && m_color && m_standIns.depth,
               "Renderer::declarePasses: targets are missing -- create() failed");
#endif
#else
    LMX_ASSERT(m_hdrColor && m_color && m_standIns.depth,
               "Renderer::declarePasses: targets are missing -- create() failed");
#endif

#ifndef LMX_PENDING_SCENE
    LMX_ASSERT(view.boundingSphere.w > 0.0f,
               "SceneView::boundingSphere needs a positive radius -- it is what the shadow "
               "frustum is fitted to");
#endif

#ifndef LMX_PENDING_POST
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
#endif

#ifndef LMX_PENDING_TEMPORAL
    const bool temporalEnabled = view.temporal.enabled;
    // The render scale is meaningful only with temporal on, the way jitterEnabled already is: a
    // frame with temporal off rasterises at the output extent whatever the field says.
    LMX_ASSERT(!temporalEnabled || (view.temporal.renderScale >= kMinRenderScale &&
                                    view.temporal.renderScale <= kMaxRenderScale),
               "SceneView temporal renderScale must lie within [kMinRenderScale, kMaxRenderScale]");
#else
    const bool temporalEnabled = false;
#endif

#ifndef LMX_PENDING_TEMPORAL
    const ReconstructionSelection selection =
        temporalEnabled
            ? m_temporalResolve->prepare(view.temporal.reconstruction, m_width, m_height)
            : ReconstructionSelection{view.temporal.reconstruction, VendorFallback::None};
    const ReconstructionMode reconstruction = selection.mode;
    const bool vendorTemporal =
        temporalEnabled && reconstruction == ReconstructionMode::VendorTemporal;
    const float renderScale = temporalEnabled
                                  ? (vendorTemporal ? vendorRenderScale(view.temporal.renderScale,
                                                                        m_gpu.upscalerSupport())
                                                    : view.temporal.renderScale)
                                  : 1.0f;
#else
    const ReconstructionMode reconstruction = ReconstructionMode::Raw;
    const bool vendorTemporal = false;
    const float renderScale = 1.0f;
#endif

    // Both extents of the frame. Every render-extent target keeps its output-extent allocation and
    // is used through an origin-anchored active rectangle, so a scale change allocates nothing.
    const FrameExtents extents = renderExtentsForScale(m_width, m_height, renderScale);
    const bool upscaled = extents.renderWidth != m_width || extents.renderHeight != m_height;
    const FrameSignature signature{.sceneGeneration = view.temporal.sceneGeneration,
                                   .extents = extents,
                                   .fovY = camera.fovY,
                                   .nearZ = camera.nearZ,
                                   .temporalEnabled = temporalEnabled};
#ifndef LMX_PENDING_TEMPORAL
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
#else
    const HistoryResetReason resetReason = HistoryResetReason::None;
    const bool historyValid = false;
    const FrameExtents previousExtents = extents;
    const uint32_t slot = 0;
    const uint32_t previousSlot = 0;
    const bool nativeTaa = false;
    const TemporalDebugView debugView = TemporalDebugView::Off;
    const glm::vec2 jitterPixels{0.0f};
    const CameraFrameState cameraState = buildCameraFrameState(camera, extents, jitterPixels);
    const CameraFrameState previousCamera = cameraState;
#endif

#ifndef LMX_PENDING_SCENE
    const ShadowMatrices shadow = fitShadowOrtho(
        view.boundingSphere, view.lights[view.shadowCaster >= 0 ? view.shadowCaster : 0].direction);
#endif

    return {.temporalEnabled = temporalEnabled,
#ifndef LMX_PENDING_TEMPORAL
            .selection = selection,
#endif
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
#ifndef LMX_PENDING_SCENE
            .shadow = shadow};
#else
    };
#endif
}

//======================================================================================================================
RendererFrameImports Renderer::importFrameResources(RenderGraph& graph,
                                                    rojoRHI::CommandList& commands,
                                                    const SceneView& view,
                                                    const RendererFrameState& state) {
    // The graph snapshots formats at import for attachment validation; these named constants are
    // the same ones resize() and create() used to build the persistent targets.
    // These targets persist across frames while three command buffers may be in flight, so each is
    // imported with the use the last frame to touch it left it in: that is what orders this frame's
    // first access behind those earlier reads and writes on Metal 4's queue. The graph writes the
    // uses back from the passes that ran; the few set by hand below are accesses it cannot see.
#ifndef LMX_PENDING_SCENE
    const GraphTexture shadowMap = graph.importPersistentTexture(
        m_shadowMap.rhi(), rojoRHI::Format::D32Float, "lmx.render.shadowMap");
#else
    const GraphTexture shadowMap;
#endif
    const GraphTexture sceneColor = graph.importPersistentTexture(
        m_hdrColor.rhi(), kSceneColorFormat, "lmx.render.sceneColorHdr");
    if (state.temporalEnabled && !state.nativeTaa && !state.vendorTemporal && !state.upscaled) {
        // The one override that states no access after the graph: it keeps the earlier
        // hand-tracked CopySource state solely so the gpu golden dump stays unchanged. That state
        // is likely under-synchronised: the derived last use is ShaderRead, because the commit copy
        // is scheduled before the bloom and display reads of the same version, so the next frame's
        // first barrier does not wait on those reads. Correcting it changes that golden, so it
        // stays a deliberate, open decision.
        graph.setFinalUse(sceneColor, TextureUse::CopySource);
    }
    const GraphTexture displayColor =
        graph.importPersistentTexture(m_color.rhi(), kDisplayFormat, "lmx.render.displayColor");
    // colorTarget() is sampled after the graph -- by the viewport, a screenshot, or a pass the
    // caller encodes by hand -- so the display target is left as a shader read whatever its last
    // pass was.
    graph.setFinalUse(displayColor, TextureUse::ShaderRead);
    // A frame with temporal off imports slot 0 under the pre-temporal name, which is what keeps its
    // declaration exactly the one M6.1 made; a temporal frame names the slot it uses.
#ifndef LMX_PENDING_TEMPORAL
    const GraphTexture sceneDepth =
        state.temporalEnabled
            ? m_temporalResolve->importDepth(graph, state.slot)
            : graph.importPersistentTexture(m_temporalResolve->depthTexture(0).rhi(),
                                            rojoRHI::Format::D32Float, "lmx.render.sceneDepth");
#else
    const GraphTexture sceneDepth = graph.importPersistentTexture(
        m_standIns.depth.rhi(), rojoRHI::Format::D32Float, "lmx.render.sceneDepth");
#endif
    // depthTexture() is public and a caller may sample it after this graph, so the frame's depth is
    // left as a shader read, a stage set that also covers the attachment work the scene pass does
    // with it. A vendor frame leaves it as the scaler's opaque read instead, which the graph
    // derives from that pass.
    if (!state.vendorTemporal) {
        graph.setFinalUse(sceneDepth, TextureUse::ShaderRead);
    }

    // The temporal pair, imported only by a frame that declares the temporal path. A frame with
    // temporal off touches neither target, so the uses the last temporal frame left survive it
    // and a re-enabling frame is still ordered behind that frame's reads.
    GraphTexture motionTargetHandle;
    GraphTexture reactiveTargetHandle;
    GraphTexture previousDepth;
    GraphTexture colorSlotImport;
    GraphTexture historyImport;
#ifndef LMX_PENDING_TEMPORAL
    if (state.temporalEnabled) {
        motionTargetHandle =
            graph.importPersistentTexture(m_motion.rhi(), kMotionFormat, "lmx.render.motion");
        reactiveTargetHandle =
            graph.importPersistentTexture(m_reactive.rhi(), kReactiveFormat, "lmx.render.reactive");
        previousDepth = m_temporalResolve->importDepth(graph, state.previousSlot);
        colorSlotImport = m_temporalResolve->importColor(graph, state.slot);
        historyImport = m_temporalResolve->importColor(graph, state.previousSlot);
        if (state.vendorTemporal) {
            // The scaler writes the slot opaquely and display then samples it; the slot keeps the
            // scaler's producer stage set across frames, the conservative state for the next
            // frame's first access.
            graph.setFinalUse(colorSlotImport, TextureUse::ExternalWrite);
        }
    }
#endif

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
#ifndef LMX_PENDING_POST
    const bool exposureWrittenByEarlierFrame = view.autoExposureEnabled || view.temporal.enabled;
    const GraphBuffer exposureImport =
        exposureWrittenByEarlierFrame
            ? graph.importBuffer(m_exposureStage->bufferOwner().rhi(), "lmx.render.exposureBuffer",
                                 BufferUse::StorageWrite)
            : graph.importBuffer(m_exposureStage->bufferOwner().rhi(), "lmx.render.exposureBuffer");
    const GraphBuffer exposureCurrent =
        m_exposureStage->declareSeed(graph, commands, view, exposureImport);
#else
    const GraphBuffer exposureCurrent =
        graph.importBuffer(m_standIns.exposure.rhi(), "lmx.render.exposureBuffer");
#endif

    std::vector<GraphBuffer> sceneBuffers;
#ifndef LMX_PENDING_SCENE
    if (view.tables.vertices) {
        LMX_ASSERT(view.tables.indices && view.tables.meshes && view.tables.instances &&
                       view.tables.materials,
                   "scene table bindings must be complete");
        sceneBuffers = {graph.importBuffer(view.tables.vertices->rhi(), "lmx.scene.vertices"),
                        graph.importBuffer(view.tables.indices->rhi(), "lmx.scene.indices"),
                        graph.importBuffer(view.tables.meshes->rhi(), "lmx.scene.meshes"),
                        graph.importBuffer(view.tables.instances->rhi(), "lmx.scene.instances"),
                        graph.importBuffer(view.tables.materials->rhi(), "lmx.scene.materials")};
    } else {
        LMX_ASSERT(view.items.empty() && !view.skySphere, "geometry needs scene table bindings");
    }
#endif

    // Row count is a high-water mark; only live lights justify importing the sixth table.
    std::optional<GraphBuffer> lightsImport;
#ifndef LMX_PENDING_LIGHTS
    if (view.tables.liveLightCount > 0) {
        LMX_ASSERT(view.tables.lights != nullptr, "a live local light needs its paced light table");
        LMX_ASSERT(view.tables.lightRows.size() >= view.tables.lightRowCount,
                   "the borrowed light rows must cover every addressable row slot");
        lightsImport = graph.importBuffer(view.tables.lights->rhi(), "lmx.scene.lights");
    }
#endif

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
