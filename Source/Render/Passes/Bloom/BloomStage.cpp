//----------------------------------------------------------------------------------------------------------------------
/// @file BloomStage.cpp
/// @brief Implements bloom resources and pass declaration.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/Passes/Bloom/BloomStage.h"
#include "Engine/Gpu/CaptureSchema.h"
#include "Render/Common/Dispatch.h"
#include "Render/Common/FrameData.h"
#include "Render/Common/GraphResources.h"
#include "Render/Passes/Bloom/BloomFrameData.h"
#include "Render/Passes/Bloom/BloomRoots.h"

#include "Core/Diagnostics/Assert.h"
#include "Core/Math/Scalar.h"
#include "Engine/Gpu/GpuOwned.h"
#include "Render/Renderer/Renderer.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace lmx::render {
namespace {

//======================================================================================================================
void registerBloomLayoutsForCapture() {
    engine::registerUniformStruct(
        {"BloomThresholdRoot",
         sizeof(BloomThresholdRoot),
         {{"paramsAddr", offsetof(BloomThresholdRoot, paramsAddr), "Addr"},
          {"sceneColor", offsetof(BloomThresholdRoot, sceneColor), "uint"},
          {"destination", offsetof(BloomThresholdRoot, destination), "uint"}}});
    engine::registerUniformStruct(
        {"BloomDownsampleRoot",
         sizeof(BloomDownsampleRoot),
         {{"paramsAddr", offsetof(BloomDownsampleRoot, paramsAddr), "Addr"},
          {"source", offsetof(BloomDownsampleRoot, source), "uint"},
          {"destination", offsetof(BloomDownsampleRoot, destination), "uint"}}});
    engine::registerUniformStruct(
        {"BloomUpsampleRoot",
         sizeof(BloomUpsampleRoot),
         {{"paramsAddr", offsetof(BloomUpsampleRoot, paramsAddr), "Addr"},
          {"base", offsetof(BloomUpsampleRoot, base), "uint"},
          {"small", offsetof(BloomUpsampleRoot, small), "uint"},
          {"destination", offsetof(BloomUpsampleRoot, destination), "uint"},
          {"padding0", offsetof(BloomUpsampleRoot, padding0), "uint"},
          {"padding1", offsetof(BloomUpsampleRoot, padding1), "uint"},
          {"padding2", offsetof(BloomUpsampleRoot, padding2), "uint"}}});
    engine::registerUniformStruct(
        {"BloomThresholdParams",
         sizeof(BloomThresholdParams),
         {{"threshold", offsetof(BloomThresholdParams, threshold), "float"},
          {"srcWidth", offsetof(BloomThresholdParams, srcWidth), "uint"},
          {"srcHeight", offsetof(BloomThresholdParams, srcHeight), "uint"},
          {"dstWidth", offsetof(BloomThresholdParams, dstWidth), "uint"},
          {"dstHeight", offsetof(BloomThresholdParams, dstHeight), "uint"}}});
    engine::registerUniformStruct(
        {"BloomDownsampleParams",
         sizeof(BloomDownsampleParams),
         {{"srcWidth", offsetof(BloomDownsampleParams, srcWidth), "uint"},
          {"srcHeight", offsetof(BloomDownsampleParams, srcHeight), "uint"},
          {"dstWidth", offsetof(BloomDownsampleParams, dstWidth), "uint"},
          {"dstHeight", offsetof(BloomDownsampleParams, dstHeight), "uint"}}});
    engine::registerUniformStruct(
        {"BloomUpsampleParams",
         sizeof(BloomUpsampleParams),
         {{"smallWidth", offsetof(BloomUpsampleParams, smallWidth), "uint"},
          {"smallHeight", offsetof(BloomUpsampleParams, smallHeight), "uint"},
          {"dstWidth", offsetof(BloomUpsampleParams, dstWidth), "uint"},
          {"dstHeight", offsetof(BloomUpsampleParams, dstHeight), "uint"}}});
}

} // namespace

//======================================================================================================================
lmx::Result<std::unique_ptr<BloomStage>> BloomStage::create(engine::GpuContext& gpu) {
    rojoRHI::Device& device = gpu.device();
    std::unique_ptr<BloomStage> self(new BloomStage);

    {
        auto library = gpu.loadShaderLibrary("BloomThreshold");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_bloomThresholdLibrary = std::move(*library);
    }
    {
        auto library = gpu.loadShaderLibrary("BloomDownsample");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_bloomDownsampleLibrary = std::move(*library);
    }
    {
        auto library = gpu.loadShaderLibrary("BloomUpsample");
        if (!library) {
            return std::unexpected(library.error());
        }
        self->m_bloomUpsampleLibrary = std::move(*library);
    }

    {
        auto pipeline = engine::adoptGpu(
            device, device.createPipeline(rojoRHI::ComputePipelineDesc{
                        .compute = {self->m_bloomThresholdLibrary.get(), "computeBloomThreshold"},
                        .label = "lmx.render.bloomThresholdPipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_bloomThresholdPipeline = std::move(*pipeline);
    }
    {
        auto pipeline = engine::adoptGpu(
            device, device.createPipeline(rojoRHI::ComputePipelineDesc{
                        .compute = {self->m_bloomDownsampleLibrary.get(), "computeBloomDownsample"},
                        .label = "lmx.render.bloomDownsamplePipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_bloomDownsamplePipeline = std::move(*pipeline);
    }
    {
        auto pipeline = engine::adoptGpu(
            device, device.createPipeline(rojoRHI::ComputePipelineDesc{
                        .compute = {self->m_bloomUpsampleLibrary.get(), "computeBloomUpsample"},
                        .label = "lmx.render.bloomUpsamplePipeline"}));
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        self->m_bloomUpsamplePipeline = std::move(*pipeline);
    }

    registerBloomLayoutsForCapture();
    return self;
}

//======================================================================================================================
GraphTexture BloomStage::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                 const BloomInputs& inputs) {
    const auto displayInput = inputs.displayInput;
    const auto sceneWidth = inputs.sceneWidth;
    const auto sceneHeight = inputs.sceneHeight;
    const auto bloomThreshold = inputs.bloomThreshold;
    // ---- Bloom (spec 10). Two graph-created transients: `bloomChain`'s mips hold the threshold
    // and the downsample chain, and `bloomBlur`'s mips hold the upsample-accumulate walk back up
    // -- a second transient rather than accumulating into bloomChain in place, because one compute
    // pass may read and write one texture only through disjoint ranges (spec 6), and the
    // accumulate step's inputs (a bloomChain mip) and output (the same-sized bloomBlur mip) would
    // otherwise name overlapping ranges of one resource if they shared it. BloomUpsample.slang's
    // header carries the same reasoning. Declared every frame; only the display pass's read of
    // bloomBlur is conditional, so dead-pass culling drops threshold/downsample/upsample together
    // when bloom is off.
    // Ceil division includes the final source column and row in the threshold pass for odd scene
    // extents. Upsample and display reconstruction map pixel centres using the actual extents,
    // with bilinear filtering and clamped edge samples.
    const uint32_t bloomWidth = divRoundUp(sceneWidth, 2u);
    const uint32_t bloomHeight = divRoundUp(sceneHeight, 2u);

    // "A downsample chain into the mips" (spec 10) wants several levels, clamped to whatever the
    // extent supports without a mip collapsing to 1x1 before it has to: bloomMipCount counts mip 0
    // (the threshold's own output) plus up to kMaxBloomDownsampleLevels further halvings.
    constexpr uint32_t kMaxBloomDownsampleLevels = 4;
    uint32_t bloomMipCount = 1;
    for (uint32_t levelExtent = std::min(bloomWidth, bloomHeight);
         bloomMipCount <= kMaxBloomDownsampleLevels && levelExtent > 1; ++bloomMipCount) {
        levelExtent = std::max(levelExtent / 2u, 1u);
    }
    // A chain that cannot downsample returns its written threshold mip directly; larger chains
    // return the upsample-accumulate texture instead.
    const bool bloomChainSupportsUpsample = bloomMipCount >= 2;

    const GraphTexture bloomChain = graph.createTexture({.width = bloomWidth,
                                                         .height = bloomHeight,
                                                         .format = kSceneColorFormat,
                                                         .mipLevels = bloomMipCount,
                                                         .sampled = !bloomChainSupportsUpsample,
                                                         .storageRead = true,
                                                         .storageWrite = true},
                                                        "lmx.render.bloomChain");

    static constexpr rojoRHI::SubresourceRange kBloomMip0{.baseMip = 0, .mipCount = 1};
    ComputePassDesc thresholdDesc;
    thresholdDesc.shaderTextureReads.push_back(displayInput);
    thresholdDesc.textureWrites.push_back(TextureUseDesc(bloomChain, kBloomMip0));
    graph.addComputePass(
        "lmx.pass.bloom.threshold", std::move(thresholdDesc),
        [this, &commands, displayInput, bloomChain, sceneWidth, sceneHeight, bloomWidth,
         bloomHeight, bloomThreshold](const PassResources& resources) {
            const BloomThresholdParams params =
                buildBloomThresholdParams({.threshold = bloomThreshold,
                                           .sceneWidth = sceneWidth,
                                           .sceneHeight = sceneHeight,
                                           .bloomWidth = bloomWidth,
                                           .bloomHeight = bloomHeight});
            const auto paramsAddress = frameData(commands, params);
            engine::registerFrameRoot("BloomThresholdParams", paramsAddress);
            commands.setPipeline(m_bloomThresholdPipeline.get());
            engine::registerFrameRoot(
                "BloomThresholdRoot",
                commands.pushRoot(BloomThresholdRoot{
                    .paramsAddr = paramsAddress,
                    .sceneColor = static_cast<uint32_t>(resources.view(displayInput)),
                    .destination = static_cast<uint32_t>(resources.storageView(bloomChain, 0))}));
#ifndef NDEBUG
            const std::array<rojoRHI::ResourceAccess, 2> accesses{
                {{.texture = resources.texture(displayInput).texture,
                  .range = {0, 1, 0, 1},
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderRead},
                 {.texture = resources.texture(bloomChain).texture,
                  .range = kBloomMip0,
                  .stage = rojoRHI::Stage::ComputeShader,
                  .access = rojoRHI::Access::ShaderWrite}}};
            commands.declareAccess(accesses);
#endif
            const auto groups = dispatchGroups2D(bloomWidth, bloomHeight);
            commands.dispatch(groups[0], groups[1], 1);
        });

    // Downsample chain: one pass per level, mip (L-1) -> mip L, each reading and writing disjoint
    // mips of the one bloomChain version that step left behind -- dispatches within a single
    // compute pass carry no ordering guarantee, so each level needs its own pass regardless of how
    // many there are.
    GraphTexture bloomChainVersion = nextVersion(bloomChain); // after the threshold wrote mip 0
    for (uint32_t level = 1; level < bloomMipCount; ++level) {
        const BloomDownsampleParams params =
            buildBloomDownsampleParams(bloomWidth, bloomHeight, level);
        const rojoRHI::SubresourceRange srcRange{.baseMip = level - 1, .mipCount = 1};
        const rojoRHI::SubresourceRange dstRange{.baseMip = level, .mipCount = 1};

        ComputePassDesc downsampleDesc;
        downsampleDesc.textureReads.push_back(TextureUseDesc(bloomChainVersion, srcRange));
        downsampleDesc.textureWrites.push_back(TextureUseDesc(bloomChainVersion, dstRange));
        graph.addComputePass(
            std::format("lmx.pass.bloom.downsample{}", level - 1), std::move(downsampleDesc),
            [this, &commands, bloomChainVersion, srcRange, dstRange,
             params](const PassResources& resources) {
                const auto paramsAddress = frameData(commands, params);
                engine::registerFrameRoot("BloomDownsampleParams", paramsAddress);
                commands.setPipeline(m_bloomDownsamplePipeline.get());
                engine::registerFrameRoot(
                    "BloomDownsampleRoot",
                    commands.pushRoot(BloomDownsampleRoot{
                        .paramsAddr = paramsAddress,
                        .source = static_cast<uint32_t>(
                            resources.storageView(bloomChainVersion, srcRange.baseMip)),
                        .destination = static_cast<uint32_t>(
                            resources.storageView(bloomChainVersion, dstRange.baseMip))}));
#ifndef NDEBUG
                const std::array<rojoRHI::ResourceAccess, 2> accesses{
                    {{.texture = resources.texture(bloomChainVersion).texture,
                      .range = srcRange,
                      .stage = rojoRHI::Stage::ComputeShader,
                      .access = rojoRHI::Access::ShaderRead},
                     {.texture = resources.texture(bloomChainVersion).texture,
                      .range = dstRange,
                      .stage = rojoRHI::Stage::ComputeShader,
                      .access = rojoRHI::Access::ShaderWrite}}};
                commands.declareAccess(accesses);
#endif
                const auto groups = dispatchGroups2D(params.dstWidth, params.dstHeight);
                commands.dispatch(groups[0], groups[1], 1);
            });
        bloomChainVersion = nextVersion(bloomChainVersion);
    }
    const GraphTexture bloomChainFinal = bloomChainVersion;

    GraphTexture bloomResult = bloomChainFinal; // overwritten below when there is a chain to walk
    if (bloomChainSupportsUpsample) {
        const GraphTexture bloomBlur = graph.createTexture({.width = bloomWidth,
                                                            .height = bloomHeight,
                                                            .format = kSceneColorFormat,
                                                            .mipLevels = bloomMipCount - 1,
                                                            .sampled = true,
                                                            .storageRead = true,
                                                            .storageWrite = true},
                                                           "lmx.render.bloomBlur");

        // Upsample-accumulate: walks from the smallest mip back to mip 0, one pass per level. The
        // first step's "small" input is bloomChain's own smallest mip; every later step's is the
        // previous step's own bloomBlur output, so the same kernel serves every level regardless
        // of which resource happens to be on the small side.
        GraphTexture bloomBlurVersion = bloomBlur; // v0 until the first write below
        for (uint32_t stepsRemaining = bloomMipCount - 1; stepsRemaining > 0; --stepsRemaining) {
            const uint32_t level = stepsRemaining - 1; // walks bloomMipCount - 2 down to 0
            const bool smallFromChain = level == bloomMipCount - 2;
            const BloomUpsampleParams params =
                buildBloomUpsampleParams(bloomWidth, bloomHeight, level);
            const rojoRHI::SubresourceRange baseRange{.baseMip = level, .mipCount = 1};
            const rojoRHI::SubresourceRange smallRange{.baseMip = level + 1, .mipCount = 1};

            ComputePassDesc upsampleDesc;
            upsampleDesc.textureReads.push_back(TextureUseDesc(bloomChainFinal, baseRange));
            upsampleDesc.textureReads.push_back(
                TextureUseDesc(smallFromChain ? bloomChainFinal : bloomBlurVersion, smallRange));
            upsampleDesc.textureWrites.push_back(TextureUseDesc(bloomBlurVersion, baseRange));
            graph.addComputePass(
                std::format("lmx.pass.bloom.upsample{}", level), std::move(upsampleDesc),
                [this, &commands, bloomChainFinal, bloomBlurVersion, smallFromChain, baseRange,
                 smallRange, params](const PassResources& resources) {
                    const auto small = smallFromChain ? bloomChainFinal : bloomBlurVersion;
                    const auto paramsAddress = frameData(commands, params);
                    engine::registerFrameRoot("BloomUpsampleParams", paramsAddress);
                    commands.setPipeline(m_bloomUpsamplePipeline.get());
                    engine::registerFrameRoot(
                        "BloomUpsampleRoot",
                        commands.pushRoot(BloomUpsampleRoot{
                            .paramsAddr = paramsAddress,
                            .base = static_cast<uint32_t>(
                                resources.storageView(bloomChainFinal, baseRange.baseMip)),
                            .small = static_cast<uint32_t>(
                                resources.storageView(small, smallRange.baseMip)),
                            .destination = static_cast<uint32_t>(
                                resources.storageView(bloomBlurVersion, baseRange.baseMip))}));
#ifndef NDEBUG
                    const std::array<rojoRHI::ResourceAccess, 3> accesses{
                        {{.texture = resources.texture(bloomChainFinal).texture,
                          .range = baseRange,
                          .stage = rojoRHI::Stage::ComputeShader,
                          .access = rojoRHI::Access::ShaderRead},
                         {.texture = resources.texture(small).texture,
                          .range = smallRange,
                          .stage = rojoRHI::Stage::ComputeShader,
                          .access = rojoRHI::Access::ShaderRead},
                         {.texture = resources.texture(bloomBlurVersion).texture,
                          .range = baseRange,
                          .stage = rojoRHI::Stage::ComputeShader,
                          .access = rojoRHI::Access::ShaderWrite}}};
                    commands.declareAccess(accesses);
#endif
                    const auto groups = dispatchGroups2D(params.dstWidth, params.dstHeight);
                    commands.dispatch(groups[0], groups[1], 1);
                });
            bloomBlurVersion = nextVersion(bloomBlurVersion);
        }
        bloomResult = bloomBlurVersion;
    }

    return bloomResult;
}

} // namespace lmx::render
