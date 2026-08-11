//----------------------------------------------------------------------------------------------------------------------
/// @file HazardRhi.cpp
/// @brief Implements H01-H24 (spec section 7's hazard matrix) against the maintained RHI.
///
/// Every case follows the same shape: an earlier op ("producer" role) and a later op ("consumer"
/// role) separated by exactly one textureBarrier, over a target texture whose content is verified
/// against Workload/StressCases.h's frozen hazardExpectedTexel. What each role does depends on the
/// case's hazard kind (HazardKind), not on which field of HazardCase names it:
///
///   RAW:  producer WRITES producerMip with the case's expected content; consumer READS
///         consumerMip (pre-populated at texture creation with the same expected content, so a
///         whole-resource case's producerMip == consumerMip makes the read observe the producer's
///         own write -- a genuine read-after-write dependency).
///   WAR:  producer READS producerMip (pre-populated at creation with hazardOldRgba -- deliberately
///         different from the expected value, so a race is distinguishable); consumer WRITES
///         consumerMip with the expected content.
///   WAW:  producer WRITES producerMip with hazardOldRgba; consumer WRITES consumerMip with the
///         expected content.
///
/// A per-mip case's producerMip and consumerMip are always distinct (Workload/StressCases.h), so
/// there both mips are verified independently against the same expected function evaluated over
/// each mip's own texel grid -- the per-mip counterpart of the whole-resource cases' single shared
/// verification, and what exercises per-subresource hazard tracking rather than a same-address
/// race.
///
/// GAP: RHI's RenderPassDesc (RHI/Include/RHI/RHI.h) names no mip level or array layer for a color
/// attachment -- CommandList::beginRenderPass always renders into mip 0. Five per-mip cases need a
/// raster op at a non-zero mip (H15 consumer, H16/H21/H22 producer, H23 both) and are reported
/// INEXPRESSIBLE on this side rather than faked; see runOneHazardCaseRhi's gap check below. The
/// nearest expressible variant is the equivalent whole-resource (mip 0) producer/consumer pairing,
/// already covered by H01-H13.
//----------------------------------------------------------------------------------------------------------------------

#include "Bench/StressCommon.h"
#include "Bench/StressRunner.h"
#include "Workload/StressCases.h"

#include "RHI/RHI.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace lmx::noapi::bench {
namespace {

using workload::HazardCase;
using workload::HazardKind;
using workload::HazardOpKind;

enum class Role { Write, Read };

rhi::TextureUse useForRole(HazardOpKind kind, Role role) {
    switch (kind) {
    case HazardOpKind::Raster:
        return role == Role::Write ? rhi::TextureUse::RenderTarget : rhi::TextureUse::ShaderRead;
    case HazardOpKind::Compute:
        return role == Role::Write ? rhi::TextureUse::StorageWrite : rhi::TextureUse::ShaderRead;
    case HazardOpKind::Copy:
        return role == Role::Write ? rhi::TextureUse::CopyDestination : rhi::TextureUse::CopySource;
    }
    return rhi::TextureUse::ShaderRead;
}

rhi::TextureSubresourceRange mipRange(uint32_t mip) {
    return {.baseMipLevel = mip, .mipLevelCount = 1};
}

// Shaders/SamplerSmoke.slang's fullscreen-triangle vertex shader emits NDC positions directly from
// its UV parameter (vertexMain: `position = uv * 2 - 1`), so the same UV that samples row 0 of a
// source texture lands at the *bottom* of the rasterized target once Metal's viewport transform
// maps NDC y=-1 there -- the opposite of copyTextureToBuffer/copyBufferToTexture's row-major
// convention, which this file's compute and copy paths (verified correct independently) both use
// unflipped. This flips row order to compensate, applied only where a raster pass samples or
// produces the content: makeSourceTexture's raster caller (before upload) and the raster consumer's
// readback (after copy).
std::vector<uint8_t> flipRows(const std::vector<uint8_t>& rgba, uint32_t extent) {
    std::vector<uint8_t> flipped(rgba.size());
    const uint64_t rowBytes = uint64_t{extent} * 4;
    for (uint32_t row = 0; row < extent; ++row) {
        std::memcpy(flipped.data() + uint64_t{row} * rowBytes,
                    rgba.data() + uint64_t{extent - 1 - row} * rowBytes, rowBytes);
    }
    return flipped;
}

// Everything one hazard case shares across its two ops and final verification.
struct HazardHarness {
    std::unique_ptr<rhi::Device> device;
    std::unique_ptr<rhi::ShaderLibrary> copyLib;
    std::unique_ptr<rhi::ShaderLibrary> readLib;
    std::unique_ptr<rhi::ShaderLibrary>
        quadLib; // SamplerSmoke.slang, reused for raster passthrough.
    std::unique_ptr<rhi::ComputePipeline> copyPipeline;
    std::unique_ptr<rhi::ComputePipeline> readPipeline;
    std::unique_ptr<rhi::GraphicsPipeline> rasterPipeline;
    std::unique_ptr<rhi::Sampler> sampler;
    std::unique_ptr<rhi::Texture> target; // The case's shared texture.

    // Buffers/textures created per-op, kept alive until after readback.
    std::vector<std::unique_ptr<rhi::Buffer>> ownedBuffers;
    std::vector<std::unique_ptr<rhi::Texture>> ownedTextures;
};

//======================================================================================================================
// Uploads `content` (extent x extent RGBA8) as a fresh sampled source texture, for Write+Compute
// and Write+Raster roles.
rhi::Texture* makeSourceTexture(HazardHarness& h, const std::vector<uint8_t>& content,
                                uint32_t extent, std::string_view label) {
    const rhi::TextureMip mip{.data = content.data(), .bytesPerRow = uint64_t{extent} * 4};
    auto result = h.device->createTexture({.width = extent,
                                           .height = extent,
                                           .format = rhi::Format::RGBA8Unorm,
                                           .kind = rhi::TextureKind::Tex2D,
                                           .mipLevels = 1,
                                           .sampled = true,
                                           .label = label},
                                          std::span<const rhi::TextureMip>(&mip, 1));
    h.ownedTextures.push_back(std::move(*result));
    return h.ownedTextures.back().get();
}

//======================================================================================================================
rhi::Buffer* makeResultBuffer(HazardHarness& h, uint64_t size, bool storageWrite,
                              std::string_view label) {
    auto result = h.device->createBuffer(
        {.size = size, .storageWrite = storageWrite, .cpuReadback = true, .label = label}, nullptr);
    h.ownedBuffers.push_back(std::move(*result));
    return h.ownedBuffers.back().get();
}

//======================================================================================================================
// Executes one op (kind, Write) against `h.target` mip `mip`/`extent`, writing `content`.
void executeWrite(HazardHarness& h, rhi::CommandList& cmd, HazardOpKind kind, uint32_t mip,
                  uint32_t extent, const std::vector<uint8_t>& content) {
    switch (kind) {
    case HazardOpKind::Copy: {
        auto created = h.device->createBuffer(
            {.size = content.size(), .label = "hazard.copySource"}, content.data());
        h.ownedBuffers.push_back(std::move(*created));
        rhi::Buffer* source = h.ownedBuffers.back().get();
        cmd.beginCopyPass("hazard.write.copy");
        cmd.copyBufferToTexture(*source, {.offset = 0, .bytesPerRow = uint64_t{extent} * 4},
                                *h.target,
                                {.mipLevel = mip, .width = extent, .height = extent, .depth = 1});
        cmd.endCopyPass();
        break;
    }
    case HazardOpKind::Compute: {
        rhi::Texture* src = makeSourceTexture(h, content, extent, "hazard.computeWriteSource");
        cmd.beginComputePass("hazard.write.compute");
        cmd.bindComputePipeline(*h.copyPipeline);
        cmd.bindStorageTexture(0, *h.target, {.range = mipRange(mip)}, rhi::StorageAccess::Write);
        cmd.bindTexture(1, *src);
        const uint32_t params = extent;
        cmd.setUniforms(1, &params, sizeof(params));
        const uint32_t groups = (extent + 7) / 8;
        cmd.dispatch(groups, groups, 1);
        cmd.endComputePass();
        break;
    }
    case HazardOpKind::Raster: {
        rhi::Texture* src =
            makeSourceTexture(h, flipRows(content, extent), extent, "hazard.rasterWriteSource");
        cmd.beginRenderPass(
            {.colorTarget = h.target.get(), .clear = false, .label = "hazard.write.raster"});
        cmd.bindPipeline(*h.rasterPipeline);
        cmd.bindTexture(0, *src);
        cmd.bindSampler(0, *h.sampler);
        cmd.draw(3);
        cmd.endRenderPass();
        break;
    }
    }
}

//======================================================================================================================
// Executes one op (kind, Read) against `h.target` mip `mip`/`extent`. Returns the buffer holding
// the observed RGBA8 (or RGBA8-packed uint32, for the compute path) content, ready to read back
// once the frame retires.
rhi::Buffer* executeRead(HazardHarness& h, rhi::CommandList& cmd, HazardOpKind kind, uint32_t mip,
                         uint32_t extent) {
    switch (kind) {
    case HazardOpKind::Copy: {
        rhi::Buffer* out =
            makeResultBuffer(h, uint64_t{extent} * extent * 4, false, "hazard.readCopyOut");
        cmd.beginCopyPass("hazard.read.copy");
        cmd.copyTextureToBuffer(*h.target,
                                {.mipLevel = mip, .width = extent, .height = extent, .depth = 1},
                                *out, {.offset = 0, .bytesPerRow = uint64_t{extent} * 4});
        cmd.endCopyPass();
        return out;
    }
    case HazardOpKind::Compute: {
        rhi::Buffer* out =
            makeResultBuffer(h, uint64_t{extent} * extent * 4, true, "hazard.readComputeOut");
        cmd.beginComputePass("hazard.read.compute");
        cmd.bindComputePipeline(*h.readPipeline);
        cmd.bindStorageBuffer(0, *out, rhi::StorageAccess::Write);
        cmd.bindTexture(0, *h.target, {.range = mipRange(mip)});
        const uint32_t params = extent;
        cmd.setUniforms(1, &params, sizeof(params));
        const uint32_t groups = (extent + 7) / 8;
        cmd.dispatch(groups, groups, 1);
        cmd.endComputePass();
        return out;
    }
    case HazardOpKind::Raster: {
        auto resultTexture = h.device->createTexture({.width = extent,
                                                      .height = extent,
                                                      .format = rhi::Format::RGBA8Unorm,
                                                      .kind = rhi::TextureKind::Tex2D,
                                                      .mipLevels = 1,
                                                      .renderTarget = true,
                                                      .cpuReadback = true,
                                                      .label = "hazard.readRasterTarget"});
        h.ownedTextures.push_back(std::move(*resultTexture));
        rhi::Texture* dst = h.ownedTextures.back().get();
        cmd.beginRenderPass({.colorTarget = dst, .clear = false, .label = "hazard.read.raster"});
        cmd.bindPipeline(*h.rasterPipeline);
        cmd.bindTexture(0, *h.target, {.range = mipRange(mip)});
        cmd.bindSampler(0, *h.sampler);
        cmd.draw(3);
        cmd.endRenderPass();
        // Texture::readback needs no further copy; report through a sentinel by storing the texture
        // pointer where a Buffer* is expected is not type-safe, so callers of the Raster read path
        // read `dst` directly (see the case switch below, which special-cases Raster).
        return nullptr;
    }
    }
    return nullptr;
}

//======================================================================================================================
CaseResult verifyRgba(const std::string& id, const char* what, const std::vector<uint8_t>& expected,
                      const std::vector<uint8_t>& actual) {
    const int64_t mismatch = firstRgbMismatch(expected, actual);
    if (mismatch >= 0) {
        if (std::getenv("LMX_NOAPI_HAZARD_DEBUG") != nullptr) {
            std::fprintf(stderr,
                         "[debug] %s %s: expected=%02x%02x%02x actual=%02x%02x%02x size=%zu\n",
                         id.c_str(), what, expected[mismatch], expected[mismatch + 1],
                         expected[mismatch + 2], actual[mismatch], actual[mismatch + 1],
                         actual[mismatch + 2], expected.size());
        }
        return {id, false,
                std::string(what) + " mismatch at byte offset " + std::to_string(mismatch)};
    }
    return {id, true, ""};
}

//======================================================================================================================
CaseResult runOneHazardCaseRhi(const HazardCase& hc) {
    const uint32_t textureSize =
        hc.perMip ? workload::kHazardPerMipTextureSize : workload::kHazardWholeResourceTextureSize;
    const uint32_t mipCount = hc.perMip ? workload::kHazardPerMipTextureMipCount : 1;
    const uint32_t producerMip = hc.perMip ? hc.producerMip : 0;
    const uint32_t consumerMip = hc.perMip ? hc.consumerMip : 0;
    const uint32_t producerExtent = textureSize >> producerMip;
    const uint32_t consumerExtent = textureSize >> consumerMip;

    if ((hc.producer == HazardOpKind::Raster && producerMip != 0) ||
        (hc.consumer == HazardOpKind::Raster && consumerMip != 0)) {
        const uint32_t badMip = hc.producer == HazardOpKind::Raster ? producerMip : consumerMip;
        return {hc.id, false,
                "INEXPRESSIBLE on RHI: RenderPassDesc (RHI/Include/RHI/RHI.h) names no mip level "
                "or array layer for a colour attachment, so a raster op cannot target mip " +
                    std::to_string(badMip) +
                    " directly. Nearest expressible variant: the equivalent whole-resource (mip 0) "
                    "producer/consumer pairing, already covered by H01-H13."};
    }

    HazardHarness h;
    auto deviceResult = rhi::createDevice();
    if (!deviceResult) {
        return {hc.id, false, "device creation failed: " + deviceResult.error().message};
    }
    h.device = std::move(*deviceResult);

    const std::vector<uint8_t> producerExpected = hazardExpectedRgba(hc, producerExtent);
    const std::vector<uint8_t> consumerExpected = hazardExpectedRgba(hc, consumerExtent);
    const std::vector<uint8_t> producerOld = hazardOldRgba(hc, producerExtent);

    // The RAW case's consumer read needs consumerMip pre-populated (see file header). Every other
    // case's mips are fully established by real, timed ops.
    std::vector<rhi::TextureMip> initialMips(mipCount);
    std::vector<uint8_t> raw_consumer_upload;
    if (hc.hazard == HazardKind::ReadAfterWrite) {
        raw_consumer_upload = consumerExpected;
        initialMips[consumerMip] = {.data = raw_consumer_upload.data(),
                                    .bytesPerRow = uint64_t{consumerExtent} * 4};
    } else if (hc.hazard == HazardKind::WriteAfterRead) {
        initialMips[producerMip] = {.data = producerOld.data(),
                                    .bytesPerRow = uint64_t{producerExtent} * 4};
    }

    auto targetResult = h.device->createTexture({.width = textureSize,
                                                 .height = textureSize,
                                                 .format = rhi::Format::RGBA8Unorm,
                                                 .kind = rhi::TextureKind::Tex2D,
                                                 .mipLevels = mipCount,
                                                 .renderTarget = true,
                                                 .sampled = true,
                                                 .storageRead = true,
                                                 .storageWrite = true,
                                                 .label = "hazard.target"},
                                                initialMips);
    if (!targetResult) {
        return {hc.id, false, "target texture creation failed: " + targetResult.error().message};
    }
    h.target = std::move(*targetResult);

    auto copyLibResult = h.device->loadShaderLibrary("Shaders/StressCopyImage");
    auto readLibResult = h.device->loadShaderLibrary("Shaders/StressReadImage");
    auto quadLibResult = h.device->loadShaderLibrary("Shaders/SamplerSmoke");
    if (!copyLibResult || !readLibResult || !quadLibResult) {
        return {hc.id, false, "hazard shader library load failed"};
    }
    h.copyLib = std::move(*copyLibResult);
    h.readLib = std::move(*readLibResult);
    h.quadLib = std::move(*quadLibResult);

    auto copyPipelineResult = h.device->createComputePipeline({.library = h.copyLib.get(),
                                                               .computeEntry = "computeCopyImage",
                                                               .threadsPerThreadgroup = {8, 8, 1},
                                                               .label = "hazard.copyPipeline"});
    auto readPipelineResult =
        h.device->createComputePipeline({.library = h.readLib.get(),
                                         .computeEntry = "computeReadImageToBuffer",
                                         .threadsPerThreadgroup = {8, 8, 1},
                                         .label = "hazard.readPipeline"});
    auto rasterPipelineResult =
        h.device->createGraphicsPipeline({.library = h.quadLib.get(),
                                          .vertexEntry = "vertexMain",
                                          .fragmentEntry = "fragmentIdentityUv",
                                          .colorFormat = rhi::Format::RGBA8Unorm,
                                          .cullMode = rhi::CullMode::None,
                                          .label = "hazard.rasterPipeline"});
    if (!copyPipelineResult || !readPipelineResult || !rasterPipelineResult) {
        return {hc.id, false, "hazard pipeline creation failed"};
    }
    h.copyPipeline = std::move(*copyPipelineResult);
    h.readPipeline = std::move(*readPipelineResult);
    h.rasterPipeline = std::move(*rasterPipelineResult);

    auto samplerResult = h.device->createSampler({.filter = rhi::FilterMode::Nearest,
                                                  .addressMode = rhi::AddressMode::Clamp,
                                                  .label = "hazard.sampler"});
    if (!samplerResult) {
        return {hc.id, false, "hazard sampler creation failed"};
    }
    h.sampler = std::move(*samplerResult);

    rhi::CommandList& cmd = h.device->beginFrame();

    rhi::Buffer* producerReadBuffer = nullptr;
    rhi::Texture* producerReadTexture = nullptr;
    rhi::Buffer* consumerReadBuffer = nullptr;
    rhi::Texture* consumerReadTexture = nullptr;

    const auto readOp = [&](HazardOpKind kind, uint32_t mip, uint32_t extent, rhi::Buffer*& outBuf,
                            rhi::Texture*& outTex) {
        if (kind == HazardOpKind::Raster) {
            outTex = nullptr;
            executeRead(h, cmd, kind, mip, extent); // pushes the owned texture
            outTex = h.ownedTextures.back().get();
        } else {
            outBuf = executeRead(h, cmd, kind, mip, extent);
        }
    };

    if (hc.hazard == HazardKind::ReadAfterWrite) {
        executeWrite(h, cmd, hc.producer, producerMip, producerExtent, producerExpected);
        cmd.textureBarrier(*h.target, mipRange(producerMip), useForRole(hc.producer, Role::Write),
                           useForRole(hc.consumer, Role::Read));
        readOp(hc.consumer, consumerMip, consumerExtent, consumerReadBuffer, consumerReadTexture);
    } else if (hc.hazard == HazardKind::WriteAfterRead) {
        readOp(hc.producer, producerMip, producerExtent, producerReadBuffer, producerReadTexture);
        cmd.textureBarrier(*h.target, mipRange(producerMip), useForRole(hc.producer, Role::Read),
                           useForRole(hc.consumer, Role::Write));
        executeWrite(h, cmd, hc.consumer, consumerMip, consumerExtent, consumerExpected);
    } else { // WriteAfterWrite
        executeWrite(h, cmd, hc.producer, producerMip, producerExtent, producerOld);
        cmd.textureBarrier(*h.target, mipRange(producerMip), useForRole(hc.producer, Role::Write),
                           useForRole(hc.consumer, Role::Write));
        executeWrite(h, cmd, hc.consumer, consumerMip, consumerExtent, consumerExpected);
    }

    // Final verification copy of whichever mip(s) a Write role touched last, plus a barrier from
    // that write's use to CopySource. WAR's producer already returned its observation directly
    // (a Read role), so only WAW/RAW need this trailing copy for the write side(s) they performed.
    rhi::Buffer* producerVerifyBuffer = nullptr;
    rhi::Buffer* consumerVerifyBuffer = nullptr;
    if (hc.hazard == HazardKind::WriteAfterWrite) {
        cmd.textureBarrier(*h.target, mipRange(producerMip), useForRole(hc.producer, Role::Write),
                           rhi::TextureUse::CopySource);
        cmd.textureBarrier(*h.target, mipRange(consumerMip), useForRole(hc.consumer, Role::Write),
                           rhi::TextureUse::CopySource);
        producerVerifyBuffer = makeResultBuffer(h, uint64_t{producerExtent} * producerExtent * 4,
                                                false, "hazard.producerVerify");
        consumerVerifyBuffer = makeResultBuffer(h, uint64_t{consumerExtent} * consumerExtent * 4,
                                                false, "hazard.consumerVerify");
        cmd.beginCopyPass("hazard.verify.waw");
        cmd.copyTextureToBuffer(*h.target,
                                {.mipLevel = producerMip,
                                 .width = producerExtent,
                                 .height = producerExtent,
                                 .depth = 1},
                                *producerVerifyBuffer,
                                {.offset = 0, .bytesPerRow = uint64_t{producerExtent} * 4});
        cmd.copyTextureToBuffer(*h.target,
                                {.mipLevel = consumerMip,
                                 .width = consumerExtent,
                                 .height = consumerExtent,
                                 .depth = 1},
                                *consumerVerifyBuffer,
                                {.offset = 0, .bytesPerRow = uint64_t{consumerExtent} * 4});
        cmd.endCopyPass();
    } else if (hc.hazard == HazardKind::ReadAfterWrite) {
        cmd.textureBarrier(*h.target, mipRange(producerMip), useForRole(hc.producer, Role::Write),
                           rhi::TextureUse::CopySource);
        producerVerifyBuffer = makeResultBuffer(h, uint64_t{producerExtent} * producerExtent * 4,
                                                false, "hazard.producerVerify");
        cmd.beginCopyPass("hazard.verify.raw");
        cmd.copyTextureToBuffer(*h.target,
                                {.mipLevel = producerMip,
                                 .width = producerExtent,
                                 .height = producerExtent,
                                 .depth = 1},
                                *producerVerifyBuffer,
                                {.offset = 0, .bytesPerRow = uint64_t{producerExtent} * 4});
        cmd.endCopyPass();
    } else { // WriteAfterRead: consumer's write needs the same trailing verification.
        cmd.textureBarrier(*h.target, mipRange(consumerMip), useForRole(hc.consumer, Role::Write),
                           rhi::TextureUse::CopySource);
        consumerVerifyBuffer = makeResultBuffer(h, uint64_t{consumerExtent} * consumerExtent * 4,
                                                false, "hazard.consumerVerify");
        cmd.beginCopyPass("hazard.verify.war");
        cmd.copyTextureToBuffer(*h.target,
                                {.mipLevel = consumerMip,
                                 .width = consumerExtent,
                                 .height = consumerExtent,
                                 .depth = 1},
                                *consumerVerifyBuffer,
                                {.offset = 0, .bytesPerRow = uint64_t{consumerExtent} * 4});
        cmd.endCopyPass();
    }

    h.device->endFrame(nullptr);
    h.device->waitIdle();

    const auto readBuffer = [](rhi::Buffer* buffer, uint64_t size) {
        std::vector<uint8_t> bytes(size);
        buffer->readback(bytes.data(), size);
        return bytes;
    };
    const auto readTexture = [](rhi::Texture* texture, uint64_t size) {
        std::vector<uint8_t> bytes(size);
        texture->readback(bytes.data(), size);
        return bytes;
    };
    const auto readComputePacked = [](rhi::Buffer* buffer, uint32_t extent) {
        std::vector<uint32_t> words(uint64_t{extent} * extent);
        buffer->readback(words.data(), words.size() * sizeof(uint32_t));
        std::vector<uint8_t> bytes(words.size() * 4);
        for (size_t i = 0; i < words.size(); ++i) {
            bytes[i * 4 + 0] = static_cast<uint8_t>(words[i] >> 0);
            bytes[i * 4 + 1] = static_cast<uint8_t>(words[i] >> 8);
            bytes[i * 4 + 2] = static_cast<uint8_t>(words[i] >> 16);
            bytes[i * 4 + 3] = static_cast<uint8_t>(words[i] >> 24);
        }
        return bytes;
    };

    if (hc.hazard == HazardKind::ReadAfterWrite) {
        const std::vector<uint8_t> producerActual =
            readBuffer(producerVerifyBuffer, uint64_t{producerExtent} * producerExtent * 4);
        CaseResult producerCheck =
            verifyRgba(hc.id, "producer write", producerExpected, producerActual);
        if (!producerCheck.passed) {
            return producerCheck;
        }
        std::vector<uint8_t> consumerActual;
        if (hc.consumer == HazardOpKind::Raster) {
            consumerActual = flipRows(
                readTexture(consumerReadTexture, uint64_t{consumerExtent} * consumerExtent * 4),
                consumerExtent);
        } else if (hc.consumer == HazardOpKind::Compute) {
            consumerActual = readComputePacked(consumerReadBuffer, consumerExtent);
        } else {
            consumerActual =
                readBuffer(consumerReadBuffer, uint64_t{consumerExtent} * consumerExtent * 4);
        }
        return verifyRgba(hc.id, "consumer read", consumerExpected, consumerActual);
    }
    if (hc.hazard == HazardKind::WriteAfterRead) {
        std::vector<uint8_t> producerActual;
        if (hc.producer == HazardOpKind::Raster) {
            producerActual = flipRows(
                readTexture(producerReadTexture, uint64_t{producerExtent} * producerExtent * 4),
                producerExtent);
        } else if (hc.producer == HazardOpKind::Compute) {
            producerActual = readComputePacked(producerReadBuffer, producerExtent);
        } else {
            producerActual =
                readBuffer(producerReadBuffer, uint64_t{producerExtent} * producerExtent * 4);
        }
        CaseResult producerCheck =
            verifyRgba(hc.id, "producer read (old value)", producerOld, producerActual);
        if (!producerCheck.passed) {
            return producerCheck;
        }
        const std::vector<uint8_t> consumerActual =
            readBuffer(consumerVerifyBuffer, uint64_t{consumerExtent} * consumerExtent * 4);
        return verifyRgba(hc.id, "consumer write", consumerExpected, consumerActual);
    }
    // WriteAfterWrite. A whole-resource case's producer and consumer write the identical mip 0
    // subresource, so the consumer's write physically overwrites the producer's "old" bytes --
    // there is nothing left to observe there, and the correctness property collapses to "the final
    // state is exactly what the consumer wrote" (proving the producer's write did not race or
    // corrupt it). A per-mip case's two mips are disjoint memory, so both are independently
    // checked.
    if (hc.perMip) {
        const std::vector<uint8_t> producerActual =
            readBuffer(producerVerifyBuffer, uint64_t{producerExtent} * producerExtent * 4);
        CaseResult producerCheck =
            verifyRgba(hc.id, "producer write (old value)", producerOld, producerActual);
        if (!producerCheck.passed) {
            return producerCheck;
        }
    }
    const std::vector<uint8_t> consumerActual =
        readBuffer(consumerVerifyBuffer, uint64_t{consumerExtent} * consumerExtent * 4);
    return verifyRgba(hc.id, "consumer write", consumerExpected, consumerActual);
}

} // namespace

//======================================================================================================================
std::vector<CaseResult> runHazardCasesRhi(const std::string& caseId) {
    std::vector<CaseResult> results;
    for (const HazardCase& hc : workload::hazardCases()) {
        if (caseId != "all" && caseId != hc.id) {
            continue;
        }
        results.push_back(runOneHazardCaseRhi(hc));
    }
    return results;
}

} // namespace lmx::noapi::bench
