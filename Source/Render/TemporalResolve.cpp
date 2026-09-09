//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolve.cpp
/// @brief Implements the temporal reconstruction stage's targets, pipelines and pass declarations.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/TemporalResolve.h"

#include "Core/Assert.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <utility>

namespace lmx::render {
namespace {

// Mirrors Shaders/TemporalReproject.slang's TemporalReprojectParams.
struct TemporalReprojectParams {
    uint32_t width = 0; // The output extent: the pass's dispatch bound.
    uint32_t height = 0;
    uint32_t renderWidth = 0; // The active rectangle of the scene colour and the motion target.
    uint32_t renderHeight = 0;
    uint32_t allocatedWidth = 0; // The allocation of those two, which their UVs are taken over.
    uint32_t allocatedHeight = 0;
    glm::vec2 jitterOffset{0.0f};
};
static_assert(sizeof(TemporalReprojectParams) == 32,
              "must match TemporalReproject.slang's TemporalReprojectParams");

// Mirrors Shaders/SpatialUpscale.slang's SpatialUpscaleParams.
struct SpatialUpscaleParams {
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    uint32_t allocatedWidth = 0;
    uint32_t allocatedHeight = 0;
    glm::vec2 jitterOffset{0.0f};
};
static_assert(sizeof(SpatialUpscaleParams) == 32,
              "must match SpatialUpscale.slang's SpatialUpscaleParams");

// Mirrors Shaders/TemporalResolve.slang's TemporalResolveParams.
struct TemporalResolveParams {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t historyValid = 0;
    uint32_t writeDiagnostics = 0; // Bit 0: rejection; bit 1: reprojected history.
    glm::mat4 inverseViewProjection{1.0f};
    glm::mat4 previousViewProjection{1.0f};
    float previousNearZ = 0.0f;
    float pad[3] = {0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(TemporalResolveParams) == 160,
              "must match TemporalResolve.slang's TemporalResolveParams");

// Mirrors Shaders/TemporalUpscale.slang's TemporalUpscaleParams: TemporalResolveParams' fields, in
// its order and with its trailing pad, then the four the upscaling kernel appends. Appending is
// what keeps the two blocks comparable field for field.
struct TemporalUpscaleParams {
    uint32_t width = 0; // The active render extent, which the render-extent inputs are read within.
    uint32_t height = 0;
    uint32_t historyValid = 0;
    uint32_t writeDiagnostics = 0; // Bit 0: rejection; bit 1: reprojected history.
    glm::mat4 inverseViewProjection{1.0f};
    glm::mat4 previousViewProjection{1.0f};
    float previousNearZ = 0.0f;
    float pad[3] = {0.0f, 0.0f, 0.0f};
    uint32_t outputWidth = 0; // The dispatch bound: one invocation per output pixel.
    uint32_t outputHeight = 0;
    uint32_t allocatedWidth = 0; // The allocation every render-extent UV is taken over.
    uint32_t allocatedHeight = 0;
    uint32_t previousRenderWidth = 0; // The extent the previous depth slot was rendered at.
    uint32_t previousRenderHeight = 0;
    glm::vec2 jitterOffset{0.0f};
};
static_assert(sizeof(TemporalUpscaleParams) == 192,
              "must match TemporalUpscale.slang's TemporalUpscaleParams");

// Mirrors Shaders/TemporalDebugView.slang's TemporalDebugViewParams.
struct TemporalDebugViewParams {
    uint32_t view = 0;
    uint32_t renderWidth = 0; // The motion target's active rectangle; every other input is output.
    uint32_t renderHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
};
static_assert(sizeof(TemporalDebugViewParams) == 20,
              "must match TemporalDebugView.slang's TemporalDebugViewParams");

// TemporalReproject.slang's slot map.
constexpr uint32_t kReprojectHistorySlot = 0;    // texture
constexpr uint32_t kReprojectSceneColorSlot = 1; // texture
constexpr uint32_t kReprojectMotionSlot = 2;     // texture
constexpr uint32_t kReprojectDiagnosticSlot = 3; // storage texture
constexpr uint32_t kReprojectSamplerSlot = 0;    // sampler
constexpr uint32_t kReprojectParamsSlot = 0;     // buffer

// TemporalResolve.slang's slot map.
constexpr uint32_t kResolveSceneColorSlot = 0;    // texture
constexpr uint32_t kResolveDepthSlot = 1;         // texture
constexpr uint32_t kResolvePreviousDepthSlot = 2; // texture
constexpr uint32_t kResolveMotionSlot = 3;        // texture
constexpr uint32_t kResolveReactiveSlot = 4;      // texture
constexpr uint32_t kResolveHistorySlot = 5;       // texture
constexpr uint32_t kResolveOutputSlot = 6;        // storage texture
constexpr uint32_t kResolveRejectionSlot = 7;     // storage texture
constexpr uint32_t kResolveReprojectedSlot = 8;   // storage texture
constexpr uint32_t kResolveSamplerSlot = 0;       // sampler
constexpr uint32_t kResolveExposureSlot = 0;      // buffer
constexpr uint32_t kResolveParamsSlot = 1;        // buffer

// Shaders/TemporalUpscale.slang's slot map is the resolve's above, kResolve* for kResolve*, which
// is what lets one declaration serve both kernels.

// SpatialUpscale.slang's slot map.
constexpr uint32_t kUpscaleSceneColorSlot = 0; // texture
constexpr uint32_t kUpscaleOutputSlot = 1;     // storage texture
constexpr uint32_t kUpscaleSamplerSlot = 0;    // sampler
constexpr uint32_t kUpscaleParamsSlot = 0;     // buffer

// TemporalDebugView.slang's slot map, plus the view selectors its fragment branches on.
constexpr uint32_t kDebugViewMotionSlot = 0;      // texture
constexpr uint32_t kDebugViewDiagnosticSlot = 1;  // texture
constexpr uint32_t kDebugViewRejectionSlot = 2;   // texture
constexpr uint32_t kDebugViewReprojectedSlot = 3; // texture
constexpr uint32_t kDebugViewResolvedSlot = 4;    // texture
constexpr uint32_t kDebugViewParamsSlot = 0;      // buffer

constexpr uint32_t kComputeThreadsPerGroup2D = 8;

// kSceneColorFormat's and D32Float's texel sizes, which the reported footprints derive from.
constexpr uint64_t kColorBytesPerTexel = 8;
constexpr uint64_t kDepthBytesPerTexel = 4;

//======================================================================================================================
uint32_t divRoundUp(uint32_t value, uint32_t divisor) {
    return (value + divisor - 1) / divisor;
}

//======================================================================================================================
// The view selector Shaders/TemporalDebugView.slang branches on. Off never reaches the shader --
// the pass is not declared in that mode -- so it maps to the same value MotionVectors does rather
// than to a code the fragment has no branch for.
uint32_t debugViewSelector(TemporalDebugView view) {
    return view == TemporalDebugView::Off ? 1u : static_cast<uint32_t>(view);
}

//======================================================================================================================
// Which of the resolve's two diagnostics a view consumes. A view reading neither leaves both
// undeclared, which is what keeps the shipped frame from allocating or writing either.
bool viewReadsRejection(TemporalDebugView view) {
    return view == TemporalDebugView::RejectionMask || view == TemporalDebugView::BlendWeight;
}

//======================================================================================================================
bool viewReadsReprojected(TemporalDebugView view) {
    return view == TemporalDebugView::ReprojectedHistory;
}

//======================================================================================================================
// Whether the frame rasterised into a rectangle smaller than the image it presents. Upscaling is a
// property of the extents rather than of the mode, which is what lets both modes keep their meaning
// at every scale.
bool isUpscaled(const FrameExtents& extents) {
    return extents.renderWidth != extents.outputWidth ||
           extents.renderHeight != extents.outputHeight;
}

//======================================================================================================================
// Whether two frames rasterised and presented at the same pair of extents.
bool sameExtents(const FrameExtents& a, const FrameExtents& b) {
    return a.renderWidth == b.renderWidth && a.renderHeight == b.renderHeight &&
           a.outputWidth == b.outputWidth && a.outputHeight == b.outputHeight;
}

//======================================================================================================================
// Shaders/SpatialUpscale.slang's block. The allocated extent is the output one: every render-extent
// target is allocated at capacity and used through an origin-anchored active rectangle, so a UV
// over one of them is taken over the output extent whatever the frame rasterised at.
SpatialUpscaleParams spatialUpscaleParams(const TemporalInputs& inputs) {
    return SpatialUpscaleParams{.renderWidth = inputs.extents.renderWidth,
                                .renderHeight = inputs.extents.renderHeight,
                                .outputWidth = inputs.extents.outputWidth,
                                .outputHeight = inputs.extents.outputHeight,
                                .allocatedWidth = inputs.extents.outputWidth,
                                .allocatedHeight = inputs.extents.outputHeight,
                                .jitterOffset = jitterTexelOffset(inputs.camera.jitterPixels)};
}

//======================================================================================================================
// Shaders/TemporalUpscale.slang's block. The render extent is what the kernel reads its inputs
// within, the output extent what it dispatches over, and the allocated extent the output one on
// spatialUpscaleParams()' terms. The previous render extent is carried separately because the
// previous depth slot is addressed at the extent it was rendered at, which a scale change moves.
TemporalUpscaleParams temporalUpscaleParams(const TemporalInputs& inputs, bool historyValid,
                                            uint32_t writeDiagnostics) {
    return TemporalUpscaleParams{.width = inputs.extents.renderWidth,
                                 .height = inputs.extents.renderHeight,
                                 .historyValid = historyValid ? 1u : 0u,
                                 .writeDiagnostics = writeDiagnostics,
                                 .inverseViewProjection = inputs.camera.inverseViewProjection,
                                 .previousViewProjection = inputs.previousCamera.viewProjection,
                                 .previousNearZ = inputs.previousCamera.nearZ,
                                 .outputWidth = inputs.extents.outputWidth,
                                 .outputHeight = inputs.extents.outputHeight,
                                 .allocatedWidth = inputs.extents.outputWidth,
                                 .allocatedHeight = inputs.extents.outputHeight,
                                 .previousRenderWidth = inputs.previousExtents.renderWidth,
                                 .previousRenderHeight = inputs.previousExtents.renderHeight,
                                 .jitterOffset = jitterTexelOffset(inputs.camera.jitterPixels)};
}

//======================================================================================================================
// Shaders/TemporalReproject.slang's block: the pass runs over the output extent and resamples the
// render extent's active rectangle to meet it, on spatialUpscaleParams()' terms.
TemporalReprojectParams reprojectParams(const TemporalInputs& inputs) {
    const SpatialUpscaleParams resampling = spatialUpscaleParams(inputs);
    return TemporalReprojectParams{.width = resampling.outputWidth,
                                   .height = resampling.outputHeight,
                                   .renderWidth = resampling.renderWidth,
                                   .renderHeight = resampling.renderHeight,
                                   .allocatedWidth = resampling.allocatedWidth,
                                   .allocatedHeight = resampling.allocatedHeight,
                                   .jitterOffset = resampling.jitterOffset};
}

} // namespace

//======================================================================================================================
rhi::Result<std::unique_ptr<TemporalResolve>> TemporalResolve::create(rhi::Device& device,
                                                                      bool cpuReadback) {
    auto self = std::unique_ptr<TemporalResolve>(new TemporalResolve(device, cpuReadback));

    if (auto library = device.loadShaderLibrary("Shaders/TemporalReproject"); library) {
        self->m_reprojectLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/TemporalResolve"); library) {
        self->m_resolveLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/TemporalDebugView"); library) {
        self->m_debugViewLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/SpatialUpscale"); library) {
        self->m_spatialUpscaleLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/TemporalUpscale"); library) {
        self->m_temporalUpscaleLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }

    if (auto pipeline = device.createComputePipeline(
            {.library = self->m_reprojectLibrary.get(),
             .computeEntry = "computeTemporalReproject",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.temporalReprojectPipeline"});
        pipeline) {
        self->m_reprojectPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline = device.createComputePipeline(
            {.library = self->m_resolveLibrary.get(),
             .computeEntry = "computeTemporalResolve",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.temporalResolvePipeline"});
        pipeline) {
        self->m_resolvePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline = device.createComputePipeline(
            {.library = self->m_spatialUpscaleLibrary.get(),
             .computeEntry = "computeSpatialUpscale",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.spatialUpscalePipeline"});
        pipeline) {
        self->m_spatialUpscalePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline = device.createComputePipeline(
            {.library = self->m_temporalUpscaleLibrary.get(),
             .computeEntry = "computeTemporalUpscale",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.temporalUpscalePipeline"});
        pipeline) {
        self->m_temporalUpscalePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline =
            device.createGraphicsPipeline({.library = self->m_debugViewLibrary.get(),
                                           .vertexEntry = "vertexMain",
                                           .fragmentEntry = "fragmentMain",
                                           .colorFormat = rhi::Format::BGRA8Unorm,
                                           .depthFormat = rhi::Format::Unknown,
                                           .cullMode = rhi::CullMode::None,
                                           .label = "lmx.render.temporalDebugViewPipeline"});
        pipeline) {
        self->m_debugViewPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    if (auto sampler = device.createSampler({.filter = rhi::FilterMode::Linear,
                                             .addressMode = rhi::AddressMode::Clamp,
                                             .label = "lmx.render.temporalSampler"});
        sampler) {
        self->m_sampler = std::move(*sampler);
    } else {
        return std::unexpected(sampler.error());
    }

    if (auto texture = device.createTexture({.width = 1,
                                             .height = 1,
                                             .format = rhi::Format::RGBA16Float,
                                             .storageWrite = true,
                                             .label = "lmx.render.temporalDiagnosticFallback"});
        texture) {
        self->m_diagnosticFallback = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto texture = device.createTexture({.width = 1,
                                             .height = 1,
                                             .format = rhi::Format::RGBA16Float,
                                             .sampled = true,
                                             .label = "lmx.render.temporalViewFallback"});
        texture) {
        self->m_viewFallback = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    return self;
}

//======================================================================================================================
rhi::Result<void> TemporalResolve::resize(uint32_t width, uint32_t height) {
    LMX_ASSERT(width > 0 && height > 0, "TemporalResolve::resize: the extent must be non-empty");

    std::unique_ptr<rhi::Texture> depth[2];
    std::unique_ptr<rhi::Texture> color[2];
    static constexpr const char* kDepthLabels[2] = {"lmx.render.sceneDepth0",
                                                    "lmx.render.sceneDepth1"};
    static constexpr const char* kColorLabels[2] = {"lmx.render.historyColor0",
                                                    "lmx.render.historyColor1"};
    for (uint32_t slot = 0; slot < 2; ++slot) {
        // Sampled as well as rendered into: the resolve reconstructs a world position from this
        // frame's depth and compares it against the other slot's, and a caller may sample the
        // public depth target after the graph.
        auto depthTexture = m_device.createTexture({.width = width,
                                                    .height = height,
                                                    .format = rhi::Format::D32Float,
                                                    .renderTarget = true,
                                                    .sampled = true,
                                                    .label = kDepthLabels[slot]});
        if (!depthTexture) {
            return std::unexpected(depthTexture.error());
        }
        // Storage-write because the resolve writes its slot directly rather than through a copy;
        // sampled because the next frame reprojects it. cpuReadback is what lets a test compare an
        // accumulated frame against the raw one it was built from.
        auto colorTexture = m_device.createTexture({.width = width,
                                                    .height = height,
                                                    .format = rhi::Format::RGBA16Float,
                                                    .sampled = true,
                                                    .storageWrite = true,
                                                    .cpuReadback = m_cpuReadback,
                                                    .label = kColorLabels[slot]});
        if (!colorTexture) {
            return std::unexpected(colorTexture.error());
        }
        depth[slot] = std::move(*depthTexture);
        color[slot] = std::move(*colorTexture);
    }

    // Swap the targets only after every allocation succeeds.
    for (uint32_t slot = 0; slot < 2; ++slot) {
        m_depth[slot] = std::move(depth[slot]);
        m_color[slot] = std::move(color[slot]);
        // The contents went with the old textures, and the extent change is a history reset anyway.
        m_colorUse[slot] = rhi::TextureUse::CopyDestination;
    }
    m_width = width;
    m_height = height;
    return {};
}

//======================================================================================================================
rhi::Texture& TemporalResolve::depthSlot(uint32_t slot) {
    LMX_ASSERT(slot < 2 && m_depth[slot] != nullptr,
               "TemporalResolve::depthSlot: no such slot -- create() or resize() failed");
    return *m_depth[slot];
}

//======================================================================================================================
rhi::Texture& TemporalResolve::colorSlot(uint32_t slot) {
    LMX_ASSERT(slot < 2 && m_color[slot] != nullptr,
               "TemporalResolve::colorSlot: no such slot -- create() or resize() failed");
    return *m_color[slot];
}

//======================================================================================================================
GraphTexture TemporalResolve::importDepth(RenderGraph& graph, uint32_t slot) {
    static constexpr const char* kNames[2] = {"lmx.render.sceneDepth0", "lmx.render.sceneDepth1"};
    return graph.importTexture(depthSlot(slot), rhi::Format::D32Float, kNames[slot],
                               rhi::TextureUse::ShaderRead);
}

//======================================================================================================================
GraphTexture TemporalResolve::importColor(RenderGraph& graph, uint32_t slot) {
    static constexpr const char* kNames[2] = {"lmx.render.historyColor0",
                                              "lmx.render.historyColor1"};
    return graph.importTexture(colorSlot(slot), rhi::Format::RGBA16Float, kNames[slot],
                               m_colorUse[slot]);
}

//======================================================================================================================
TemporalResolveOutputs TemporalResolve::declare(RenderGraph& graph, rhi::CommandList& commands,
                                                const TemporalInputs& inputs,
                                                TemporalDebugView debugView,
                                                GraphTexture& displayResult) {
    const bool historyValid = inputs.resetReason == HistoryResetReason::None;

    // Declared wherever there is history to reproject; nothing but the ReprojectionError view
    // consumes the result, so any other frame culls the whole pass.
    GraphTexture diagnostic;
    if (historyValid) {
        diagnostic = declareReprojection(graph, commands, inputs);
    }

    const bool upscaled = isUpscaled(inputs.extents);
    // The native kernel addresses every one of its inputs at one extent, the previous depth slot
    // included, so it is correct only where this frame's render extent is its output extent *and*
    // the slot it tests disocclusion against was rendered at that same extent. A reset frame has
    // no previous slot to disagree with; any other frame following a differently sized one takes
    // the upscaling kernel for that one frame, which carries the previous render extent explicitly.
    const bool nativeKernel =
        !upscaled && (!historyValid || sameExtents(inputs.previousExtents, inputs.extents));
    TemporalResolveOutputs outputs;
    if (inputs.mode == ReconstructionMode::NativeTaa) {
        if (nativeKernel) {
            declareResolve(graph, commands, inputs, viewReadsRejection(debugView),
                           viewReadsReprojected(debugView), outputs);
        } else {
            declareUpscale(graph, commands, inputs, viewReadsRejection(debugView),
                           viewReadsReprojected(debugView), outputs);
        }
    } else {
        outputs.resolved = upscaled ? declareSpatialCommit(graph, commands, inputs)
                                    : declareHistoryCommit(graph, commands, inputs);
    }

    if (debugView != TemporalDebugView::Off) {
        const bool readsDiagnostic =
            debugView == TemporalDebugView::ReprojectionError && historyValid;
        displayResult = declareDebugView(graph, commands, debugView, inputs, outputs, diagnostic,
                                         readsDiagnostic, displayResult);
    }
    return outputs;
}

//======================================================================================================================
void TemporalResolve::recordFrame(uint32_t slot, ReconstructionMode mode,
                                  TemporalDebugView debugView, bool historyValid, bool upscaled) {
    LMX_ASSERT(slot < 2, "TemporalResolve::recordFrame: slot must be 0 or 1");
    const uint32_t other = 1 - slot;
    // Under NativeTaa the resolve writes the slot and bloom and display then sample it, so the
    // frame's last access to it is a shader read. An upscaled Raw frame ends the same way: the
    // spatial commit is what produced the picture, so bloom and display sample the slot rather than
    // the smaller rectangle of scene colour behind it. Otherwise only the HistoryAge view samples
    // the committed slot; without it the commit copy is its last use, since display reads scene
    // colour.
    const bool readCurrent = mode == ReconstructionMode::NativeTaa || upscaled ||
                             debugView == TemporalDebugView::HistoryAge;
    m_colorUse[slot] = readCurrent ? rhi::TextureUse::ShaderRead : rhi::TextureUse::CopyDestination;
    // The other slot is read by the resolve on every NativeTaa frame, and by the reprojection
    // diagnostic on a Raw frame only where that pass survived culling. A frame that read it
    // neither way leaves its record where the frame that wrote it put it.
    const bool readOther = mode == ReconstructionMode::NativeTaa ||
                           (historyValid && debugView == TemporalDebugView::ReprojectionError);
    if (readOther) {
        m_colorUse[other] = rhi::TextureUse::ShaderRead;
    }
}

//======================================================================================================================
uint64_t TemporalResolve::colorBytes() const {
    return 2 * uint64_t{m_width} * m_height * kColorBytesPerTexel;
}

//======================================================================================================================
uint64_t TemporalResolve::depthBytes() const {
    return 2 * uint64_t{m_width} * m_height * kDepthBytesPerTexel;
}

//======================================================================================================================
GraphTexture TemporalResolve::declareReprojection(RenderGraph& graph, rhi::CommandList& commands,
                                                  const TemporalInputs& inputs) {
    // The output extent, at every scale: the history and the display the view draws over are both
    // that size, and the current colour is resampled out of the active rectangle to meet them. It
    // also keeps the transient's descriptor independent of the render scale, so the transient pool
    // sees one footprint across every scale.
    const uint32_t width = inputs.extents.outputWidth;
    const uint32_t height = inputs.extents.outputHeight;
    const GraphTexture diagnostic = graph.createTexture({.width = width,
                                                         .height = height,
                                                         .format = rhi::Format::RGBA16Float,
                                                         .sampled = true,
                                                         .storageWrite = true},
                                                        "lmx.render.temporalDiagnostic");

    ComputePassDesc reprojectDesc;
    reprojectDesc.shaderTextureReads.push_back(inputs.history);
    reprojectDesc.shaderTextureReads.push_back(inputs.sceneColor);
    reprojectDesc.shaderTextureReads.push_back(inputs.motion);
    reprojectDesc.textureWrites.push_back(diagnostic);
    graph.addComputePass(
        "lmx.pass.temporal.reproject", std::move(reprojectDesc),
        [this, &commands, inputs, diagnostic,
         params = reprojectParams(inputs)](const PassResources& resources) {
            const GraphResult<rhi::Texture*> historyTexture = resources.texture(inputs.history);
            LMX_ASSERT(historyTexture.has_value(), historyTexture.error().message);
            const GraphResult<rhi::Texture*> sceneTexture = resources.texture(inputs.sceneColor);
            LMX_ASSERT(sceneTexture.has_value(), sceneTexture.error().message);
            const GraphResult<rhi::Texture*> motionTexture = resources.texture(inputs.motion);
            LMX_ASSERT(motionTexture.has_value(), motionTexture.error().message);
            const GraphResult<rhi::Texture*> target = resources.texture(diagnostic);
            LMX_ASSERT(target.has_value(), target.error().message);

            commands.bindComputePipeline(*m_reprojectPipeline);
            commands.bindTexture(kReprojectHistorySlot, **historyTexture);
            commands.bindTexture(kReprojectSceneColorSlot, **sceneTexture);
            commands.bindTexture(kReprojectMotionSlot, **motionTexture);
            commands.bindStorageTexture(kReprojectDiagnosticSlot, **target, {},
                                        rhi::StorageAccess::Write);
            commands.bindSampler(kReprojectSamplerSlot, *m_sampler);
            commands.bindFrameData(kReprojectParamsSlot, params);
            commands.dispatch(divRoundUp(params.width, kComputeThreadsPerGroup2D),
                              divRoundUp(params.height, kComputeThreadsPerGroup2D), 1);
        });
    return nextVersion(diagnostic);
}

//======================================================================================================================
void TemporalResolve::declareResolve(RenderGraph& graph, rhi::CommandList& commands,
                                     const TemporalInputs& inputs, bool rejectionWanted,
                                     bool reprojectedWanted, TemporalResolveOutputs& outputs) {
    // Only a frame whose render extent is its output extent reaches here, so the two are the same
    // number; naming the output one is what states which of them the pass and its diagnostics are
    // sized by.
    const uint32_t width = inputs.extents.outputWidth;
    const uint32_t height = inputs.extents.outputHeight;
    if (rejectionWanted) {
        outputs.rejection = graph.createTexture({.width = width,
                                                 .height = height,
                                                 .format = rhi::Format::RGBA8Unorm,
                                                 .sampled = true,
                                                 .storageWrite = true},
                                                "lmx.render.temporalRejection");
    }
    if (reprojectedWanted) {
        outputs.reprojected = graph.createTexture({.width = width,
                                                   .height = height,
                                                   .format = rhi::Format::RGBA16Float,
                                                   .sampled = true,
                                                   .storageWrite = true},
                                                  "lmx.render.temporalReprojected");
    }

    ComputePassDesc resolveDesc;
    resolveDesc.shaderTextureReads.push_back(inputs.sceneColor);
    resolveDesc.shaderTextureReads.push_back(inputs.depth);
    resolveDesc.shaderTextureReads.push_back(inputs.previousDepth);
    resolveDesc.shaderTextureReads.push_back(inputs.motion);
    resolveDesc.shaderTextureReads.push_back(inputs.reactive);
    // Declared on a reset frame too: the kernel is told through `historyValid` not to read it,
    // and declaring the same set either way is what keeps one pass shape for both.
    resolveDesc.shaderTextureReads.push_back(inputs.history);
    resolveDesc.bufferReads.push_back(inputs.exposure);
    resolveDesc.textureWrites.push_back(inputs.colorSlot);
    if (rejectionWanted) {
        resolveDesc.textureWrites.push_back(outputs.rejection);
    }
    if (reprojectedWanted) {
        resolveDesc.textureWrites.push_back(outputs.reprojected);
    }

    const bool historyValid = inputs.resetReason == HistoryResetReason::None;
    const TemporalResolveParams params{
        .width = width,
        .height = height,
        .historyValid = historyValid ? 1u : 0u,
        .writeDiagnostics = (rejectionWanted ? 1u : 0u) | (reprojectedWanted ? 2u : 0u),
        .inverseViewProjection = inputs.camera.inverseViewProjection,
        .previousViewProjection = inputs.previousCamera.viewProjection,
        .previousNearZ = inputs.previousCamera.nearZ};

    const GraphTexture output = inputs.colorSlot;
    const GraphTexture rejection = outputs.rejection;
    const GraphTexture reprojected = outputs.reprojected;
    graph.addComputePass(
        "lmx.pass.temporal.resolve", std::move(resolveDesc),
        [this, &commands, inputs, output, rejection, reprojected, rejectionWanted,
         reprojectedWanted, params, width, height](const PassResources& resources) {
            const auto bindRead = [&](uint32_t slot, GraphTexture handle) {
                const GraphResult<rhi::Texture*> texture = resources.texture(handle);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindTexture(slot, **texture);
            };
            bindRead(kResolveSceneColorSlot, inputs.sceneColor);
            bindRead(kResolveDepthSlot, inputs.depth);
            bindRead(kResolvePreviousDepthSlot, inputs.previousDepth);
            bindRead(kResolveMotionSlot, inputs.motion);
            bindRead(kResolveReactiveSlot, inputs.reactive);
            bindRead(kResolveHistorySlot, inputs.history);

            const GraphResult<rhi::Texture*> target = resources.texture(output);
            LMX_ASSERT(target.has_value(), target.error().message);
            const GraphResult<rhi::Buffer*> exposure = resources.buffer(inputs.exposure);
            LMX_ASSERT(exposure.has_value(), exposure.error().message);

            commands.bindComputePipeline(*m_resolvePipeline);
            commands.bindStorageTexture(kResolveOutputSlot, **target, {},
                                        rhi::StorageAccess::Write);
            // The argument table entry has to hold a writable texture even where the kernel's
            // corresponding writeDiagnostics bit makes it write nothing.
            if (rejectionWanted) {
                const GraphResult<rhi::Texture*> texture = resources.texture(rejection);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindStorageTexture(kResolveRejectionSlot, **texture, {},
                                            rhi::StorageAccess::Write);
            } else {
                commands.bindStorageTexture(kResolveRejectionSlot, *m_diagnosticFallback, {},
                                            rhi::StorageAccess::Write);
            }
            if (reprojectedWanted) {
                const GraphResult<rhi::Texture*> texture = resources.texture(reprojected);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindStorageTexture(kResolveReprojectedSlot, **texture, {},
                                            rhi::StorageAccess::Write);
            } else {
                commands.bindStorageTexture(kResolveReprojectedSlot, *m_diagnosticFallback, {},
                                            rhi::StorageAccess::Write);
            }
            commands.bindStorageBuffer(kResolveExposureSlot, **exposure, rhi::StorageAccess::Read);
            commands.bindSampler(kResolveSamplerSlot, *m_sampler);
            commands.bindFrameData(kResolveParamsSlot, params);
            commands.dispatch(divRoundUp(width, kComputeThreadsPerGroup2D),
                              divRoundUp(height, kComputeThreadsPerGroup2D), 1);
        });

    if (rejectionWanted) {
        outputs.rejection = nextVersion(outputs.rejection);
    }
    if (reprojectedWanted) {
        outputs.reprojected = nextVersion(outputs.reprojected);
    }
    // Nothing in this frame keeps the slot alive on its own account -- bloom and display read it,
    // but the accumulation's real consumer is the next frame -- so the version is exported the way
    // M6.1's commit was.
    outputs.resolved = nextVersion(inputs.colorSlot);
    graph.exportTexture(outputs.resolved);
}

//======================================================================================================================
GraphTexture TemporalResolve::declareHistoryCommit(RenderGraph& graph, rhi::CommandList& commands,
                                                   const TemporalInputs& inputs) {
    // Declared only where the two extents agree, so the copy covers the whole slot; the caller
    // routes an upscaled frame to declareSpatialCommit() instead.
    const uint32_t width = inputs.extents.renderWidth;
    const uint32_t height = inputs.extents.renderHeight;

    CopyPassDesc commitDesc;
    commitDesc.textureSources.push_back(inputs.sceneColor);
    commitDesc.textureDestinations.push_back(inputs.colorSlot);
    const GraphTexture sceneColor = inputs.sceneColor;
    const GraphTexture history = inputs.colorSlot;
    graph.addCopyPass(
        "lmx.pass.temporal.commitHistory", std::move(commitDesc),
        [&commands, sceneColor, history, width, height](const PassResources& resources) {
            const GraphResult<rhi::Texture*> sceneTexture = resources.texture(sceneColor);
            LMX_ASSERT(sceneTexture.has_value(), sceneTexture.error().message);
            const GraphResult<rhi::Texture*> historyTexture = resources.texture(history);
            LMX_ASSERT(historyTexture.has_value(), historyTexture.error().message);

            const rhi::TextureCopyRegion region{.width = width, .height = height};
            commands.copyTexture(**sceneTexture, region, **historyTexture, region);
        });
    // Nothing else in the frame consumes it -- the consumer is the next frame -- so the export is
    // what keeps the copy alive through culling.
    const GraphTexture committed = nextVersion(history);
    graph.exportTexture(committed);
    return committed;
}

//======================================================================================================================
GraphTexture TemporalResolve::declareSpatialCommit(RenderGraph& graph, rhi::CommandList& commands,
                                                   const TemporalInputs& inputs) {
    const SpatialUpscaleParams params = spatialUpscaleParams(inputs);

    ComputePassDesc commitDesc;
    commitDesc.shaderTextureReads.push_back(inputs.sceneColor);
    commitDesc.textureWrites.push_back(inputs.colorSlot);
    const GraphTexture sceneColor = inputs.sceneColor;
    const GraphTexture history = inputs.colorSlot;
    graph.addComputePass(
        "lmx.pass.temporal.commitUpscaled", std::move(commitDesc),
        [this, &commands, sceneColor, history, params](const PassResources& resources) {
            const GraphResult<rhi::Texture*> sceneTexture = resources.texture(sceneColor);
            LMX_ASSERT(sceneTexture.has_value(), sceneTexture.error().message);
            const GraphResult<rhi::Texture*> historyTexture = resources.texture(history);
            LMX_ASSERT(historyTexture.has_value(), historyTexture.error().message);

            commands.bindComputePipeline(*m_spatialUpscalePipeline);
            commands.bindTexture(kUpscaleSceneColorSlot, **sceneTexture);
            commands.bindStorageTexture(kUpscaleOutputSlot, **historyTexture, {},
                                        rhi::StorageAccess::Write);
            // The clamped sampler: a tap of the Catmull-Rom fetch that reaches the edge of the
            // active rectangle must answer with that edge rather than with the opposite one.
            commands.bindSampler(kUpscaleSamplerSlot, *m_sampler);
            commands.bindFrameData(kUpscaleParamsSlot, params);
            commands.dispatch(divRoundUp(params.outputWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(params.outputHeight, kComputeThreadsPerGroup2D), 1);
        });
    // On declareHistoryCommit()'s terms: the consumer is the next frame, so the export is what
    // keeps the pass alive through culling.
    const GraphTexture committed = nextVersion(history);
    graph.exportTexture(committed);
    return committed;
}

//======================================================================================================================
void TemporalResolve::declareUpscale(RenderGraph& graph, rhi::CommandList& commands,
                                     const TemporalInputs& inputs, bool rejectionWanted,
                                     bool reprojectedWanted, TemporalResolveOutputs& outputs) {
    // The output extent, at every scale: the history the kernel accumulates over and the picture
    // it produces are both that size, so the diagnostics beside them are too -- which also keeps
    // their descriptors independent of the render scale.
    const uint32_t width = inputs.extents.outputWidth;
    const uint32_t height = inputs.extents.outputHeight;
    if (rejectionWanted) {
        outputs.rejection = graph.createTexture({.width = width,
                                                 .height = height,
                                                 .format = rhi::Format::RGBA8Unorm,
                                                 .sampled = true,
                                                 .storageWrite = true},
                                                "lmx.render.temporalRejection");
    }
    if (reprojectedWanted) {
        outputs.reprojected = graph.createTexture({.width = width,
                                                   .height = height,
                                                   .format = rhi::Format::RGBA16Float,
                                                   .sampled = true,
                                                   .storageWrite = true},
                                                  "lmx.render.temporalReprojected");
    }

    // The resolve's declaration exactly: the accumulating kernel reads all six inputs and writes
    // the same three outputs, so one pass shape serves both extents.
    ComputePassDesc upscaleDesc;
    upscaleDesc.shaderTextureReads.push_back(inputs.sceneColor);
    upscaleDesc.shaderTextureReads.push_back(inputs.depth);
    upscaleDesc.shaderTextureReads.push_back(inputs.previousDepth);
    upscaleDesc.shaderTextureReads.push_back(inputs.motion);
    upscaleDesc.shaderTextureReads.push_back(inputs.reactive);
    // Declared on a reset frame too, on declareResolve()'s terms: the kernel is told through
    // `historyValid` not to read it.
    upscaleDesc.shaderTextureReads.push_back(inputs.history);
    upscaleDesc.bufferReads.push_back(inputs.exposure);
    upscaleDesc.textureWrites.push_back(inputs.colorSlot);
    if (rejectionWanted) {
        upscaleDesc.textureWrites.push_back(outputs.rejection);
    }
    if (reprojectedWanted) {
        upscaleDesc.textureWrites.push_back(outputs.reprojected);
    }

    const bool historyValid = inputs.resetReason == HistoryResetReason::None;
    const TemporalUpscaleParams params = temporalUpscaleParams(
        inputs, historyValid, (rejectionWanted ? 1u : 0u) | (reprojectedWanted ? 2u : 0u));

    const GraphTexture output = inputs.colorSlot;
    const GraphTexture rejection = outputs.rejection;
    const GraphTexture reprojected = outputs.reprojected;
    graph.addComputePass(
        "lmx.pass.temporal.upscale", std::move(upscaleDesc),
        [this, &commands, inputs, output, rejection, reprojected, rejectionWanted,
         reprojectedWanted, params](const PassResources& resources) {
            const auto bindRead = [&](uint32_t slot, GraphTexture handle) {
                const GraphResult<rhi::Texture*> texture = resources.texture(handle);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindTexture(slot, **texture);
            };
            bindRead(kResolveSceneColorSlot, inputs.sceneColor);
            bindRead(kResolveDepthSlot, inputs.depth);
            bindRead(kResolvePreviousDepthSlot, inputs.previousDepth);
            bindRead(kResolveMotionSlot, inputs.motion);
            bindRead(kResolveReactiveSlot, inputs.reactive);
            bindRead(kResolveHistorySlot, inputs.history);

            const GraphResult<rhi::Texture*> target = resources.texture(output);
            LMX_ASSERT(target.has_value(), target.error().message);
            const GraphResult<rhi::Buffer*> exposure = resources.buffer(inputs.exposure);
            LMX_ASSERT(exposure.has_value(), exposure.error().message);

            commands.bindComputePipeline(*m_temporalUpscalePipeline);
            commands.bindStorageTexture(kResolveOutputSlot, **target, {},
                                        rhi::StorageAccess::Write);
            // declareResolve()'s rule: the argument table entry has to hold a writable texture even
            // where the kernel's corresponding writeDiagnostics bit makes it write nothing.
            if (rejectionWanted) {
                const GraphResult<rhi::Texture*> texture = resources.texture(rejection);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindStorageTexture(kResolveRejectionSlot, **texture, {},
                                            rhi::StorageAccess::Write);
            } else {
                commands.bindStorageTexture(kResolveRejectionSlot, *m_diagnosticFallback, {},
                                            rhi::StorageAccess::Write);
            }
            if (reprojectedWanted) {
                const GraphResult<rhi::Texture*> texture = resources.texture(reprojected);
                LMX_ASSERT(texture.has_value(), texture.error().message);
                commands.bindStorageTexture(kResolveReprojectedSlot, **texture, {},
                                            rhi::StorageAccess::Write);
            } else {
                commands.bindStorageTexture(kResolveReprojectedSlot, *m_diagnosticFallback, {},
                                            rhi::StorageAccess::Write);
            }
            commands.bindStorageBuffer(kResolveExposureSlot, **exposure, rhi::StorageAccess::Read);
            // The clamped sampler: a tap of either Catmull-Rom fetch that reaches the edge of the
            // active rectangle must answer with that edge rather than with the opposite one.
            commands.bindSampler(kResolveSamplerSlot, *m_sampler);
            commands.bindFrameData(kResolveParamsSlot, params);
            commands.dispatch(divRoundUp(params.outputWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(params.outputHeight, kComputeThreadsPerGroup2D), 1);
        });

    if (rejectionWanted) {
        outputs.rejection = nextVersion(outputs.rejection);
    }
    if (reprojectedWanted) {
        outputs.reprojected = nextVersion(outputs.reprojected);
    }
    // declareResolve()'s export, for its reason: the accumulation's real consumer is the next
    // frame, so nothing in this frame keeps the slot alive on its own account.
    outputs.resolved = nextVersion(inputs.colorSlot);
    graph.exportTexture(outputs.resolved);
}

//======================================================================================================================
GraphTexture TemporalResolve::declareDebugView(RenderGraph& graph, rhi::CommandList& commands,
                                               TemporalDebugView debugView,
                                               const TemporalInputs& inputs,
                                               const TemporalResolveOutputs& outputs,
                                               GraphTexture diagnostic, bool readsDiagnostic,
                                               GraphTexture displayResult) {
    const bool nativeTaa = inputs.mode == ReconstructionMode::NativeTaa;
    const bool readsRejection = nativeTaa && viewReadsRejection(debugView);
    const bool readsReprojected = nativeTaa && viewReadsReprojected(debugView);
    // The age lives in the resolved colour's alpha, which under Raw is a copy of the frame's own
    // coverage rather than a count -- the view still reads it, and shows what the mode produced.
    const bool readsResolved = debugView == TemporalDebugView::HistoryAge;

    PassDesc debugDesc;
    debugDesc.textureReads.push_back(inputs.motion);
    if (readsDiagnostic) {
        debugDesc.textureReads.push_back(diagnostic);
    }
    if (readsRejection) {
        debugDesc.textureReads.push_back(outputs.rejection);
    }
    if (readsReprojected) {
        debugDesc.textureReads.push_back(outputs.reprojected);
    }
    if (readsResolved) {
        debugDesc.textureReads.push_back(outputs.resolved);
    }
    debugDesc.color = ColorAttachment{
        .handle = displayResult, .load = LoadOp::Clear, .store = StoreOp::Store, .clearColor = {}};
    const GraphTexture rejection = outputs.rejection;
    const GraphTexture reprojected = outputs.reprojected;
    const GraphTexture resolved = outputs.resolved;
    const GraphTexture motion = inputs.motion;
    const FrameExtents extents = inputs.extents;
    graph.addPass("lmx.pass.temporal.debugView", std::move(debugDesc),
                  [this, &commands, motion, diagnostic, rejection, reprojected, resolved,
                   readsDiagnostic, readsRejection, readsReprojected, readsResolved, debugView,
                   extents](const PassResources& resources) {
                      const GraphResult<rhi::Texture*> motionTexture = resources.texture(motion);
                      LMX_ASSERT(motionTexture.has_value(), motionTexture.error().message);

                      commands.bindPipeline(*m_debugViewPipeline);
                      commands.bindTexture(kDebugViewMotionSlot, **motionTexture);
                      // A view that reads none of these binds the 1x1 fallback: every texel the
                      // shader loads lies outside it, and an out-of-bounds Load answers with zeroes
                      // -- which is the shader's own "nothing to show" for each of them.
                      const auto bindOptional = [&](uint32_t slot, bool wanted,
                                                    GraphTexture handle) {
                          if (!wanted) {
                              commands.bindTexture(slot, *m_viewFallback);
                              return;
                          }
                          const GraphResult<rhi::Texture*> texture = resources.texture(handle);
                          LMX_ASSERT(texture.has_value(), texture.error().message);
                          commands.bindTexture(slot, **texture);
                      };
                      bindOptional(kDebugViewDiagnosticSlot, readsDiagnostic, diagnostic);
                      bindOptional(kDebugViewRejectionSlot, readsRejection, rejection);
                      bindOptional(kDebugViewReprojectedSlot, readsReprojected, reprojected);
                      bindOptional(kDebugViewResolvedSlot, readsResolved, resolved);

                      const TemporalDebugViewParams params{.view = debugViewSelector(debugView),
                                                           .renderWidth = extents.renderWidth,
                                                           .renderHeight = extents.renderHeight,
                                                           .outputWidth = extents.outputWidth,
                                                           .outputHeight = extents.outputHeight};
                      commands.bindFrameData(kDebugViewParamsSlot, params);
                      commands.draw(3);
                  });
    return nextVersion(displayResult);
}

} // namespace lmx::render
