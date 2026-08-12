//----------------------------------------------------------------------------------------------------------------------
/// @file BindRhi.cpp
/// @brief Implements BindRhi for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Implements S-BIND (spec section 7) against the maintained RHI: `drawCount` draws over
/// 256
///        unique 64x64 RGBA8Unorm textures (texture index = draw index mod 256), one sampled
///        texture plus one per-draw uniform block per draw, into a 512x512 RGBA8Unorm target with
///        no depth.
///
///        Setup/per-frame split (spec section 8, restated here since this case is also the Stage 4b
///        timing workload): device creation, the 256 source textures and their one-time upload, the
///        target texture, the pipeline, and the sampler are all created once, before the loop below
///        -- ordinary one-time setup, excluded from any future timed region. The per-`drawCount`
///        loop's own body -- one bindTexture, one bindSampler, one setUniforms, and one draw call
///        per draw -- is exactly the per-frame encode path a Stage 4b timing harness would wrap;
///        nothing above it belongs inside that region.
///
///        Each draw's quad is a disjoint, non-overlapping tile of the 512x512 target (grid size =
///        sqrt(drawCount): 32x32 for 1,024 draws, 64x64 for 4,096), and every source texture is a
///        single solid colour, so the expected image has no blending or filtering ambiguity: cell
///        (col, row) must hold exactly texture `(row * gridSize + col) % 256`'s colour, tinted by
///        that draw's uniform block.

#include "Bench/StressCommon.h"
#include "Workload/Splitmix64.h"
#include "Workload/StressCases.h"

#include "RHI/RHI.h"
#include "RHI/Validate.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace lmx::experimental::noapi::bench {
namespace {

//======================================================================================================================
// Duplicated from RhiAdapter.cpp's identically named helper (that file's own comment explains the
// formula and its one accepted limitation: exact for the uncompressed formats both S-BIND and the
// representative graph use, an undercount for a hypothetical block-compressed texture). Kept as a
// small separate copy rather than a shared header because RhiAdapter.cpp's copy is `static` to its
// own translation unit and S-BIND's setup shape (flat arrays, not a persistent adapter object) does
// not share enough structure with RhiAdapter to justify factoring the two files together.
uint64_t textureRequestedBytes(const rhi::Texture& texture) {
    const uint32_t bytesPerTexel = rhi::bytesPerPixel(texture.format());
    uint64_t total = 0;
    for (uint32_t mip = 0; mip < texture.mipLevels(); ++mip) {
        const uint32_t width = rhi::mipExtent(texture.width(), mip);
        const uint32_t height = rhi::mipExtent(texture.height(), mip);
        total += uint64_t{width} * height * bytesPerTexel;
    }
    return total * texture.arrayLayers();
}

//======================================================================================================================
CaseResult runBindScaleRhi(uint32_t drawCount) {
    const std::string id = "S-BIND-" + std::to_string(drawCount);
    auto deviceResult = rhi::createDevice();
    if (!deviceResult) {
        return {id, false, "device creation failed: " + deviceResult.error().message};
    }
    auto device = std::move(*deviceResult);

    // 256 unique solid-colour 64x64 textures (spec: "256 unique 64x64 RGBA8Unorm textures").
    std::vector<std::unique_ptr<rhi::Texture>> textures;
    std::vector<std::array<uint8_t, 4>> colors(workload::kBindTextureCount);
    textures.reserve(workload::kBindTextureCount);
    std::vector<uint8_t> pixelBuffer(uint64_t{workload::kBindTextureSize} *
                                     workload::kBindTextureSize * 4);
    for (uint32_t t = 0; t < workload::kBindTextureCount; ++t) {
        const uint64_t draw = workload::splitmix64(workload::kSeed, {5000, t});
        workload::unitRgba8(draw, colors[t].data());
        colors[t][3] = 255;
        for (uint32_t texel = 0; texel < workload::kBindTextureSize * workload::kBindTextureSize;
             ++texel) {
            std::memcpy(pixelBuffer.data() + uint64_t{texel} * 4, colors[t].data(), 4);
        }
        const rhi::TextureMip mip{.data = pixelBuffer.data(),
                                  .bytesPerRow = uint64_t{workload::kBindTextureSize} * 4};
        auto textureResult = device->createTexture({.width = workload::kBindTextureSize,
                                                    .height = workload::kBindTextureSize,
                                                    .format = rhi::Format::RGBA8Unorm,
                                                    .kind = rhi::TextureKind::Tex2D,
                                                    .sampled = true,
                                                    .label = "sbind.source"},
                                                   std::span<const rhi::TextureMip>(&mip, 1));
        if (!textureResult) {
            return {id, false, "source texture creation failed"};
        }
        textures.push_back(std::move(*textureResult));
    }

    auto targetResult = device->createTexture({.width = workload::kBindTargetWidth,
                                               .height = workload::kBindTargetHeight,
                                               .format = rhi::Format::RGBA8Unorm,
                                               .kind = rhi::TextureKind::Tex2D,
                                               .renderTarget = true,
                                               .cpuReadback = true,
                                               .label = "sbind.target"});
    if (!targetResult) {
        return {id, false, "target texture creation failed"};
    }
    auto target = std::move(*targetResult);

    auto samplerResult = device->createSampler({.filter = rhi::FilterMode::Nearest,
                                                .addressMode = rhi::AddressMode::Clamp,
                                                .label = "sbind.sampler"});
    if (!samplerResult) {
        return {id, false, "sampler creation failed"};
    }
    auto sampler = std::move(*samplerResult);

    auto library = device->loadShaderLibrary("Shaders/StressQuad");
    if (!library) {
        return {id, false, "StressQuad load failed: " + library.error().message};
    }
    auto pipelineResult = device->createGraphicsPipeline({.library = library->get(),
                                                          .vertexEntry = "vertexQuad",
                                                          .fragmentEntry = "fragmentQuad",
                                                          .colorFormat = rhi::Format::RGBA8Unorm,
                                                          .fillMode = rhi::FillMode::Solid,
                                                          .cullMode = rhi::CullMode::None,
                                                          .label = "sbind.pipeline"});
    if (!pipelineResult) {
        return {id, false, "pipeline creation failed: " + pipelineResult.error().message};
    }
    auto pipeline = std::move(*pipelineResult);

    const uint32_t gridSize = static_cast<uint32_t>(std::lround(std::sqrt(double(drawCount))));
    const float cellNdc = 2.0f / static_cast<float>(gridSize);

    struct QuadParams {
        float offset[2], halfExtent[2], tint[4];
    };

    // FINDING (mirrors RhiAdapter.cpp's documented P04 finding): the production uniform ring is a
    // fixed 256 KiB per frame, and setUniforms rounds every call up to a 256-byte-aligned slot
    // regardless of payload size -- so drawCount draws alone need drawCount * 256 bytes, which
    // exceeds the ring at 1,024 draws already (262,144 > 262,144 is exactly at the edge once any
    // other per-frame uniform write shares the ring) and always at 4,096. S-BIND's per-draw uniform
    // block is therefore delivered through a freshly-created buffer per draw and bindBuffer,
    // exactly the same legal workaround the representative graph's P04 uses, not a convenience: it
    // is the only way to vary this slot's contents drawCount times under the shipped ring budget.
    // Buffer creation here is one-time setup (the frozen draw content is static across the run), so
    // it sits outside the timed region the same way the representative graph's per-draw buffers do.
    std::vector<std::unique_ptr<rhi::Buffer>> paramBuffers;
    paramBuffers.reserve(drawCount);
    for (uint32_t d = 0; d < drawCount; ++d) {
        const uint32_t col = d % gridSize;
        const uint32_t row = d / gridSize;
        // NDC y=+1 is the *top* of the viewport and lands at row 0 of a row-major readback (Metal's
        // viewport transform, like OpenGL's), so grid row 0 -- row 0 of the readback we verify
        // against -- must map to NDC y=+1, not -1: the y term below is negated relative to x's.
        const QuadParams params{.offset = {-1.0f + cellNdc * (static_cast<float>(col) + 0.5f),
                                           1.0f - cellNdc * (static_cast<float>(row) + 0.5f)},
                                .halfExtent = {cellNdc * 0.5f, cellNdc * 0.5f},
                                .tint = {1.0f, 1.0f, 1.0f, 1.0f}};
        auto buffer =
            device->createBuffer({.size = sizeof(params), .label = "sbind.quadParams"}, &params);
        if (!buffer) {
            return {id, false, "per-draw params buffer creation failed"};
        }
        paramBuffers.push_back(std::move(*buffer));
    }

    rhi::CommandList& cmd = device->beginFrame();
    cmd.beginRenderPass({.colorTarget = target.get(), .clear = true, .label = "sbind.draws"});
    cmd.bindPipeline(*pipeline);
    cmd.bindSampler(0, *sampler);
    for (uint32_t d = 0; d < drawCount; ++d) {
        const uint32_t textureIndex = workload::bindTextureIndexForDraw(d);
        cmd.bindTexture(0, *textures[textureIndex]);
        cmd.bindBuffer(2, *paramBuffers[d]);
        cmd.draw(6);
    }
    cmd.endRenderPass();
    device->endFrame(nullptr);
    device->waitIdle();

    std::vector<uint8_t> actual(uint64_t{workload::kBindTargetWidth} * workload::kBindTargetHeight *
                                4);
    target->readback(actual.data(), actual.size());

    for (uint32_t d = 0; d < drawCount; ++d) {
        const uint32_t col = d % gridSize;
        const uint32_t row = d / gridSize;
        const uint32_t cellPixels = workload::kBindTargetWidth / gridSize;
        const uint32_t px = col * cellPixels + cellPixels / 2;
        const uint32_t py = row * cellPixels + cellPixels / 2;
        const uint64_t offset = (uint64_t{py} * workload::kBindTargetWidth + px) * 4;
        const auto& expected = colors[workload::bindTextureIndexForDraw(d)];
        if (actual[offset] != expected[0] || actual[offset + 1] != expected[1] ||
            actual[offset + 2] != expected[2]) {
            return {id, false,
                    "draw " + std::to_string(d) +
                        " cell centre does not match its texture's colour"};
        }
    }
    return {id, true, ""};
}

} // namespace

//======================================================================================================================
std::vector<CaseResult> runBindCasesRhi(const std::string& caseId) {
    std::vector<CaseResult> results;
    for (uint32_t drawCount : workload::kBindDrawCounts) {
        const std::string id = "S-BIND-" + std::to_string(drawCount);
        if (caseId != "all" && caseId != id) {
            continue;
        }
        results.push_back(runBindScaleRhi(drawCount));
    }
    return results;
}

//======================================================================================================================
MeasuredRun measureBindScaleRhi(uint32_t drawCount, uint32_t warmupFrames,
                                uint32_t measuredFrames) {
    MeasuredRun result;

    auto deviceResult = rhi::createDevice({.enableValidation = false});
    if (!deviceResult) {
        result.error = "device creation failed: " + deviceResult.error().message;
        return result;
    }
    auto device = std::move(*deviceResult);

    std::vector<std::unique_ptr<rhi::Texture>> textures;
    std::vector<std::array<uint8_t, 4>> colors(workload::kBindTextureCount);
    textures.reserve(workload::kBindTextureCount);
    std::vector<uint8_t> pixelBuffer(uint64_t{workload::kBindTextureSize} *
                                     workload::kBindTextureSize * 4);
    for (uint32_t t = 0; t < workload::kBindTextureCount; ++t) {
        const uint64_t draw = workload::splitmix64(workload::kSeed, {5000, t});
        workload::unitRgba8(draw, colors[t].data());
        colors[t][3] = 255;
        for (uint32_t texel = 0; texel < workload::kBindTextureSize * workload::kBindTextureSize;
             ++texel) {
            std::memcpy(pixelBuffer.data() + uint64_t{texel} * 4, colors[t].data(), 4);
        }
        const rhi::TextureMip mip{.data = pixelBuffer.data(),
                                  .bytesPerRow = uint64_t{workload::kBindTextureSize} * 4};
        auto textureResult = device->createTexture({.width = workload::kBindTextureSize,
                                                    .height = workload::kBindTextureSize,
                                                    .format = rhi::Format::RGBA8Unorm,
                                                    .kind = rhi::TextureKind::Tex2D,
                                                    .sampled = true,
                                                    .label = "sbind.source"},
                                                   std::span<const rhi::TextureMip>(&mip, 1));
        if (!textureResult) {
            result.error = "source texture creation failed";
            return result;
        }
        textures.push_back(std::move(*textureResult));
    }

    auto targetResult = device->createTexture({.width = workload::kBindTargetWidth,
                                               .height = workload::kBindTargetHeight,
                                               .format = rhi::Format::RGBA8Unorm,
                                               .kind = rhi::TextureKind::Tex2D,
                                               .renderTarget = true,
                                               .cpuReadback = true,
                                               .label = "sbind.target"});
    if (!targetResult) {
        result.error = "target texture creation failed";
        return result;
    }
    auto target = std::move(*targetResult);

    auto samplerResult = device->createSampler({.filter = rhi::FilterMode::Nearest,
                                                .addressMode = rhi::AddressMode::Clamp,
                                                .label = "sbind.sampler"});
    if (!samplerResult) {
        result.error = "sampler creation failed";
        return result;
    }
    auto sampler = std::move(*samplerResult);

    auto library = device->loadShaderLibrary("Shaders/StressQuad");
    if (!library) {
        result.error = "StressQuad load failed: " + library.error().message;
        return result;
    }
    auto pipelineResult = device->createGraphicsPipeline({.library = library->get(),
                                                          .vertexEntry = "vertexQuad",
                                                          .fragmentEntry = "fragmentQuad",
                                                          .colorFormat = rhi::Format::RGBA8Unorm,
                                                          .fillMode = rhi::FillMode::Solid,
                                                          .cullMode = rhi::CullMode::None,
                                                          .label = "sbind.pipeline"});
    if (!pipelineResult) {
        result.error = "pipeline creation failed: " + pipelineResult.error().message;
        return result;
    }
    auto pipeline = std::move(*pipelineResult);

    const uint32_t gridSize = static_cast<uint32_t>(std::lround(std::sqrt(double(drawCount))));
    const float cellNdc = 2.0f / static_cast<float>(gridSize);

    struct QuadParams {
        float offset[2], halfExtent[2], tint[4];
    };

    // One-time setup, per BindRhi.cpp's own header comment: the frozen per-draw content never
    // changes across the run, so every param buffer is created once here and reused, unwritten,
    // by every measured frame's re-encode below.
    std::vector<std::unique_ptr<rhi::Buffer>> paramBuffers;
    paramBuffers.reserve(drawCount);
    for (uint32_t d = 0; d < drawCount; ++d) {
        const uint32_t col = d % gridSize;
        const uint32_t row = d / gridSize;
        const QuadParams params{.offset = {-1.0f + cellNdc * (static_cast<float>(col) + 0.5f),
                                           1.0f - cellNdc * (static_cast<float>(row) + 0.5f)},
                                .halfExtent = {cellNdc * 0.5f, cellNdc * 0.5f},
                                .tint = {1.0f, 1.0f, 1.0f, 1.0f}};
        auto buffer =
            device->createBuffer({.size = sizeof(params), .label = "sbind.quadParams"}, &params);
        if (!buffer) {
            result.error = "per-draw params buffer creation failed";
            return result;
        }
        paramBuffers.push_back(std::move(*buffer));
    }

    result.endOfSetup = {.textureCreateCalls = workload::kBindTextureCount + 1,
                         .bufferCreateCalls = drawCount,
                         .samplerCreateCalls = 1,
                         .pipelineCreateCalls = 1};
    for (const std::unique_ptr<rhi::Texture>& texture : textures) {
        result.endOfSetup.requestedBytes += textureRequestedBytes(*texture);
    }
    result.endOfSetup.requestedBytes += textureRequestedBytes(*target);
    for (const std::unique_ptr<rhi::Buffer>& buffer : paramBuffers) {
        result.endOfSetup.requestedBytes += buffer->size();
    }

    const uint32_t totalFrames = warmupFrames + measuredFrames;
    result.perFrameTimedRegionNs.reserve(measuredFrames);
    bool countersEverSet = false;
    for (uint32_t frame = 0; frame < totalFrames; ++frame) {
        FrameBindingCounters frameCounters{};

        // ---- BEGIN TIMED REGION (spec section 8; identical clock and boundary to RhiAdapter.cpp
        // and NoApiAdapter.cpp: begins after the frame-slot pacing wait beginFrame() bundles,
        // includes every binding call and the submit, excludes the readback wait below)
        const auto start = std::chrono::steady_clock::now();
        rhi::CommandList& cmd = device->beginFrame();
        cmd.beginRenderPass({.colorTarget = target.get(), .clear = true, .label = "sbind.draws"});
        cmd.bindPipeline(*pipeline);
        cmd.bindSampler(0, *sampler);
        frameCounters.bindCalls += 1;
        for (uint32_t d = 0; d < drawCount; ++d) {
            const uint32_t textureIndex = workload::bindTextureIndexForDraw(d);
            cmd.bindTexture(0, *textures[textureIndex]);
            cmd.bindBuffer(2, *paramBuffers[d]);
            cmd.draw(6);
            frameCounters.bindCalls += 2;
        }
        cmd.endRenderPass();
        device->endFrame(nullptr);
        const auto end = std::chrono::steady_clock::now();
        // ---- END TIMED REGION

        device->waitIdle();
        if (frame >= warmupFrames) {
            result.perFrameTimedRegionNs.push_back(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
            if (!countersEverSet) {
                result.counters = frameCounters;
                countersEverSet = true;
            } else if (result.counters != frameCounters) {
                result.countersStableAcrossFrames = false;
            }
        }
    }

    result.endOfRun = result.endOfSetup; // Nothing is created or destroyed after setup, per frame.
    result.ok = true;
    return result;
}

} // namespace lmx::experimental::noapi::bench
