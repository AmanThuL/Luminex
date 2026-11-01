//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolve.cpp
/// @brief Owns temporal history, initialization and reconstruction routing.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Temporal/TemporalResolve.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Common/Formats.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/Temporal/TemporalResolveInternal.h"
#include "Render/Passes/Temporal/TemporalRoots.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Render/Passes/Temporal/VendorTemporalScaler.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace lmx::render {
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

//======================================================================================================================
// Names every temporal root and parameter record in capture sidecars, field by field.
void registerTemporalLayoutsForCapture() {
    using temporal_detail::SpatialUpscaleParams;
    using temporal_detail::TemporalDebugViewParams;
    using temporal_detail::TemporalReprojectParams;
    using temporal_detail::TemporalResolveParams;
    using temporal_detail::TemporalUpscaleParams;
    engine::registerUniformStruct(
        {"TemporalReprojectRoot",
         sizeof(TemporalReprojectRoot),
         {{"paramsAddr", offsetof(TemporalReprojectRoot, paramsAddr), "Addr"},
          {"history", offsetof(TemporalReprojectRoot, history), "uint"},
          {"sceneColor", offsetof(TemporalReprojectRoot, sceneColor), "uint"},
          {"motion", offsetof(TemporalReprojectRoot, motion), "uint"},
          {"diagnostic", offsetof(TemporalReprojectRoot, diagnostic), "uint"},
          {"sampler", offsetof(TemporalReprojectRoot, sampler), "uint"},
          {"padding0", offsetof(TemporalReprojectRoot, padding0), "uint"}}});
    engine::registerUniformStruct(
        {"TemporalResolveRoot",
         sizeof(TemporalResolveRoot),
         {{"paramsAddr", offsetof(TemporalResolveRoot, paramsAddr), "Addr"},
          {"exposureAddr", offsetof(TemporalResolveRoot, exposureAddr), "Addr"},
          {"sceneColor", offsetof(TemporalResolveRoot, sceneColor), "uint"},
          {"depth", offsetof(TemporalResolveRoot, depth), "uint"},
          {"previousDepth", offsetof(TemporalResolveRoot, previousDepth), "uint"},
          {"motion", offsetof(TemporalResolveRoot, motion), "uint"},
          {"reactive", offsetof(TemporalResolveRoot, reactive), "uint"},
          {"history", offsetof(TemporalResolveRoot, history), "uint"},
          {"output", offsetof(TemporalResolveRoot, output), "uint"},
          {"rejection", offsetof(TemporalResolveRoot, rejection), "uint"},
          {"reprojected", offsetof(TemporalResolveRoot, reprojected), "uint"},
          {"sampler", offsetof(TemporalResolveRoot, sampler), "uint"},
          {"padding0", offsetof(TemporalResolveRoot, padding0), "uint"},
          {"padding1", offsetof(TemporalResolveRoot, padding1), "uint"}}});
    engine::registerUniformStruct(
        {"SpatialUpscaleRoot",
         sizeof(SpatialUpscaleRoot),
         {{"paramsAddr", offsetof(SpatialUpscaleRoot, paramsAddr), "Addr"},
          {"sceneColor", offsetof(SpatialUpscaleRoot, sceneColor), "uint"},
          {"output", offsetof(SpatialUpscaleRoot, output), "uint"},
          {"sampler", offsetof(SpatialUpscaleRoot, sampler), "uint"},
          {"padding0", offsetof(SpatialUpscaleRoot, padding0), "uint"},
          {"padding1", offsetof(SpatialUpscaleRoot, padding1), "uint"},
          {"padding2", offsetof(SpatialUpscaleRoot, padding2), "uint"}}});
    engine::registerUniformStruct(
        {"TemporalDebugViewRoot",
         sizeof(TemporalDebugViewRoot),
         {{"paramsAddr", offsetof(TemporalDebugViewRoot, paramsAddr), "Addr"},
          {"motion", offsetof(TemporalDebugViewRoot, motion), "uint"},
          {"diagnostic", offsetof(TemporalDebugViewRoot, diagnostic), "uint"},
          {"rejection", offsetof(TemporalDebugViewRoot, rejection), "uint"},
          {"reprojected", offsetof(TemporalDebugViewRoot, reprojected), "uint"},
          {"resolved", offsetof(TemporalDebugViewRoot, resolved), "uint"},
          {"padding0", offsetof(TemporalDebugViewRoot, padding0), "uint"}}});
    engine::registerUniformStruct(
        {"TemporalResolveParams",
         sizeof(TemporalResolveParams),
         {{"width", offsetof(TemporalResolveParams, width), "uint"},
          {"height", offsetof(TemporalResolveParams, height), "uint"},
          {"historyValid", offsetof(TemporalResolveParams, historyValid), "uint"},
          {"writeDiagnostics", offsetof(TemporalResolveParams, writeDiagnostics), "uint"},
          {"inverseViewProjection", offsetof(TemporalResolveParams, inverseViewProjection),
           "float4x4"},
          {"previousViewProjection", offsetof(TemporalResolveParams, previousViewProjection),
           "float4x4"},
          {"previousNearZ", offsetof(TemporalResolveParams, previousNearZ), "float"}}});
    engine::registerUniformStruct(
        {"TemporalUpscaleParams",
         sizeof(TemporalUpscaleParams),
         {{"width", offsetof(TemporalUpscaleParams, width), "uint"},
          {"height", offsetof(TemporalUpscaleParams, height), "uint"},
          {"historyValid", offsetof(TemporalUpscaleParams, historyValid), "uint"},
          {"writeDiagnostics", offsetof(TemporalUpscaleParams, writeDiagnostics), "uint"},
          {"inverseViewProjection", offsetof(TemporalUpscaleParams, inverseViewProjection),
           "float4x4"},
          {"previousViewProjection", offsetof(TemporalUpscaleParams, previousViewProjection),
           "float4x4"},
          {"previousNearZ", offsetof(TemporalUpscaleParams, previousNearZ), "float"},
          {"outputWidth", offsetof(TemporalUpscaleParams, outputWidth), "uint"},
          {"outputHeight", offsetof(TemporalUpscaleParams, outputHeight), "uint"},
          {"allocatedWidth", offsetof(TemporalUpscaleParams, allocatedWidth), "uint"},
          {"allocatedHeight", offsetof(TemporalUpscaleParams, allocatedHeight), "uint"},
          {"previousRenderWidth", offsetof(TemporalUpscaleParams, previousRenderWidth), "uint"},
          {"previousRenderHeight", offsetof(TemporalUpscaleParams, previousRenderHeight), "uint"},
          {"jitterOffset", offsetof(TemporalUpscaleParams, jitterOffset), "float2"}}});
    engine::registerUniformStruct(
        {"SpatialUpscaleParams",
         sizeof(SpatialUpscaleParams),
         {{"renderWidth", offsetof(SpatialUpscaleParams, renderWidth), "uint"},
          {"renderHeight", offsetof(SpatialUpscaleParams, renderHeight), "uint"},
          {"outputWidth", offsetof(SpatialUpscaleParams, outputWidth), "uint"},
          {"outputHeight", offsetof(SpatialUpscaleParams, outputHeight), "uint"},
          {"allocatedWidth", offsetof(SpatialUpscaleParams, allocatedWidth), "uint"},
          {"allocatedHeight", offsetof(SpatialUpscaleParams, allocatedHeight), "uint"},
          {"jitterOffset", offsetof(SpatialUpscaleParams, jitterOffset), "float2"}}});
    engine::registerUniformStruct(
        {"TemporalReprojectParams",
         sizeof(TemporalReprojectParams),
         {{"width", offsetof(TemporalReprojectParams, width), "uint"},
          {"height", offsetof(TemporalReprojectParams, height), "uint"},
          {"renderWidth", offsetof(TemporalReprojectParams, renderWidth), "uint"},
          {"renderHeight", offsetof(TemporalReprojectParams, renderHeight), "uint"},
          {"allocatedWidth", offsetof(TemporalReprojectParams, allocatedWidth), "uint"},
          {"allocatedHeight", offsetof(TemporalReprojectParams, allocatedHeight), "uint"},
          {"jitterOffset", offsetof(TemporalReprojectParams, jitterOffset), "float2"}}});
    engine::registerUniformStruct(
        {"TemporalDebugViewParams",
         sizeof(TemporalDebugViewParams),
         {{"view", offsetof(TemporalDebugViewParams, view), "uint"},
          {"renderWidth", offsetof(TemporalDebugViewParams, renderWidth), "uint"},
          {"renderHeight", offsetof(TemporalDebugViewParams, renderHeight), "uint"},
          {"outputWidth", offsetof(TemporalDebugViewParams, outputWidth), "uint"},
          {"outputHeight", offsetof(TemporalDebugViewParams, outputHeight), "uint"}}});
}

} // namespace

//======================================================================================================================
TemporalResolve::TemporalResolve(engine::GpuContext& gpu)
    : m_device(gpu.device()), m_vendor(std::make_unique<VendorTemporalScaler>(gpu)) {}

//======================================================================================================================
TemporalResolve::~TemporalResolve() = default;

//======================================================================================================================
lmx::Result<std::unique_ptr<TemporalResolve>> TemporalResolve::create(engine::GpuContext& gpu,
                                                                      bool cpuReadback) {
    (void)cpuReadback;
    rojoRHI::Device& device = gpu.device();
    auto self = std::unique_ptr<TemporalResolve>(new TemporalResolve(gpu));

    struct ComputeKernel {
        const char* library;
        const char* entry;
        const char* label;
        engine::GpuOwned<rojoRHI::ShaderLibrary>* libraryOwner;
        engine::GpuOwned<rojoRHI::Pipeline>* pipelineOwner;
    };
    const std::array<ComputeKernel, 4> kernels{{
        {"TemporalReproject", "computeTemporalReproject", "lmx.render.temporalReprojectPipeline",
         &self->m_reprojectLibrary, &self->m_reprojectPipeline},
        {"TemporalResolve", "computeTemporalResolve", "lmx.render.temporalResolvePipeline",
         &self->m_resolveLibrary, &self->m_resolvePipeline},
        {"SpatialUpscale", "computeSpatialUpscale", "lmx.render.spatialUpscalePipeline",
         &self->m_spatialUpscaleLibrary, &self->m_spatialUpscalePipeline},
        {"TemporalUpscale", "computeTemporalUpscale", "lmx.render.temporalUpscalePipeline",
         &self->m_temporalUpscaleLibrary, &self->m_temporalUpscalePipeline},
    }};
    for (const ComputeKernel& kernel : kernels) {
        auto library = gpu.loadShaderLibrary(kernel.library);
        if (!library) {
            return std::unexpected(library.error());
        }
        *kernel.libraryOwner = std::move(*library);
        auto pipeline = engine::adoptGpu(
            device,
            device.createPipeline(rojoRHI::ComputePipelineDesc{
                .compute = {kernel.libraryOwner->get(), kernel.entry}, .label = kernel.label}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        *kernel.pipelineOwner = std::move(*pipeline);
    }
    {
        auto library = gpu.loadShaderLibrary("TemporalDebugView");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_debugViewLibrary = std::move(*library);
        auto pipeline =
            createFullscreenPipeline(device, self->m_debugViewLibrary.get(), "fragmentMain",
                                     kDisplayFormat, "lmx.render.temporalDebugViewPipeline");
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_debugViewPipeline = std::move(*pipeline);
    }

    {
        auto sampler = engine::adoptGpu(
            device, device.createSampler({.filter = rojoRHI::SamplerFilter::Linear,
                                          .address = rojoRHI::SamplerAddress::ClampToEdge,
                                          .label = "lmx.render.temporalSampler"}));
        if (!sampler) {
            return std::unexpected(sampler.error());
        }
        self->m_sampler = std::move(*sampler);
    }

    {
        auto texture =
            engine::GpuTexture::create(device,
                                       {.format = rojoRHI::Format::RGBA16Float,
                                        .extent = {1, 1, 1},
                                        .usage = rojoRHI::TextureUsage::Storage,
                                        .label = "lmx.render.temporalDiagnosticFallback"},
                                       {}, {.sampled = false, .storageMips = true});
        if (!texture) {
            return std::unexpected(texture.error());
        }
        self->m_diagnosticFallback = std::move(*texture);
    }
    {
        constexpr std::array<uint16_t, 4> kZeroHalf4 = {0, 0, 0, 0};
        auto texture =
            createTexel(device, rojoRHI::Format::RGBA16Float, rojoRHI::TextureType::Tex2D,
                        std::as_bytes(std::span{kZeroHalf4}), "lmx.render.temporalViewFallback");
        if (!texture) {
            return std::unexpected(texture.error());
        }
        self->m_viewFallback = std::move(*texture);
    }
    registerTemporalLayoutsForCapture();
    return self;
}

//======================================================================================================================
lmx::Result<void> TemporalResolve::resize(uint32_t width, uint32_t height) {
    LMX_ASSERT(width > 0 && height > 0, "TemporalResolve::resize: the extent must be non-empty");

    engine::GpuTexture depth[2];
    engine::GpuTexture color[2];
    static constexpr const char* kDepthLabels[2] = {"lmx.render.sceneDepth0",
                                                    "lmx.render.sceneDepth1"};
    static constexpr const char* kColorLabels[2] = {"lmx.render.historyColor0",
                                                    "lmx.render.historyColor1"};
    for (uint32_t slot = 0; slot < 2; ++slot) {
        // Sampled as well as rendered into: the resolve reconstructs a world position from this
        // frame's depth and compares it against the other slot's, and a caller may sample the
        // public depth target after the graph.
        auto depthTexture =
            engine::GpuTexture::create(m_device, {.format = rojoRHI::Format::D32Float,
                                                  .extent = {width, height, 1},
                                                  .usage = rojoRHI::TextureUsage::DepthStencil |
                                                           rojoRHI::TextureUsage::Sampled,
                                                  .label = kDepthLabels[slot]});
        if (!depthTexture) {
            return std::unexpected(depthTexture.error());
        }
        // Storage because the resolve writes its slot directly rather than through a copy; sampled
        // because the next frame reprojects it; a render target because the vendor upscaler may
        // need one for its output.
        auto colorTexture = engine::GpuTexture::create(
            m_device,
            {.format = rojoRHI::Format::RGBA16Float,
             .extent = {width, height, 1},
             .usage = rojoRHI::TextureUsage::RenderTarget | rojoRHI::TextureUsage::Sampled |
                      rojoRHI::TextureUsage::Storage,
             .label = kColorLabels[slot]},
            {}, {.sampled = true, .storageMips = true});
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
    }
    m_vendor->invalidateOutput();
    m_width = width;
    m_height = height;
    return {};
}

//======================================================================================================================
const engine::GpuTexture& TemporalResolve::depthTexture(uint32_t slot) const {
    LMX_ASSERT(slot < 2 && m_depth[slot],
               "TemporalResolve::depthSlot: no such slot -- create() or resize() failed");
    return m_depth[slot];
}

//======================================================================================================================
const engine::GpuTexture& TemporalResolve::colorTexture(uint32_t slot) const {
    LMX_ASSERT(slot < 2 && m_color[slot],
               "TemporalResolve::colorSlot: no such slot -- create() or resize() failed");
    return m_color[slot];
}

//======================================================================================================================
GraphTexture TemporalResolve::importDepth(RenderGraph& graph, uint32_t slot) {
    static constexpr const char* kNames[2] = {"lmx.render.sceneDepth0", "lmx.render.sceneDepth1"};
    return graph.importPersistentTexture(depthTexture(slot), kNames[slot]);
}

//======================================================================================================================
GraphTexture TemporalResolve::importColor(RenderGraph& graph, uint32_t slot) {
    static constexpr const char* kNames[2] = {"lmx.render.historyColor0",
                                              "lmx.render.historyColor1"};
    return graph.importPersistentTexture(colorTexture(slot), kNames[slot]);
}

//======================================================================================================================
TemporalResolveOutputs TemporalResolve::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                                const TemporalInputs& inputs,
                                                TemporalDebugView debugView,
                                                GraphTexture displayResult) {
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
    outputs.displayResult = displayResult;
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
        outputs.displayResult = declareDebugView(graph, commands, debugView, inputs, outputs,
                                                 diagnostic, readsDiagnostic, displayResult);
    }
    return outputs;
}

//======================================================================================================================
void TemporalResolve::recordFrame(ReconstructionMode mode) {
    m_vendor->recordMode(mode);
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
