//----------------------------------------------------------------------------------------------------------------------
/// @file Renderer.cpp
/// @brief Implements frame pass declaration and renderer-owned GPU resources.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Renderer/Renderer.h"
#include "Render/DisplayStage.h"
#include "Render/Passes/Bloom/BloomStage.h"
#include "Render/Passes/Exposure/ExposureStage.h"
#include "Render/Passes/Occlusion/OcclusionReference.h"
#include "Render/Passes/Temporal/VendorTemporalScaler.h"
#include "Render/Passes/Visibility/GpuVisibility.h"
#include <chrono>

#include "Core/Diagnostics/Assert.h"

#include <array>
#include <cmath>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lmx::render {

namespace {

constexpr uint32_t kShadowMapSize = 2048;

// Linear white is the neutral multiplier for every per-draw material factor.
constexpr std::array<uint8_t, 4> kWhiteTexel = {255, 255, 255, 255};
// Encoded tangent-space (0, 0, 1); bound to keep every declared slot valid.
constexpr std::array<uint8_t, 4> kFlatNormalTexel = {128, 128, 255, 255};
// Black zeroes both image-based terms when a scene carries no IBL set.
constexpr std::array<uint8_t, 4> kBlackTexel = {0, 0, 0, 255};
// The DFG fallback's (scale, bias), as RG16Float bits. Zero makes the specular reconstruction
// F0 * 0 + 0 vanish -- the matching answer for an environment that is itself black.
constexpr std::array<uint16_t, 2> kZeroDfgTexel = {0, 0};

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<rojoRHI::Texture>>
createFallbackTexture(rojoRHI::Device& device, const std::array<uint8_t, 4>& rgba,
                      rojoRHI::TextureKind kind, std::string_view label) {
    const rojoRHI::TextureMip mip{.data = rgba.data(), .bytesPerRow = 4};
    const uint32_t faceCount = kind == rojoRHI::TextureKind::Cube ? 6u : 1u;
    // Cube fallbacks must cover every face with the same neutral texel.
    const std::array<rojoRHI::TextureMip, 6> mips = {mip, mip, mip, mip, mip, mip};
    return device.createTexture({.width = 1,
                                 .height = 1,
                                 .format = rojoRHI::Format::RGBA8Unorm,
                                 .kind = kind,
                                 .sampled = true,
                                 .label = label},
                                std::span{mips.data(), faceCount});
}

//======================================================================================================================
// The DFG fallback needs its own creator: it is the one fallback that is neither RGBA8 nor a cube,
// because the split-sum table it stands in for is RG16Float and a shader reading it as anything
// else would find its two channels in the wrong place.
rojoRHI::Result<std::unique_ptr<rojoRHI::Texture>> createZeroDfgTexture(rojoRHI::Device& device) {
    const rojoRHI::TextureMip mip{.data = kZeroDfgTexel.data(),
                                  .bytesPerRow = sizeof(kZeroDfgTexel)};
    return device.createTexture({.width = 1,
                                 .height = 1,
                                 .format = rojoRHI::Format::RG16Float,
                                 .sampled = true,
                                 .label = "lmx.render.zeroDfgFallback"},
                                std::span{&mip, 1});
}

} // namespace

//======================================================================================================================
void registerUniformLayoutsForCapture() {
    SceneStage::registerSceneTableLayoutsForCapture();
    ShadowStage::registerUniformLayoutsForCapture();
    registerVisibilityLayoutsForCapture();
    LightClusterStage::registerLayoutsForCapture();
    SceneStage::registerPassLayoutsForCapture();
}

//======================================================================================================================
Renderer::Renderer(rojoRHI::Device& device, bool cpuReadback)
    : m_device(device), m_transientPool(device), m_exposureStage(std::make_unique<ExposureStage>()),
      m_bloomStage(std::make_unique<BloomStage>()),
      m_displayStage(std::make_unique<DisplayStage>()), m_drawSubmission(device),
      m_cpuReadback(cpuReadback) {}

//======================================================================================================================
Renderer::~Renderer() = default;

//======================================================================================================================
rojoRHI::Buffer& Renderer::exposureBuffer() {
    return m_exposureStage->buffer();
}

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<Renderer>> Renderer::create(rojoRHI::Device& device, uint32_t width,
                                                            uint32_t height, bool cpuReadback) {
    LMX_ASSERT(width > 0 && height > 0, "Renderer::create: width and height must be non-zero");

    // Registration is idempotent and keeps capture startup independent of Renderer state.
    registerUniformLayoutsForCapture();

    std::unique_ptr<Renderer> self(new Renderer(device, cpuReadback));

    if (auto stage = ShadowStage::create(device); stage) {
        self->m_shadowStage = std::move(*stage);
    } else {
        return std::unexpected(stage.error());
    }
    if (auto stage = SceneStage::create(device, kSceneColorFormat); stage) {
        self->m_sceneStage = std::move(*stage);
    } else {
        return std::unexpected(stage.error());
    }

    if (auto result = self->m_displayStage->loadLibraries(device); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = self->m_exposureStage->loadLibraries(device); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = self->m_bloomStage->loadLibraries(device); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = self->m_displayStage->createPipelines(device); !result) {
        return std::unexpected(result.error());
    }

    if (auto result = self->m_exposureStage->createPipelines(device); !result) {
        return std::unexpected(result.error());
    }

    if (auto result = self->m_bloomStage->createPipelines(device); !result) {
        return std::unexpected(result.error());
    }

    // The shadow map transitions from depth attachment to sampled texture each frame.
    if (auto shadowMap = device.createTexture({.width = kShadowMapSize,
                                               .height = kShadowMapSize,
                                               .format = rojoRHI::Format::D32Float,
                                               .renderTarget = true,
                                               .sampled = true,
                                               .label = "lmx.render.shadowMap"});
        shadowMap) {
        self->m_shadowMap = std::move(*shadowMap);
    } else {
        return std::unexpected(shadowMap.error());
    }

    if (auto texture = createFallbackTexture(device, kWhiteTexel, rojoRHI::TextureKind::Tex2D,
                                             "lmx.render.whiteFallback");
        texture) {
        self->m_whiteTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto texture = createFallbackTexture(device, kFlatNormalTexel, rojoRHI::TextureKind::Tex2D,
                                             "lmx.render.flatNormalFallback");
        texture) {
        self->m_flatNormalTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto texture = createFallbackTexture(device, kBlackTexel, rojoRHI::TextureKind::Cube,
                                             "lmx.render.blackCubeFallback");
        texture) {
        self->m_blackCubeTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto texture = createZeroDfgTexture(device); texture) {
        self->m_zeroDfgTexture = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto result = self->m_displayStage->createResources(device); !result) {
        return std::unexpected(result.error());
    }

    if (auto result = self->m_exposureStage->createResources(device, cpuReadback); !result) {
        return std::unexpected(result.error());
    }

    if (auto sampler = device.createSampler({.filter = rojoRHI::FilterMode::Linear,
                                             .addressMode = rojoRHI::AddressMode::Wrap,
                                             .maxAnisotropy = 16,
                                             .label = "lmx.render.linearSampler"});
        sampler) {
        self->m_linearSampler = std::move(*sampler);
    } else {
        return std::unexpected(sampler.error());
    }
    // GreaterEqual is the reversed-Z compare: the sampler answers "lit" where the receiver's own
    // depth is at least the stored one, because nearer to the light is now the larger number.
    // Clamp extends the 0.0 clear outside the fitted shadow footprint, and every receiver depth
    // clears that bar, so that region stays lit exactly as it did under the 1.0 clear before.
    if (auto sampler = device.createSampler({.filter = rojoRHI::FilterMode::Linear,
                                             .addressMode = rojoRHI::AddressMode::Clamp,
                                             .maxAnisotropy = 16,
                                             .compare = rojoRHI::CompareFunc::GreaterEqual,
                                             .label = "lmx.render.shadowSampler"});
        sampler) {
        self->m_shadowSampler = std::move(*sampler);
    } else {
        return std::unexpected(sampler.error());
    }

    // Clamped, not wrapped: the IBL set is read at the very edge of its domain -- the DFG table at
    // N.V = 1 and at roughness 1 -- where a wrapping sampler would fold the opposite edge's texels
    // into the result. Linear filtering carries the mip filter the prefiltered chain is sampled
    // across; no anisotropy, because neither lookup has a screen-space footprint to be anisotropic
    // about.
    if (auto sampler = self->m_device.createSampler({.filter = rojoRHI::FilterMode::Linear,
                                                     .addressMode = rojoRHI::AddressMode::Clamp,
                                                     .label = "lmx.render.iblSampler"});
        sampler) {
        self->m_iblSampler = std::move(*sampler);
    } else {
        return std::unexpected(sampler.error());
    }

    // The reconstruction stage owns both history pairs and the passes over them, so it is built
    // before the first resize(), which is what allocates its slots at this renderer's extent.
    if (auto stage = TemporalResolve::create(device, self->m_cpuReadback); stage) {
        self->m_temporalResolve = std::move(*stage);
    } else {
        return std::unexpected(stage.error());
    }

    if (auto targets = self->resize(width, height); !targets) {
        return std::unexpected(targets.error());
    }
    return self;
}

//======================================================================================================================
rojoRHI::Result<void> Renderer::resize(uint32_t width, uint32_t height) {
    LMX_ASSERT(width > 0 && height > 0, "Renderer::resize: width and height must be non-zero");

    // Scene-linear radiance: rendered into by the scene and sky passes, sampled by the display
    // pass. Readable on the same terms as the display target, because a caller that wants to
    // inspect radiance rather than the picture has nowhere else to read it from.
    auto hdrColor = m_device.createTexture({.width = width,
                                            .height = height,
                                            .format = kSceneColorFormat,
                                            .renderTarget = true,
                                            .sampled = true,
                                            .cpuReadback = m_cpuReadback,
                                            .label = "lmx.render.sceneColorHdr"});
    if (!hdrColor) {
        return std::unexpected(hdrColor.error());
    }
    // The display transform's output: what the viewport samples and a screenshot reads back.
    auto color = m_device.createTexture({.width = width,
                                         .height = height,
                                         .format = kDisplayFormat,
                                         .renderTarget = true,
                                         .sampled = true,
                                         .cpuReadback = m_cpuReadback,
                                         .label = "lmx.render.displayColor"});
    if (!color) {
        return std::unexpected(color.error());
    }
    // Swap the targets only after every allocation succeeds.
    m_hdrColor = std::move(*hdrColor);
    m_color = std::move(*color);
    m_width = width;
    m_height = height;

    // The temporal targets follow the scene targets' extent, under the same caller idle guarantee
    // this function already requires. The histories' contents are dropped with the old textures,
    // which the extent change makes a reset anyway (HistoryResetReason::ExtentChanged). Depth is
    // among them: it ping-pongs by temporal frame parity, so the stage owns both slots of it.
    if (auto targets = createTemporalTargets(); !targets) {
        return std::unexpected(targets.error());
    }
    if (auto slots = m_temporalResolve->resize(width, height); !slots) {
        return std::unexpected(slots.error());
    }
    if (m_hzbStage) {
        if (auto result = m_hzbStage->resize(width, height); !result)
            return std::unexpected(result.error());
    }
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> Renderer::createTemporalTargets() {
    LMX_ASSERT(m_width > 0 && m_height > 0,
               "Renderer::createTemporalTargets: the render extent must be non-empty");

    // Readable on the same terms as the scene targets: motion is the one output a GPU test has to
    // compare against an arithmetic oracle rather than against a picture.
    auto motion = m_device.createTexture({.width = m_width,
                                          .height = m_height,
                                          .format = kMotionFormat,
                                          .renderTarget = true,
                                          .sampled = true,
                                          .cpuReadback = m_cpuReadback,
                                          .label = "lmx.render.motion"});
    if (!motion) {
        return std::unexpected(motion.error());
    }
    // The per-pixel reactive weight: the scene pass's second extra attachment, read by the resolve
    // alone. One byte per pixel, and colour-renderable rather than storage, because a fragment
    // writes it as an ordinary attachment.
    auto reactive = m_device.createTexture({.width = m_width,
                                            .height = m_height,
                                            .format = kReactiveFormat,
                                            .renderTarget = true,
                                            .sampled = true,
                                            .cpuReadback = m_cpuReadback,
                                            .label = "lmx.render.reactive"});
    if (!reactive) {
        return std::unexpected(reactive.error());
    }

    m_motion = std::move(*motion);
    m_reactive = std::move(*reactive);
    return {};
}

//======================================================================================================================
GraphTexture Renderer::declarePasses(RenderGraph& graph, rojoRHI::CommandList& commands,
                                     const engine::Camera& camera, const SceneView& view) {
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
        temporalEnabled
            ? m_temporalResolve->importDepth(graph, slot)
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
    if (temporalEnabled) {
        motionTargetHandle =
            graph.importTexture(*m_motion, kMotionFormat, "lmx.render.motion", m_previousMotionUse);
        reactiveTargetHandle = graph.importTexture(*m_reactive, kReactiveFormat,
                                                   "lmx.render.reactive", m_previousReactiveUse);
        previousDepth = m_temporalResolve->importDepth(graph, previousSlot);
        colorSlotImport = m_temporalResolve->importColor(graph, slot);
        historyImport = m_temporalResolve->importColor(graph, previousSlot);
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

    const auto lightClusters =
        prepareLighting(graph, commands, view, lightsImport, extents, cameraState);
    const auto planes = extractFrustumPlanes(temporalEnabled ? cameraState.viewProjectionJittered
                                                             : cameraState.viewProjection);
    m_previousPyramid = prepareOcclusion(graph, camera, view, extents, cameraState);
    const auto drawBuffers = prepareVisibility(graph, commands, view, planes, sceneBuffers);
    const auto drawRows = drawBuffers[0];
    const auto drawArguments = drawBuffers[1];

    const GraphTexture shadowRead =
        m_shadowStage->declare(graph, commands, view,
                               {.draws = m_drawSubmission.shadow(),
                                .drawRows = drawRows,
                                .drawArguments = drawArguments,
                                .sceneBuffers = sceneBuffers,
                                .shadowMap = shadowMap,
                                .lightViewProj = shadow.viewProj,
                                .whiteTexture = m_whiteTexture.get(),
                                .linearSampler = m_linearSampler.get()});
    const GraphTexture sceneColorRead = m_sceneStage->declare(
        graph, commands, view,
        {.draws = m_drawSubmission.scene(),
         .drawRows = drawRows,
         .drawArguments = drawArguments,
         .sceneBuffers = sceneBuffers,
         .lights = lightsImport,
         .lightGrid = lightClusters.declared ? std::optional{lightClusters.grid} : std::nullopt,
         .lightIndices =
             lightClusters.declared ? std::optional{lightClusters.indices} : std::nullopt,
         .sceneColor = sceneColor,
         .sceneDepth = sceneDepth,
         .shadowRead = shadowRead,
         .motion = motionTargetHandle,
         .reactive = reactiveTargetHandle,
         .exposure = exposureCurrent,
         .camera = cameraState,
         .previousCamera = previousCamera,
         .eyePosition = camera.position,
         .shadowTransform = shadow.shadowTransform,
         .extents = extents,
         .jitterPixels = jitterPixels,
         .clearColor = {clearColor[0], clearColor[1], clearColor[2], clearColor[3]},
         .timeSeconds = timeSeconds,
         .temporalEnabled = temporalEnabled,
         .whiteTexture = m_whiteTexture.get(),
         .flatNormalTexture = m_flatNormalTexture.get(),
         .blackCubeTexture = m_blackCubeTexture.get(),
         .zeroDfgTexture = m_zeroDfgTexture.get(),
         .linearSampler = m_linearSampler.get(),
         .shadowSampler = m_shadowSampler.get(),
         .iblSampler = m_iblSampler.get()});
    const uint32_t sceneWidth = m_width;
    const uint32_t sceneHeight = m_height;
    const GraphTexture motionRead =
        temporalEnabled ? nextVersion(motionTargetHandle) : GraphTexture{};

    // ---- Temporal reconstruction (M6.2 spec 6). The stage declares the reprojection diagnostic,
    // this frame's accumulation or its raw commit, and the debug view; this frame routes what it
    // produced into bloom and display. The debug view draws over the version the display pass
    // produces, which is declared further down -- the graph's schedule is topological, so naming
    // that version here still orders the view behind its producer.
    GraphTexture displayResult = nextVersion(displayColor);
    TemporalResolveOutputs temporalOutputs;
    if (temporalEnabled) {
        TemporalInputs temporalInputs;
        temporalInputs.sceneColor = sceneColorRead;
        temporalInputs.depth = nextVersion(sceneDepth);
        temporalInputs.previousDepth = previousDepth;
        temporalInputs.motion = motionRead;
        temporalInputs.reactive = nextVersion(reactiveTargetHandle);
        temporalInputs.history = historyImport;
        temporalInputs.colorSlot = colorSlotImport;
        // The version the scene pass and the histogram read: after the seed, so "applied this
        // frame" means the same thing to shading, metering and the exposure correction alike.
        temporalInputs.exposure = exposureCurrent;
        temporalInputs.camera = cameraState;
        temporalInputs.previousCamera = previousCamera;
        temporalInputs.extents = extents;
        temporalInputs.previousExtents = previousExtents;
        temporalInputs.resetReason = resetReason;
        temporalInputs.mode = reconstruction;
        temporalOutputs =
            m_temporalResolve->declare(graph, commands, temporalInputs, debugView, displayResult);
    }

    // What bloom and the display transform read: the colour slot whenever the frame accumulated or
    // upscaled into it -- both leave the finished output-extent picture there -- and the raw
    // jittered frame otherwise. Only a Raw frame that rasterised at the output extent still reads
    // scene colour. The histogram deliberately keeps metering the raw scene colour, so metering
    // stays independent of the accumulation it corrects.
    const GraphTexture displayInput =
        nativeTaa || vendorTemporal || upscaled ? temporalOutputs.resolved : sceneColorRead;

    m_exposureStage->declareMetering(graph, commands, view, extents, sceneColorRead,
                                     exposureCurrent);

    const GraphTexture bloomResult = m_bloomStage->declare(
        graph, commands, displayInput, sceneWidth, sceneHeight, view.bloomThreshold);

    const bool lightDebugEnabled =
        view.lightDebugView != engine::LightDebugView::Off && lightClusters.declared;
    LMX_ASSERT(view.lightDebugView == engine::LightDebugView::Off ||
                   (view.localLightMode == engine::LocalLightMode::Clustered &&
                    debugView == TemporalDebugView::Off && view.hzbDebugLevel < 0),
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
                                        : displayColor;
    m_displayStage->declare(graph, commands, displayInput, bloomResult, displayDestination,
                            view.bloomEnabled, view.bloomIntensity);
    if (lightDebugEnabled) {
        if (!m_lightDebug) {
            auto stage = LightDebugStage::create(m_device);
            LMX_ASSERT(stage.has_value(), stage.error().message);
            m_lightDebug = std::move(*stage);
        }
        LightClusterParams clusterParams;
        clusterParams.rowCount = view.tables.lightRowCount;
        clusterParams.activeWidth = extents.renderWidth;
        clusterParams.activeHeight = extents.renderHeight;
        clusterParams.sliceDepth = clusterSliceDepths(camera.nearZ);
        displayResult = m_lightDebug->declare(
            graph, commands,
            {.mode = view.lightDebugView,
             .depth = nextVersion(sceneDepth),
             .display = nextVersion(displayDestination),
             .output = displayColor,
             .lights = *lightsImport,
             .grid = lightClusters.grid,
             .indices = lightClusters.indices,
             .clusters = clusterParams,
             .inverseViewProjection = glm::inverse(
                 temporalEnabled ? cameraState.viewProjectionJittered : cameraState.viewProjection),
             .outputWidth = m_width,
             .outputHeight = m_height});
    }

    // HZB follows every temporal depth consumer in the stable graph schedule.
    declareOcclusion(graph, commands, view, nextVersion(sceneDepth), displayResult);

    // The frame just declared becomes the previous one. A frame the caller abandoned before
    // declaring never reaches here, so it never becomes anyone's predecessor.
    ++m_declaredFrames;
    m_temporalStatus.lastReset = resetReason;
    if (resetReason != HistoryResetReason::None) {
        m_temporalStatus.lastResetFrame = m_declaredFrames;
    }
    m_temporalStatus.jitterIndex = m_temporalFrame % kJitterSequenceLength;
    m_temporalStatus.historyValid = historyValid;
    m_temporalStatus.historyBytes = m_temporalResolve->colorBytes();
    m_temporalStatus.depthHistoryBytes = m_temporalResolve->depthBytes();
    m_temporalStatus.reconstruction = reconstruction;
    m_temporalStatus.vendorFallback = selection.fallback;
    m_temporalStatus.vendorName = m_device.capabilities().temporalScaler.name;
    m_temporalStatus.vendorReset = vendorTemporal && m_temporalResolve->vendorReset();
    m_temporalStatus.vendorScalerGeneration = m_temporalResolve->vendorScalerGeneration();
    // The age counts declared temporal frames since the last non-None reason, whatever the mode:
    // both modes leave a real frame in the colour slot, so the count survives a mode switch. A
    // frame with temporal off starts it over, because the history the next temporal frame finds is
    // not the one this count would have described.
    if (!temporalEnabled) {
        m_temporalResolve->recordDisabledFrame();
        m_temporalStatus.historyAge = 0;
    } else if (resetReason != HistoryResetReason::None) {
        m_temporalStatus.historyAge = 1;
    } else {
        m_temporalStatus.historyAge = std::min<uint32_t>(m_temporalStatus.historyAge + 1, 65535);
    }
    m_temporalStatus.warmupComplete = m_temporalStatus.historyAge >= kTemporalWarmupFrames;
    m_temporalStatus.extents = extents;
    m_temporalStatus.renderScale = renderScale;
    m_temporalStatus.upscaled = upscaled;
    // A render-extent change under a reset reason says nothing: the history is being thrown away
    // anyway, and a frame with temporal off has no history to have survived anything -- it also
    // rasterises at the output extent whatever the scale field says, so a scale change straddling
    // it would otherwise be recorded twice. Under None with temporal on it is the whole point --
    // the history survived a change of the extent the scene rasterised at -- so that is the only
    // frame the count records.
    if (temporalEnabled && resetReason == HistoryResetReason::None && m_previousSignature &&
        (m_previousSignature->extents.renderWidth != extents.renderWidth ||
         m_previousSignature->extents.renderHeight != extents.renderHeight)) {
        m_temporalStatus.lastRenderExtentChangeFrame = m_declaredFrames;
    }
    m_previousSignature = signature;
    m_previousCamera = cameraState;
    // What this frame's last access to each persistent target was, for the next frame's imports to
    // state. The motion and reactive records survive frames with temporal off, which touch neither
    // target: overwriting them there would let a later re-enabling frame claim the last access was
    // its own attachment write, and its fragment-stage barrier would not drain the dispatch-stage
    // read the last temporal frame actually ended with.
    if (temporalEnabled) {
        // The resolve reads motion on every NativeTaa frame, and the debug view reads it whenever
        // one is shown.
        m_previousMotionUse = nativeTaa || vendorTemporal || debugView != TemporalDebugView::Off
                                  ? rojoRHI::TextureUse::ShaderRead
                                  : rojoRHI::TextureUse::RenderTarget;
        // Only the resolve reads the reactive attachment, so a Raw frame ends with its own write.
        m_previousReactiveUse = nativeTaa || vendorTemporal ? rojoRHI::TextureUse::ShaderRead
                                                            : rojoRHI::TextureUse::RenderTarget;
        m_temporalResolve->recordFrame(slot, reconstruction, debugView, historyValid, upscaled);
    }
    if (lightDebugEnabled)
        m_temporalResolve->recordDepthRead(temporalEnabled ? slot : 0);
    // The scene colour is written and read by every frame. Under Raw at the output extent the
    // commit copy is the last thing to touch it; an upscaled Raw frame samples it in the spatial
    // pass instead of copying it, and under NativeTaa and with temporal off, bloom, the histogram
    // and the display transform all read it and none copies out of it.
    m_previousSceneColorUse = vendorTemporal ? rojoRHI::TextureUse::ExternalRead
                              : temporalEnabled && !nativeTaa && !upscaled
                                  ? rojoRHI::TextureUse::CopySource
                                  : rojoRHI::TextureUse::ShaderRead;
    if (temporalEnabled) {
        ++m_temporalFrame;
    }
    return displayResult;
}

//======================================================================================================================
void Renderer::render(rojoRHI::CommandList& commands, const engine::Camera& camera,
                      const SceneView& view, bool barrierForSampling) {
    // declarePasses() always declares bloom's transients (spec 10: it is an ordinary graph pass
    // whether or not a caller's own graph has a pool), so this convenience path needs one of its
    // own -- the caller already opened this frame with Device::beginFrame() before calling here,
    // which is what m_transientPool.beginFrame() requires.
    m_transientPool.beginFrame();
    RenderGraph graph(m_transientPool);
    const GraphTexture displayColor = declarePasses(graph, commands, camera, view);
    // The display-transformed image is this frame's whole result, so it is what the graph roots.
    graph.exportTexture(displayColor);
    graph.execute(commands, m_device.frameNumber());

    if (barrierForSampling) {
        // For a sampling pass outside this graph: a hand-encoded pass declares nothing, so there
        // is no read for the graph to have derived the transition from.
        commands.textureBarrier(*m_color, rojoRHI::TextureUse::RenderTarget,
                                rojoRHI::TextureUse::ShaderRead);
    }
}

//======================================================================================================================
rojoRHI::Texture& Renderer::colorTarget() {
    LMX_ASSERT(m_color != nullptr, "Renderer::colorTarget: no color target -- create() failed");
    return *m_color;
}

//======================================================================================================================
rojoRHI::Texture& Renderer::hdrColorTarget() {
    LMX_ASSERT(m_hdrColor != nullptr,
               "Renderer::hdrColorTarget: no scene color target -- create() failed");
    return *m_hdrColor;
}

//======================================================================================================================
rojoRHI::Texture& Renderer::depthTarget() {
    LMX_ASSERT(m_temporalResolve != nullptr,
               "Renderer::depthTarget: no depth target -- create() failed");
    return m_temporalResolve->depthSlot(m_currentSlot);
}

//======================================================================================================================
rojoRHI::Texture* Renderer::historyTarget() {
    LMX_ASSERT(m_temporalResolve != nullptr,
               "Renderer::historyTarget: no history -- create() failed");
    return &m_temporalResolve->colorSlot(m_currentSlot);
}

} // namespace lmx::render
