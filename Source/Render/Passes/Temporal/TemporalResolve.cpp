//----------------------------------------------------------------------------------------------------------------------
/// @file TemporalResolve.cpp
/// @brief Owns temporal history, initialization and reconstruction routing.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Temporal/TemporalResolve.h"
#include "Render/Common/Dispatch.h"
#include "Render/Common/Formats.h"
#include "Render/Common/StageSetup.h"
#include "Render/Passes/Temporal/TemporalResolveInternal.h"

#include "Core/Diagnostics/Assert.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Render/Passes/Temporal/VendorTemporalScaler.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

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

} // namespace

//======================================================================================================================
TemporalResolve::TemporalResolve(engine::GpuContext& gpu, bool cpuReadback)
    : m_device(gpu.device()), m_vendor(std::make_unique<VendorTemporalScaler>(gpu)),
      m_cpuReadback(cpuReadback) {}

//======================================================================================================================
TemporalResolve::~TemporalResolve() = default;

//======================================================================================================================
lmx::Result<std::unique_ptr<TemporalResolve>> TemporalResolve::create(engine::GpuContext& gpu,
                                                                      bool cpuReadback) {
    rojoRHI::Device& device = gpu.device();
    auto self = std::unique_ptr<TemporalResolve>(new TemporalResolve(gpu, cpuReadback));

    {
        auto library = gpu.loadShaderLibrary("TemporalReproject");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_reprojectLibrary = std::move(*library);
    }
    {
        auto library = gpu.loadShaderLibrary("TemporalResolve");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_resolveLibrary = std::move(*library);
    }
    {
        auto library = gpu.loadShaderLibrary("TemporalDebugView");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_debugViewLibrary = std::move(*library);
    }
    {
        auto library = gpu.loadShaderLibrary("SpatialUpscale");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_spatialUpscaleLibrary = std::move(*library);
    }
    {
        auto library = gpu.loadShaderLibrary("TemporalUpscale");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_temporalUpscaleLibrary = std::move(*library);
    }

    {
        auto pipeline = engine::adoptGpu(device.createComputePipeline(
            {.library = self->m_reprojectLibrary.get(),
             .computeEntry = "computeTemporalReproject",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.temporalReprojectPipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_reprojectPipeline = std::move(*pipeline);
    }
    {
        auto pipeline = engine::adoptGpu(device.createComputePipeline(
            {.library = self->m_resolveLibrary.get(),
             .computeEntry = "computeTemporalResolve",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.temporalResolvePipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_resolvePipeline = std::move(*pipeline);
    }
    {
        auto pipeline = engine::adoptGpu(device.createComputePipeline(
            {.library = self->m_spatialUpscaleLibrary.get(),
             .computeEntry = "computeSpatialUpscale",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.spatialUpscalePipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_spatialUpscalePipeline = std::move(*pipeline);
    }
    {
        auto pipeline = engine::adoptGpu(device.createComputePipeline(
            {.library = self->m_temporalUpscaleLibrary.get(),
             .computeEntry = "computeTemporalUpscale",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.temporalUpscalePipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_temporalUpscalePipeline = std::move(*pipeline);
    }
    {
        auto pipeline = engine::adoptGpu(device.createGraphicsPipeline(
            fullscreenPipelineDesc(self->m_debugViewLibrary.get(), "fragmentMain", kDisplayFormat,
                                   "lmx.render.temporalDebugViewPipeline")));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_debugViewPipeline = std::move(*pipeline);
    }

    {
        auto sampler =
            engine::adoptGpu(device.createSampler({.filter = rojoRHI::FilterMode::Linear,
                                                   .addressMode = rojoRHI::AddressMode::Clamp,
                                                   .label = "lmx.render.temporalSampler"}));
        if (!sampler) {
            return std::unexpected(sampler.error());
        }
        self->m_sampler = std::move(*sampler);
    }

    {
        auto texture =
            engine::GpuTexture::create(device, {.width = 1,
                                                .height = 1,
                                                .format = rojoRHI::Format::RGBA16Float,
                                                .storageWrite = true,
                                                .label = "lmx.render.temporalDiagnosticFallback"});
        if (!texture) {
            return std::unexpected(texture.error());
        }
        self->m_diagnosticFallback = std::move(*texture);
    }
    {
        auto texture =
            engine::GpuTexture::create(device, {.width = 1,
                                                .height = 1,
                                                .format = rojoRHI::Format::RGBA16Float,
                                                .sampled = true,
                                                .label = "lmx.render.temporalViewFallback"});
        if (!texture) {
            return std::unexpected(texture.error());
        }
        self->m_viewFallback = std::move(*texture);
    }
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
            engine::GpuTexture::create(m_device, {.width = width,
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
        auto colorTexture =
            engine::GpuTexture::create(m_device, {.width = width,
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
    }
    m_vendor->invalidateOutput();
    m_width = width;
    m_height = height;
    return {};
}

//======================================================================================================================
rojoRHI::Texture& TemporalResolve::depthSlot(uint32_t slot) {
    LMX_ASSERT(slot < 2 && m_depth[slot],
               "TemporalResolve::depthSlot: no such slot -- create() or resize() failed");
    return m_depth[slot].rhi();
}

//======================================================================================================================
rojoRHI::Texture& TemporalResolve::colorSlot(uint32_t slot) {
    LMX_ASSERT(slot < 2 && m_color[slot],
               "TemporalResolve::colorSlot: no such slot -- create() or resize() failed");
    return m_color[slot].rhi();
}

//======================================================================================================================
GraphTexture TemporalResolve::importDepth(RenderGraph& graph, uint32_t slot) {
    static constexpr const char* kNames[2] = {"lmx.render.sceneDepth0", "lmx.render.sceneDepth1"};
    return graph.importPersistentTexture(depthSlot(slot), rojoRHI::Format::D32Float, kNames[slot]);
}

//======================================================================================================================
GraphTexture TemporalResolve::importColor(RenderGraph& graph, uint32_t slot) {
    static constexpr const char* kNames[2] = {"lmx.render.historyColor0",
                                              "lmx.render.historyColor1"};
    return graph.importPersistentTexture(colorSlot(slot), rojoRHI::Format::RGBA16Float,
                                         kNames[slot]);
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
