//----------------------------------------------------------------------------------------------------------------------
/// @file ExposureStage.cpp
/// @brief Implements exposure resources and pass declaration.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Exposure/ExposureStage.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Scalar.h"
#include "Render/Renderer/Renderer.h"

#include <array>
#include <cmath>
#include <utility>

namespace lmx::render {
namespace {

// Mirrors Shaders/Passes/Exposure/HistogramAccumulate.slang's HistogramParams. Every field is a
// scalar, so HLSL cbuffer packing (which Slang's Metal path still follows) leaves them contiguous
// -- no vector field ever forces a gap here.
struct HistogramParams {
    float logLuminanceMin;
    float logLuminanceMax;
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(HistogramParams) == 16,
              "must match HistogramAccumulate.slang's HistogramParams");

// Mirrors Shaders/Passes/Exposure/ExposureSeed.slang's ExposureSeedParams.
struct ExposureSeedParams {
    float exposure;
};
static_assert(sizeof(ExposureSeedParams) == 4,
              "must match ExposureSeed.slang's ExposureSeedParams");

// Mirrors Shaders/Passes/Exposure/ExposureResolve.slang's ExposureResolveParams.
struct ExposureResolveParams {
    float lowPercentile;
    float highPercentile;
    float targetGrey;
    float evMin;
    float evMax;
    float compensationEv;
    float logLuminanceMin;
    float logLuminanceMax;
    float adaptUp;
    float adaptDown;
    float deltaSeconds;
    float pad;
};
static_assert(sizeof(ExposureResolveParams) == 48,
              "must match ExposureResolve.slang's ExposureResolveParams");

// HistogramAccumulate.slang's slot map.
constexpr uint32_t kHistogramSceneColorSlot = 0; // texture
constexpr uint32_t kHistogramBufferSlot = 0;     // buffer
constexpr uint32_t kHistogramExposureSlot = 1;   // buffer
constexpr uint32_t kHistogramParamsSlot = 2;     // buffer

// ExposureResolve.slang's slot map (buffer space only).
constexpr uint32_t kResolveHistogramSlot = 0;
constexpr uint32_t kResolveExposureSlot = 1;
constexpr uint32_t kResolveParamsSlot = 2;

// ExposureSeed.slang's slot map (buffer space only).
constexpr uint32_t kSeedExposureSlot = 0;
constexpr uint32_t kSeedParamsSlot = 1;

constexpr uint32_t kHistogramBins = 256;
constexpr uint64_t kHistogramBufferSize = uint64_t{kHistogramBins} * sizeof(uint32_t);
// Wide enough to cover everything from near-black shadow detail to a strongly overexposed
// highlight without the metering formula ever needing to know a scene's real range in advance;
// HistogramAccumulate.slang and ExposureResolve.slang must agree on both.
constexpr float kExposureLogLuminanceMin = -12.0f;
constexpr float kExposureLogLuminanceMax = 4.0f;
// The seconds one declared frame advances the scene by, which is what exposure adaptation steps
// against. The App's clock is a fixed step per frame rather than wall time -- asset::
// kAnimationBakeRate -- and this restates the number because Render cannot depend on Asset. A
// renderer that stepped by wall time would resolve a different exposure for the same frame on a
// different machine, which is not something a frozen stability tolerance can survive.
constexpr float kExposureFrameSeconds = 1.0f / 60.0f;

constexpr uint32_t kComputeThreadsPerGroup2D = 8;

} // namespace

//======================================================================================================================
rojoRHI::Result<void> ExposureStage::loadLibraries(rojoRHI::Device& device) {
    if (auto library = device.loadShaderLibrary("Shaders/HistogramAccumulate"); library) {
        m_histogramLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/ExposureResolve"); library) {
        m_exposureResolveLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/ExposureSeed"); library) {
        m_exposureSeedLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> ExposureStage::createPipelines(rojoRHI::Device& device) {
    if (auto pipeline = device.createComputePipeline(
            {.library = m_histogramLibrary.get(),
             .computeEntry = "computeHistogramAccumulate",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.histogramPipeline"});
        pipeline) {
        m_histogramPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline =
            device.createComputePipeline({.library = m_exposureResolveLibrary.get(),
                                          .computeEntry = "computeExposureResolve",
                                          .threadsPerThreadgroup = {1, 1, 1},
                                          .label = "lmx.render.exposureResolvePipeline"});
        pipeline) {
        m_exposureResolvePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline = device.createComputePipeline({.library = m_exposureSeedLibrary.get(),
                                                      .computeEntry = "computeExposureSeed",
                                                      .threadsPerThreadgroup = {1, 1, 1},
                                                      .label = "lmx.render.exposureSeedPipeline"});
        pipeline) {
        m_exposureSeedPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> ExposureStage::createResources(rojoRHI::Device& device, bool cpuReadback) {
    if (auto buffer = device.createBuffer({.size = kHistogramBufferSize,
                                           .storageRead = true,
                                           .storageWrite = true,
                                           .label = "lmx.render.histogramBuffer"},
                                          nullptr);
        buffer) {
        m_histogramBuffer = std::move(*buffer);
    } else {
        return std::unexpected(buffer.error());
    }
    // The persistent {applied, previous} pair (spec 9's feedback buffer, widened by M6.2 spec 7):
    // the seed and resolve passes write it, and -- when auto-exposure is on -- the *next* frame's
    // scene and sky passes read index 0 directly as a storage buffer (declarePasses()'s
    // exposureCurrent/bufferReads below), never through a CPU readback. Unit exposure in both slots
    // is what a buffer no frame has seeded or resolved yet holds, which is the same answer EV 0
    // gives. cpuReadback only where the caller asked for it: the App never reads this back -- that
    // is the whole point of keeping the feedback GPU-resident -- and the tests do.
    {
        constexpr std::array<float, kExposureBufferFloats> kInitialExposure{1.0f, 1.0f};
        if (auto buffer = device.createBuffer({.size = sizeof(kInitialExposure),
                                               .storageRead = true,
                                               .storageWrite = true,
                                               .cpuReadback = cpuReadback,
                                               .label = "lmx.render.exposureBuffer"},
                                              kInitialExposure.data());
            buffer) {
            m_exposureBuffer = std::move(*buffer);
        } else {
            return std::unexpected(buffer.error());
        }
    }

    return {};
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
        const float manualExposure = std::exp2(view.exposureEv);
        ComputePassDesc seedDesc;
        seedDesc.bufferWrites.push_back(exposureImport);
        graph.addComputePass(
            "lmx.pass.exposure.seed", std::move(seedDesc),
            [this, &commands, exposureImport, manualExposure](const PassResources& resources) {
                const GraphResult<rojoRHI::Buffer*> exposure = resources.buffer(exposureImport);
                LMX_ASSERT(exposure.has_value(), exposure.error().message);
                const ExposureSeedParams params{.exposure = manualExposure};
                commands.bindComputePipeline(*m_exposureSeedPipeline);
                // Read-write, not write: the kernel shifts index 0 into index 1 before it sets it.
                commands.bindStorageBuffer(kSeedExposureSlot, **exposure,
                                           rojoRHI::StorageAccess::ReadWrite);
                commands.bindFrameData(kSeedParamsSlot, params);
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
                                    const SceneView& view, const FrameExtents& extents,
                                    GraphTexture sceneColorRead, GraphBuffer exposureCurrent) {
    // ---- Exposure feedback continued: histogram + resolve (spec 9). Declared every frame;
    // exported only when auto-exposure is on, so dead-pass culling drops the whole chain when it
    // is off. Both read `exposureCurrent` -- the exact version the scene/sky passes read above (or
    // its import version, harmlessly, when they did not) -- so the histogram's reconstruction of
    // "this frame's preExposure" agrees with what shading actually used, by construction rather
    // than by a CPU value threaded through both.
    // The previous frame's resolve dispatch is still potentially reading the histogram when this
    // frame clears it. Seeding the import with that terminal read makes the first live clear wait
    // on the real WAR edge; when auto exposure is off the chain is culled, so this costs nothing.
    const GraphBuffer histogramImport = graph.importBuffer(
        *m_histogramBuffer, "lmx.render.histogramBuffer", rojoRHI::BufferUse::StorageRead);

    CopyPassDesc histogramClearDesc;
    histogramClearDesc.bufferDestinations.push_back(histogramImport);
    graph.addCopyPass("lmx.pass.exposure.clearHistogram", std::move(histogramClearDesc),
                      [&commands, histogramImport](const PassResources& resources) {
                          const GraphResult<rojoRHI::Buffer*> histogram =
                              resources.buffer(histogramImport);
                          LMX_ASSERT(histogram.has_value(), histogram.error().message);
                          commands.fillBuffer(**histogram, 0, kHistogramBufferSize, 0);
                      });
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
            const GraphResult<rojoRHI::Texture*> scene = resources.texture(sceneColorRead);
            LMX_ASSERT(scene.has_value(), scene.error().message);
            const GraphResult<rojoRHI::Buffer*> histogram = resources.buffer(histogramCleared);
            LMX_ASSERT(histogram.has_value(), histogram.error().message);
            const GraphResult<rojoRHI::Buffer*> exposure = resources.buffer(exposureCurrent);
            LMX_ASSERT(exposure.has_value(), exposure.error().message);

            const HistogramParams params{.logLuminanceMin = kExposureLogLuminanceMin,
                                         .logLuminanceMax = kExposureLogLuminanceMax,
                                         .width = meterWidth,
                                         .height = meterHeight};
            commands.bindComputePipeline(*m_histogramPipeline);
            commands.bindTexture(kHistogramSceneColorSlot, **scene);
            commands.bindStorageBuffer(kHistogramBufferSlot, **histogram,
                                       rojoRHI::StorageAccess::ReadWrite);
            commands.bindBuffer(kHistogramExposureSlot, **exposure);
            commands.bindFrameData(kHistogramParamsSlot, params);
            commands.dispatch(divRoundUp(meterWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(meterHeight, kComputeThreadsPerGroup2D), 1);
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
            const GraphResult<rojoRHI::Buffer*> histogram = resources.buffer(histogramFinal);
            LMX_ASSERT(histogram.has_value(), histogram.error().message);
            const GraphResult<rojoRHI::Buffer*> exposure = resources.buffer(exposureCurrent);
            LMX_ASSERT(exposure.has_value(), exposure.error().message);

            const ExposureResolveParams params{.lowPercentile = view.exposureLowPercentile,
                                               .highPercentile = view.exposureHighPercentile,
                                               .targetGrey = view.exposureTargetGrey,
                                               .evMin = view.exposureEvMin,
                                               .evMax = view.exposureEvMax,
                                               .compensationEv = view.exposureCompensationEv,
                                               .logLuminanceMin = kExposureLogLuminanceMin,
                                               .logLuminanceMax = kExposureLogLuminanceMax,
                                               .adaptUp = view.exposureAdaptUpStopsPerSecond,
                                               .adaptDown = view.exposureAdaptDownStopsPerSecond,
                                               .deltaSeconds = kExposureFrameSeconds,
                                               .pad = 0.0f};
            commands.bindComputePipeline(*m_exposureResolvePipeline);
            commands.bindStorageBuffer(kResolveHistogramSlot, **histogram,
                                       rojoRHI::StorageAccess::Read);
            // Read-write, not write: the kernel steps from the exposure this frame applied, which
            // it reads out of index 0 before overwriting it.
            commands.bindStorageBuffer(kResolveExposureSlot, **exposure,
                                       rojoRHI::StorageAccess::ReadWrite);
            commands.bindFrameData(kResolveParamsSlot, params);
            commands.dispatch(1, 1, 1);
        });
    const GraphBuffer exposureResolved = nextVersion(exposureCurrent);
    if (view.autoExposureEnabled) {
        graph.exportBuffer(exposureResolved);
    }
}

} // namespace lmx::render
