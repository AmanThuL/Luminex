//----------------------------------------------------------------------------------------------------------------------
/// @file BloomStage.cpp
/// @brief Implements bloom resources and pass declaration.
//----------------------------------------------------------------------------------------------------------------------

#include "Render/BloomStage.h"

#include "Core/Assert.h"
#include "Core/Math.h"
#include "Render/Renderer.h"

#include <algorithm>
#include <format>
#include <utility>

namespace lmx::render {
namespace {

// Mirrors Shaders/BloomThreshold.slang's BloomThresholdParams.
struct BloomThresholdParams {
    float threshold;
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
};
static_assert(sizeof(BloomThresholdParams) == 20,
              "must match BloomThreshold.slang's BloomThresholdParams");

// Mirrors Shaders/BloomDownsample.slang's BloomDownsampleParams.
struct BloomDownsampleParams {
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
};
static_assert(sizeof(BloomDownsampleParams) == 16,
              "must match BloomDownsample.slang's BloomDownsampleParams");

// Mirrors Shaders/BloomUpsample.slang's BloomUpsampleParams.
struct BloomUpsampleParams {
    uint32_t smallWidth;
    uint32_t smallHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
};
static_assert(sizeof(BloomUpsampleParams) == 16,
              "must match BloomUpsample.slang's BloomUpsampleParams");

// BloomThreshold.slang's slot map.
constexpr uint32_t kBloomThresholdSceneColorSlot = 0; // texture
constexpr uint32_t kBloomThresholdDstSlot = 1;        // texture
constexpr uint32_t kBloomThresholdParamsSlot = 0;     // buffer

// BloomDownsample.slang's slot map (texture space only, plus its own buffer 0).
constexpr uint32_t kBloomDownsampleSrcSlot = 0;
constexpr uint32_t kBloomDownsampleDstSlot = 1;
constexpr uint32_t kBloomDownsampleParamsSlot = 0; // buffer

// BloomUpsample.slang's slot map (texture space only, plus its own buffer 0).
constexpr uint32_t kBloomUpsampleBaseSlot = 0;
constexpr uint32_t kBloomUpsampleSmallSlot = 1;
constexpr uint32_t kBloomUpsampleDstSlot = 2;
constexpr uint32_t kBloomUpsampleParamsSlot = 0; // buffer

constexpr uint32_t kComputeThreadsPerGroup2D = 8;

} // namespace

//======================================================================================================================
rojoRHI::Result<void> BloomStage::loadLibraries(rojoRHI::Device& device) {
    if (auto library = device.loadShaderLibrary("Shaders/BloomThreshold"); library) {
        m_bloomThresholdLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/BloomDownsample"); library) {
        m_bloomDownsampleLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    if (auto library = device.loadShaderLibrary("Shaders/BloomUpsample"); library) {
        m_bloomUpsampleLibrary = std::move(*library);
    } else {
        return std::unexpected(library.error());
    }
    return {};
}

//======================================================================================================================
rojoRHI::Result<void> BloomStage::createPipelines(rojoRHI::Device& device) {
    if (auto pipeline = device.createComputePipeline(
            {.library = m_bloomThresholdLibrary.get(),
             .computeEntry = "computeBloomThreshold",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.bloomThresholdPipeline"});
        pipeline) {
        m_bloomThresholdPipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline = device.createComputePipeline(
            {.library = m_bloomDownsampleLibrary.get(),
             .computeEntry = "computeBloomDownsample",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.bloomDownsamplePipeline"});
        pipeline) {
        m_bloomDownsamplePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }
    if (auto pipeline = device.createComputePipeline(
            {.library = m_bloomUpsampleLibrary.get(),
             .computeEntry = "computeBloomUpsample",
             .threadsPerThreadgroup = {kComputeThreadsPerGroup2D, kComputeThreadsPerGroup2D, 1},
             .label = "lmx.render.bloomUpsamplePipeline"});
        pipeline) {
        m_bloomUpsamplePipeline = std::move(*pipeline);
    } else {
        return std::unexpected(pipeline.error());
    }

    return {};
}

//======================================================================================================================
GraphTexture BloomStage::declare(RenderGraph& graph, rojoRHI::CommandList& commands,
                                 GraphTexture displayInput, uint32_t sceneWidth,
                                 uint32_t sceneHeight, float bloomThreshold) {
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
    // A chain that cannot downsample even once (an extent already at 1x1) has nothing for an
    // upsample-accumulate step to combine; bloom degrades to no contribution that frame rather
    // than declaring a transient nothing would ever write.
    const bool bloomChainSupportsUpsample = bloomMipCount >= 2;

    const GraphTexture bloomChain = graph.createTexture({.width = bloomWidth,
                                                         .height = bloomHeight,
                                                         .format = kSceneColorFormat,
                                                         .mipLevels = bloomMipCount,
                                                         .storageRead = true,
                                                         .storageWrite = true},
                                                        "lmx.render.bloomChain");

    static constexpr rojoRHI::TextureSubresourceRange kBloomMip0{.baseMipLevel = 0, .mipLevelCount = 1};
    ComputePassDesc thresholdDesc;
    thresholdDesc.shaderTextureReads.push_back(displayInput);
    thresholdDesc.textureWrites.push_back(TextureUseDesc(bloomChain, kBloomMip0));
    graph.addComputePass(
        "lmx.pass.bloom.threshold", std::move(thresholdDesc),
        [this, &commands, displayInput, bloomChain, sceneWidth, sceneHeight, bloomWidth,
         bloomHeight, bloomThreshold](const PassResources& resources) {
            const GraphResult<rojoRHI::Texture*> scene = resources.texture(displayInput);
            LMX_ASSERT(scene.has_value(), scene.error().message);
            const GraphResult<rojoRHI::Texture*> chain = resources.texture(bloomChain);
            LMX_ASSERT(chain.has_value(), chain.error().message);

            const BloomThresholdParams params{.threshold = bloomThreshold,
                                              .srcWidth = sceneWidth,
                                              .srcHeight = sceneHeight,
                                              .dstWidth = bloomWidth,
                                              .dstHeight = bloomHeight};
            commands.bindComputePipeline(*m_bloomThresholdPipeline);
            commands.bindTexture(kBloomThresholdSceneColorSlot, **scene);
            commands.bindStorageTexture(kBloomThresholdDstSlot, **chain,
                                        rojoRHI::TextureViewDesc{.range = kBloomMip0},
                                        rojoRHI::StorageAccess::Write);
            commands.bindFrameData(kBloomThresholdParamsSlot, params);
            commands.dispatch(divRoundUp(bloomWidth, kComputeThreadsPerGroup2D),
                              divRoundUp(bloomHeight, kComputeThreadsPerGroup2D), 1);
        });

    // Downsample chain: one pass per level, mip (L-1) -> mip L, each reading and writing disjoint
    // mips of the one bloomChain version that step left behind -- dispatches within a single
    // compute pass carry no ordering guarantee, so each level needs its own pass regardless of how
    // many there are.
    GraphTexture bloomChainVersion = nextVersion(bloomChain); // after the threshold wrote mip 0
    for (uint32_t level = 1; level < bloomMipCount; ++level) {
        const uint32_t srcWidth = std::max(bloomWidth >> (level - 1), 1u);
        const uint32_t srcHeight = std::max(bloomHeight >> (level - 1), 1u);
        const uint32_t dstWidth = std::max(bloomWidth >> level, 1u);
        const uint32_t dstHeight = std::max(bloomHeight >> level, 1u);
        const rojoRHI::TextureSubresourceRange srcRange{.baseMipLevel = level - 1, .mipLevelCount = 1};
        const rojoRHI::TextureSubresourceRange dstRange{.baseMipLevel = level, .mipLevelCount = 1};

        ComputePassDesc downsampleDesc;
        downsampleDesc.textureReads.push_back(TextureUseDesc(bloomChainVersion, srcRange));
        downsampleDesc.textureWrites.push_back(TextureUseDesc(bloomChainVersion, dstRange));
        graph.addComputePass(
            std::format("lmx.pass.bloom.downsample{}", level - 1), std::move(downsampleDesc),
            [this, &commands, bloomChainVersion, srcRange, dstRange, srcWidth, srcHeight, dstWidth,
             dstHeight](const PassResources& resources) {
                const GraphResult<rojoRHI::Texture*> chain = resources.texture(bloomChainVersion);
                LMX_ASSERT(chain.has_value(), chain.error().message);

                const BloomDownsampleParams params{.srcWidth = srcWidth,
                                                   .srcHeight = srcHeight,
                                                   .dstWidth = dstWidth,
                                                   .dstHeight = dstHeight};
                commands.bindComputePipeline(*m_bloomDownsamplePipeline);
                commands.bindStorageTexture(kBloomDownsampleSrcSlot, **chain,
                                            rojoRHI::TextureViewDesc{.range = srcRange},
                                            rojoRHI::StorageAccess::Read);
                commands.bindStorageTexture(kBloomDownsampleDstSlot, **chain,
                                            rojoRHI::TextureViewDesc{.range = dstRange},
                                            rojoRHI::StorageAccess::Write);
                commands.bindFrameData(kBloomDownsampleParamsSlot, params);
                commands.dispatch(divRoundUp(dstWidth, kComputeThreadsPerGroup2D),
                                  divRoundUp(dstHeight, kComputeThreadsPerGroup2D), 1);
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
            const uint32_t baseWidth = std::max(bloomWidth >> level, 1u);
            const uint32_t baseHeight = std::max(bloomHeight >> level, 1u);
            const uint32_t smallWidth = std::max(bloomWidth >> (level + 1), 1u);
            const uint32_t smallHeight = std::max(bloomHeight >> (level + 1), 1u);
            const rojoRHI::TextureSubresourceRange baseRange{.baseMipLevel = level, .mipLevelCount = 1};
            const rojoRHI::TextureSubresourceRange smallRange{.baseMipLevel = level + 1,
                                                          .mipLevelCount = 1};

            ComputePassDesc upsampleDesc;
            upsampleDesc.textureReads.push_back(TextureUseDesc(bloomChainFinal, baseRange));
            upsampleDesc.textureReads.push_back(
                TextureUseDesc(smallFromChain ? bloomChainFinal : bloomBlurVersion, smallRange));
            upsampleDesc.textureWrites.push_back(TextureUseDesc(bloomBlurVersion, baseRange));
            graph.addComputePass(
                std::format("lmx.pass.bloom.upsample{}", level), std::move(upsampleDesc),
                [this, &commands, bloomChainFinal, bloomBlurVersion, smallFromChain, baseRange,
                 smallRange, baseWidth, baseHeight, smallWidth,
                 smallHeight](const PassResources& resources) {
                    const GraphResult<rojoRHI::Texture*> chain = resources.texture(bloomChainFinal);
                    LMX_ASSERT(chain.has_value(), chain.error().message);
                    const GraphResult<rojoRHI::Texture*> blur = resources.texture(bloomBlurVersion);
                    LMX_ASSERT(blur.has_value(), blur.error().message);
                    rojoRHI::Texture& smallTexture = smallFromChain ? **chain : **blur;

                    const BloomUpsampleParams params{.smallWidth = smallWidth,
                                                     .smallHeight = smallHeight,
                                                     .dstWidth = baseWidth,
                                                     .dstHeight = baseHeight};
                    commands.bindComputePipeline(*m_bloomUpsamplePipeline);
                    commands.bindStorageTexture(kBloomUpsampleBaseSlot, **chain,
                                                rojoRHI::TextureViewDesc{.range = baseRange},
                                                rojoRHI::StorageAccess::Read);
                    commands.bindStorageTexture(kBloomUpsampleSmallSlot, smallTexture,
                                                rojoRHI::TextureViewDesc{.range = smallRange},
                                                rojoRHI::StorageAccess::Read);
                    commands.bindStorageTexture(kBloomUpsampleDstSlot, **blur,
                                                rojoRHI::TextureViewDesc{.range = baseRange},
                                                rojoRHI::StorageAccess::Write);
                    commands.bindFrameData(kBloomUpsampleParamsSlot, params);
                    commands.dispatch(divRoundUp(baseWidth, kComputeThreadsPerGroup2D),
                                      divRoundUp(baseHeight, kComputeThreadsPerGroup2D), 1);
                });
            bloomBlurVersion = nextVersion(bloomBlurVersion);
        }
        bloomResult = bloomBlurVersion;
    }

    return bloomResult;
}

} // namespace lmx::render
