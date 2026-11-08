//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureStage.cpp
/// @brief Implements exposure resources and pass declaration.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Exposure/ExposureStage.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Common/Dispatch.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/GraphResources.h"
#include "Render/Common/PacedSlots.h"
#include "Render/Passes/Exposure/ExposureFrameData.h"
#include "Render/Passes/Exposure/ExposureRoots.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Scalar.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Render/Renderer/Renderer.h"

#include <array>
#include <span>
#include <utility>

namespace lmx::render {
namespace {

constexpr uint32_t kHistogramBins = 256;
constexpr uint64_t kHistogramBufferSize = uint64_t{kHistogramBins} * sizeof(uint32_t);

//======================================================================================================================
void registerExposureLayoutsForCapture() {
    engine::registerUniformStruct(
        {"ExposureSeedRoot",
         sizeof(ExposureSeedRoot),
         {{"exposureAddr", offsetof(ExposureSeedRoot, exposureAddr), "Addr"},
          {"paramsAddr", offsetof(ExposureSeedRoot, paramsAddr), "Addr"}}});
    engine::registerUniformStruct(
        {"HistogramRoot",
         sizeof(HistogramRoot),
         {{"histogramAddr", offsetof(HistogramRoot, histogramAddr), "Addr"},
          {"exposureAddr", offsetof(HistogramRoot, exposureAddr), "Addr"},
          {"paramsAddr", offsetof(HistogramRoot, paramsAddr), "Addr"},
          {"sceneColor", offsetof(HistogramRoot, sceneColor), "uint"},
          {"padding", offsetof(HistogramRoot, padding), "uint"}}});
    engine::registerUniformStruct(
        {"ExposureResolveRoot",
         sizeof(ExposureResolveRoot),
         {{"histogramAddr", offsetof(ExposureResolveRoot, histogramAddr), "Addr"},
          {"exposureAddr", offsetof(ExposureResolveRoot, exposureAddr), "Addr"},
          {"paramsAddr", offsetof(ExposureResolveRoot, paramsAddr), "Addr"},
          {"padding0", offsetof(ExposureResolveRoot, padding0), "uint"},
          {"padding1", offsetof(ExposureResolveRoot, padding1), "uint"}}});
    engine::registerUniformStruct(
        {"HistogramParams",
         sizeof(HistogramParams),
         {{"logLuminanceMin", offsetof(HistogramParams, logLuminanceMin), "float"},
          {"logLuminanceMax", offsetof(HistogramParams, logLuminanceMax), "float"},
          {"width", offsetof(HistogramParams, width), "uint"},
          {"height", offsetof(HistogramParams, height), "uint"}}});
    engine::registerUniformStruct(
        {"ExposureSeedParams",
         sizeof(ExposureSeedParams),
         {{"exposure", offsetof(ExposureSeedParams, exposure), "float"}}});
    engine::registerUniformStruct(
        {"ExposureResolveParams",
         sizeof(ExposureResolveParams),
         {{"lowPercentile", offsetof(ExposureResolveParams, lowPercentile), "float"},
          {"highPercentile", offsetof(ExposureResolveParams, highPercentile), "float"},
          {"targetGrey", offsetof(ExposureResolveParams, targetGrey), "float"},
          {"evMin", offsetof(ExposureResolveParams, evMin), "float"},
          {"evMax", offsetof(ExposureResolveParams, evMax), "float"},
          {"compensationEv", offsetof(ExposureResolveParams, compensationEv), "float"},
          {"logLuminanceMin", offsetof(ExposureResolveParams, logLuminanceMin), "float"},
          {"logLuminanceMax", offsetof(ExposureResolveParams, logLuminanceMax), "float"},
          {"adaptUp", offsetof(ExposureResolveParams, adaptUp), "float"},
          {"adaptDown", offsetof(ExposureResolveParams, adaptDown), "float"},
          {"deltaSeconds", offsetof(ExposureResolveParams, deltaSeconds), "float"},
          {"pad", offsetof(ExposureResolveParams, pad), "float"}}});
}

} // namespace

//======================================================================================================================
lmx::Result<std::unique_ptr<ExposureStage>> ExposureStage::create(engine::GpuContext& gpu) {
    rojoRHI::Device& device = gpu.device();
    std::unique_ptr<ExposureStage> self(new ExposureStage);

    {
        auto library = gpu.loadShaderLibrary("HistogramAccumulate");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_histogramLibrary = std::move(*library);
    }
    {
        auto library = gpu.loadShaderLibrary("ExposureResolve");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_exposureResolveLibrary = std::move(*library);
    }
    {
        auto library = gpu.loadShaderLibrary("ExposureSeed");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_exposureSeedLibrary = std::move(*library);
    }

    {
        auto pipeline = engine::adoptGpu(
            device, device.createPipeline(rojoRHI::ComputePipelineDesc{
                        .compute = {self->m_histogramLibrary.get(), "computeHistogramAccumulate"},
                        .label = "lmx.render.histogramPipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_histogramPipeline = std::move(*pipeline);
    }
    {
        auto pipeline = engine::adoptGpu(
            device, device.createPipeline(rojoRHI::ComputePipelineDesc{
                        .compute = {self->m_exposureResolveLibrary.get(), "computeExposureResolve"},
                        .label = "lmx.render.exposureResolvePipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_exposureResolvePipeline = std::move(*pipeline);
    }
    {
        auto pipeline = engine::adoptGpu(
            device, device.createPipeline(rojoRHI::ComputePipelineDesc{
                        .compute = {self->m_exposureSeedLibrary.get(), "computeExposureSeed"},
                        .label = "lmx.render.exposureSeedPipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_exposureSeedPipeline = std::move(*pipeline);
    }

    {
        auto buffer = engine::GpuBuffer::create(device, {.size = kHistogramBufferSize,
                                                         .usage = rojoRHI::BufferUsage::ShaderWrite,
                                                         .label = "lmx.render.histogramBuffer"});
        if (!buffer) {
            return std::unexpected(buffer.error());
        }
        self->m_histogramBuffer = std::move(*buffer);
    }
    // The persistent {applied, previous} pair (spec 9's feedback buffer, widened by M6.2 spec 7):
    // the seed and resolve passes write it, and -- when auto-exposure is on -- the *next* frame's
    // scene and sky passes read index 0 directly as a storage buffer (declarePasses()'s
    // exposureCurrent/bufferReads below), never through a CPU readback. Unit exposure in both slots
    // is what a buffer no frame has seeded or resolved yet holds, which is the same answer EV 0
    // gives. Readbacks use separate staging, leaving this source GPU-resident for every caller.
    {
        constexpr std::array<float, kExposureBufferFloats> kInitialExposure{1.0f, 1.0f};
        {
            auto buffer = engine::GpuBuffer::create(device,
                                                    {.size = sizeof(kInitialExposure),
                                                     .usage = rojoRHI::BufferUsage::ShaderWrite,
                                                     .label = "lmx.render.exposureBuffer"},
                                                    std::as_bytes(std::span{kInitialExposure}));
            if (!buffer) {
                return std::unexpected(buffer.error());
            }
            self->m_exposureBuffer = std::move(*buffer);
        }
    }

    registerExposureLayoutsForCapture();
    return self;
}

//======================================================================================================================
GraphBuffer ExposureStage::declareSeed(RenderGraph& graph, rojoRHI::CommandList& commands,
                                       const SceneView& view, GraphBuffer exposureImport) {
    GraphBuffer exposureCurrent = exposureImport;
    // Auto mode seeds on spec 9's reset frames, where the seed is what restarts the metering loop.
    // Manual mode seeds on every temporal frame (M6.2 spec 7): the buffer is the single source of
    // the exposure the scene pass applies, so a manual EV edit has to land in it -- with the value
    // before the edit shifted into `previous` -- for a temporal resolve to correct the history it
    // is about to blend. A manual frame with temporal off declares nothing here, which is what
    // keeps the pre-temporal frame's declaration exact.
    const bool seedExposure = view.autoExposureEnabled ? view.exposureReset : view.temporal.enabled;
    if (seedExposure) {
        const ExposureSeedParams seedParams = buildExposureSeedParams(view.exposureEv);
        ComputePassDesc seedDesc;
        seedDesc.bufferWrites.push_back(exposureImport);
        graph.addComputePass(
            "lmx.pass.exposure.seed", std::move(seedDesc),
            [this, &commands, exposureImport, seedParams](const PassResources& resources) {
                const auto paramsAddress = frameData(commands, seedParams);
                engine::registerFrameRoot("ExposureSeedParams", paramsAddress);
                commands.setPipeline(m_exposureSeedPipeline.get());
                engine::registerFrameRoot("ExposureSeedRoot",
                                          commands.pushRoot(ExposureSeedRoot{
                                              .exposureAddr = resources.address(exposureImport),
                                              .paramsAddr = paramsAddress}));
#ifndef NDEBUG
                const rojoRHI::ResourceAccess access{.buffer = resources.buffer(exposureImport),
                                                     .size = kExposureBufferFloats * sizeof(float),
                                                     .stage = rojoRHI::Stage::ComputeShader,
                                                     .access = rojoRHI::Access::ShaderRead |
                                                               rojoRHI::Access::ShaderWrite};
                commands.declareAccess(std::span{&access, 1});
#endif
                commands.dispatch(1, 1, 1);
            });
        exposureCurrent = nextVersion(exposureImport);
        if (!view.autoExposureEnabled) {
            // Auto mode's resolve exports the end of the chain the seed starts, so the seed reaches
            // a sink. In manual mode nothing declared in this frame consumes what the seed wrote --
            // its consumer is the next frame -- so the version it produces is exported the way the
            // history commit's is, or dead-pass culling would drop the pass that records the pair.
            graph.exportBuffer(exposureCurrent);
        }
    }

    return exposureCurrent;
}

//======================================================================================================================
void ExposureStage::declareMetering(RenderGraph& graph, rojoRHI::CommandList& commands,
                                    const SceneView& view, const ExposureMeteringInputs& inputs) {
    const auto& extents = inputs.extents;
    const auto sceneColorRead = inputs.sceneColorRead;
    const auto exposureCurrent = inputs.exposureCurrent;
    // ---- Exposure feedback continued: histogram + resolve (spec 9). Declared every frame;
    // exported only when auto-exposure is on, so dead-pass culling drops the whole chain when it
    // is off. Both read `exposureCurrent` -- the exact version the scene/sky passes read above (or
    // its import version, harmlessly, when they did not) -- so the histogram's reconstruction of
    // "this frame's preExposure" agrees with what shading actually used, by construction rather
    // than by a CPU value threaded through both.
    // The previous frame's resolve dispatch is still potentially reading the histogram when this
    // frame clears it. Seeding the import with that terminal read makes the first live clear wait
    // on the real WAR edge; when auto exposure is off the chain is culled, so this costs nothing.
    const GraphBuffer histogramImport =
        graph.importBuffer(m_histogramBuffer, "lmx.render.histogramBuffer", BufferUse::StorageRead);

    declareZeroFill(graph, commands, "lmx.pass.exposure.clearHistogram", histogramImport,
                    kHistogramBufferSize);
    const GraphBuffer histogramCleared = nextVersion(histogramImport);

    // Metering reads the raw scene colour, which is a render-extent signal: the pass dispatches
    // over the active rectangle and states it, so no invocation reads the stale region outside it.
    // Metering is per texel, so the exposure it derives is the same at every scale.
    const uint32_t meterWidth = extents.renderWidth;
    const uint32_t meterHeight = extents.renderHeight;
    ComputePassDesc histogramDesc;
    histogramDesc.shaderTextureReads.push_back(sceneColorRead);
    histogramDesc.shaderBufferReads.push_back(exposureCurrent);
    histogramDesc.bufferWrites.push_back(histogramCleared);
    graph.addComputePass(
        "lmx.pass.exposure.histogram", std::move(histogramDesc),
        [this, &commands, sceneColorRead, histogramCleared, exposureCurrent, meterWidth,
         meterHeight](const PassResources& resources) {
            const HistogramParams params = buildHistogramParams(meterWidth, meterHeight);
            const auto paramsAddress = frameData(commands, params);
            engine::registerFrameRoot("HistogramParams", paramsAddress);
            commands.setPipeline(m_histogramPipeline.get());
            engine::registerFrameRoot(
                "HistogramRoot",
                commands.pushRoot(HistogramRoot{
                    .histogramAddr = resources.address(histogramCleared),
                    .exposureAddr = resources.address(exposureCurrent),
                    .paramsAddr = paramsAddress,
                    .sceneColor = static_cast<uint32_t>(resources.view(sceneColorRead))}));
#ifndef NDEBUG
            const std::array<rojoRHI::ResourceAccess, 3> accesses{
                {{.texture = resources.texture(sceneColorRead).texture,
                  .range = {0, 1, 0, 1},
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderRead},
                 {.buffer = resources.buffer(exposureCurrent),
                  .size = sizeof(float),
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderRead},
                 {.buffer = resources.buffer(histogramCleared),
                  .size = kHistogramBufferSize,
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderWrite}}};
            commands.declareAccess(accesses);
#endif
            const auto groups = dispatchGroups2D(meterWidth, meterHeight);
            commands.dispatch(groups[0], groups[1], 1);
        });
    const GraphBuffer histogramFinal = nextVersion(histogramCleared);

    ComputePassDesc resolveDesc;
    resolveDesc.bufferReads.push_back(histogramFinal);
    // The kernel reads the exposure it adapts from out of the same version it overwrites, and the
    // write alone is what states that: a declared write already orders this pass after every
    // earlier producer and consumer of that version, so naming the read as well would add a
    // declaration to the temporal-off frame's record without adding an edge to derive from it.
    resolveDesc.bufferWrites.push_back(exposureCurrent);
    graph.addComputePass(
        "lmx.pass.exposure.resolve", std::move(resolveDesc),
        [this, &commands, histogramFinal, exposureCurrent, view](const PassResources& resources) {
            const ExposureResolveParams params = buildExposureResolveParams(
                {.lowPercentile = view.exposureLowPercentile,
                 .highPercentile = view.exposureHighPercentile,
                 .targetGrey = view.exposureTargetGrey,
                 .evMin = view.exposureEvMin,
                 .evMax = view.exposureEvMax,
                 .compensationEv = view.exposureCompensationEv,
                 .adaptUpStopsPerSecond = view.exposureAdaptUpStopsPerSecond,
                 .adaptDownStopsPerSecond = view.exposureAdaptDownStopsPerSecond});
            const auto paramsAddress = frameData(commands, params);
            engine::registerFrameRoot("ExposureResolveParams", paramsAddress);
            commands.setPipeline(m_exposureResolvePipeline.get());
            engine::registerFrameRoot("ExposureResolveRoot",
                                      commands.pushRoot(ExposureResolveRoot{
                                          .histogramAddr = resources.address(histogramFinal),
                                          .exposureAddr = resources.address(exposureCurrent),
                                          .paramsAddr = paramsAddress}));
#ifndef NDEBUG
            const std::array<rojoRHI::ResourceAccess, 2> accesses{
                {{.buffer = resources.buffer(histogramFinal),
                  .size = kHistogramBufferSize,
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderRead},
                 {.buffer = resources.buffer(exposureCurrent),
                  .size = kExposureBufferFloats * sizeof(float),
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderRead | rojoRHI::Access::ShaderWrite}}};
            commands.declareAccess(accesses);
#endif
            commands.dispatch(1, 1, 1);
        });
    const GraphBuffer exposureResolved = nextVersion(exposureCurrent);
    if (view.autoExposureEnabled) {
        graph.exportBuffer(exposureResolved);
    }
}

} // namespace lmx::render
