//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolve.cpp
/// @brief Owns temporal history, initialization and reconstruction routing.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/TemporalResolve.h"
#include "Render/TemporalResolveInternal.h"

#include "Core/Diagnostics/Assert.h"
#include "Render/VendorTemporalScaler.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <utility>

namespace lmx::render {
using temporal_detail::kComputeThreadsPerGroup2D;
using temporal_detail::viewReadsRejection;
using temporal_detail::viewReadsReprojected;

namespace {

// kSceneColorFormat's and D32Float's texel sizes, which the reported footprints derive from.
constexpr uint64_t kColorBytesPerTexel = 8;
constexpr uint64_t kDepthBytesPerTexel = 4;

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

} // namespace

//======================================================================================================================
TemporalResolve::TemporalResolve(rojoRHI::Device& device, bool cpuReadback)
    : m_device(device), m_vendor(std::make_unique<VendorTemporalScaler>(device)),
      m_cpuReadback(cpuReadback) {}

//======================================================================================================================
TemporalResolve::~TemporalResolve() = default;

//======================================================================================================================
rojoRHI::Result<std::unique_ptr<TemporalResolve>> TemporalResolve::create(rojoRHI::Device& device,
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
                                           .colorFormat = rojoRHI::Format::BGRA8Unorm,
                                           .depthFormat = rojoRHI::Format::Unknown,
                                           .cullMode = rojoRHI::CullMode::None,
                                           .label = "lmx.render.temporalDebugViewPipeline"});
        pipeline) {
        self->m_debugViewPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    if (auto sampler = device.createSampler({.filter = rojoRHI::FilterMode::Linear,
                                             .addressMode = rojoRHI::AddressMode::Clamp,
                                             .label = "lmx.render.temporalSampler"});
        sampler) {
        self->m_sampler = std::move(*sampler);
    } else {
        return std::unexpected(sampler.error());
    }

    if (auto texture = device.createTexture({.width = 1,
                                             .height = 1,
                                             .format = rojoRHI::Format::RGBA16Float,
                                             .storageWrite = true,
                                             .label = "lmx.render.temporalDiagnosticFallback"});
        texture) {
        self->m_diagnosticFallback = std::move(*texture);
    } else {
        return std::unexpected(texture.error());
    }
    if (auto texture = device.createTexture({.width = 1,
                                             .height = 1,
                                             .format = rojoRHI::Format::RGBA16Float,
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
rojoRHI::Result<void> TemporalResolve::resize(uint32_t width, uint32_t height) {
    LMX_ASSERT(width > 0 && height > 0, "TemporalResolve::resize: the extent must be non-empty");

    std::unique_ptr<rojoRHI::Texture> depth[2];
    std::unique_ptr<rojoRHI::Texture> color[2];
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
                                                    .format = rojoRHI::Format::D32Float,
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
                                                    .format = rojoRHI::Format::RGBA16Float,
                                                    .renderTarget = true,
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
        m_colorUse[slot] = rojoRHI::TextureUse::CopyDestination;
        m_depthUse[slot] = rojoRHI::TextureUse::ShaderRead;
    }
    m_vendor->invalidateOutput();
    m_width = width;
    m_height = height;
    return {};
}

//======================================================================================================================
rojoRHI::Texture& TemporalResolve::depthSlot(uint32_t slot) {
    LMX_ASSERT(slot < 2 && m_depth[slot] != nullptr,
               "TemporalResolve::depthSlot: no such slot -- create() or resize() failed");
    return *m_depth[slot];
}

//======================================================================================================================
rojoRHI::Texture& TemporalResolve::colorSlot(uint32_t slot) {
    LMX_ASSERT(slot < 2 && m_color[slot] != nullptr,
               "TemporalResolve::colorSlot: no such slot -- create() or resize() failed");
    return *m_color[slot];
}

//======================================================================================================================
GraphTexture TemporalResolve::importDepth(RenderGraph& graph, uint32_t slot) {
    static constexpr const char* kNames[2] = {"lmx.render.sceneDepth0", "lmx.render.sceneDepth1"};
    return graph.importTexture(depthSlot(slot), rojoRHI::Format::D32Float, kNames[slot],
                               m_depthUse[slot]);
}

//======================================================================================================================
void TemporalResolve::recordDepthRead(uint32_t slot) {
    LMX_ASSERT(slot < 2, "recordDepthRead requires a valid history slot");
    m_depthUse[slot] = rojoRHI::TextureUse::ShaderRead;
}

//======================================================================================================================
rojoRHI::TextureUse TemporalResolve::depthUse(uint32_t slot) const {
    LMX_ASSERT(slot < 2, "depthUse requires a valid history slot");
    return m_depthUse[slot];
}

//======================================================================================================================
GraphTexture TemporalResolve::importColor(RenderGraph& graph, uint32_t slot) {
    static constexpr const char* kNames[2] = {"lmx.render.historyColor0",
                                              "lmx.render.historyColor1"};
    return graph.importTexture(colorSlot(slot), rojoRHI::Format::RGBA16Float, kNames[slot],
                               m_colorUse[slot]);
}

//======================================================================================================================
TemporalResolveOutputs TemporalResolve::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
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
    } else if (inputs.mode == ReconstructionMode::VendorTemporal) {
        LMX_ASSERT(!nativeOnlyTemporalView(debugView), "vendor mode cannot show native internals");
        if (debugView == TemporalDebugView::ReprojectedHistory) {
            outputs.reprojected = declareVendorHistory(graph, commands, inputs);
        }
        outputs.resolved = m_vendor->declare(graph, commands, inputs);
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
    m_vendor->recordMode(mode);
    m_depthUse[slot] = mode == ReconstructionMode::VendorTemporal
                           ? rojoRHI::TextureUse::ExternalRead
                           : rojoRHI::TextureUse::ShaderRead;
    if (mode == ReconstructionMode::NativeTaa ||
        (mode == ReconstructionMode::VendorTemporal &&
         debugView == TemporalDebugView::ReprojectedHistory)) {
        m_depthUse[other] = rojoRHI::TextureUse::ShaderRead;
    }
    // Under NativeTaa the resolve writes the slot and bloom and display then sample it, so the
    // frame's last access to it is a shader read. An upscaled Raw frame ends the same way: the
    // spatial commit is what produced the picture, so bloom and display sample the slot rather than
    // the smaller rectangle of scene colour behind it. Otherwise only the HistoryAge view samples
    // the committed slot; without it the commit copy is its last use, since display reads scene
    // colour.
    const bool readCurrent = mode == ReconstructionMode::NativeTaa || upscaled ||
                             debugView == TemporalDebugView::HistoryAge;
    m_colorUse[slot] =
        readCurrent ? rojoRHI::TextureUse::ShaderRead : rojoRHI::TextureUse::CopyDestination;
    if (mode == ReconstructionMode::VendorTemporal) {
        // Retain the opaque producer stage set across frames even though display samples it.
        m_colorUse[slot] = rojoRHI::TextureUse::ExternalWrite;
    }
    // The other slot is read by the resolve on every NativeTaa frame, and by the reprojection
    // diagnostic on a Raw frame only where that pass survived culling. A frame that read it
    // neither way leaves its record where the frame that wrote it put it.
    const bool readOther = mode == ReconstructionMode::NativeTaa ||
                           (historyValid && debugView == TemporalDebugView::ReprojectionError) ||
                           (mode == ReconstructionMode::VendorTemporal &&
                            debugView == TemporalDebugView::ReprojectedHistory);
    if (readOther) {
        m_colorUse[other] = rojoRHI::TextureUse::ShaderRead;
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

} // namespace lmx::render
