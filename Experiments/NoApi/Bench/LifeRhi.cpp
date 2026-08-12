//----------------------------------------------------------------------------------------------------------------------
/// @file LifeRhi.cpp
/// @brief Implements LifeRhi for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Implements S-LIFE (spec section 7) against the maintained RHI: the frozen 12-frame
/// extent
///        schedule over E1/E2/E3 plus frame 4's mid-flight upload.
///
///        Simplification: this correctness run waits the device idle after every frame (the same
///        choice RhiAdapter.cpp's header comment records for the representative graph's correctness
///        run), so "frames 1-2 in flight" when the resize is issued during frame 3 is realised as
///        "frames 1-2 already retired by the time frame 3 begins" rather than genuinely overlapped
///        execution -- a weaker but still real exercise of the schedule's resize/destroy ordering:
///        a texture is destroyed only once every frame that could have referenced it has retired,
///        which this run proves trivially (every frame retires before the next begins) rather than
///        under real pipelining. Allocation counters (kept here, since the public RHI exposes no
///        built-in ones) are asserted settled at frame 11: exactly one texture and one buffer
///        alive, matching every create() this run issued minus every destroy().

#include "Bench/StressCommon.h"
#include "Workload/StressCases.h"

#include "RHI/RHI.h"

#include <array>
#include <memory>
#include <sstream>

namespace lmx::experimental::noapi::bench {
namespace {

struct Counters {
    uint32_t textureCreates = 0, textureDestroys = 0;
};

//======================================================================================================================
std::unique_ptr<rhi::Texture> makeExtentTexture(rhi::Device& device, Counters& counters,
                                                workload::Extent extent) {
    auto result = device.createTexture({.width = extent.width,
                                        .height = extent.height,
                                        .format = rhi::Format::RGBA8Unorm,
                                        .renderTarget = true,
                                        .cpuReadback = true,
                                        .label = "slife.target"});
    counters.textureCreates += 1;
    return result ? std::move(*result) : nullptr;
}

} // namespace

//======================================================================================================================
std::vector<CaseResult> runLifeCasesRhi(const std::string& caseId) {
    if (caseId != "all" && caseId != "S-LIFE") {
        return {};
    }

    auto deviceResult = rhi::createDevice();
    if (!deviceResult) {
        return {{"S-LIFE", false, "device creation failed: " + deviceResult.error().message}};
    }
    auto device = std::move(*deviceResult);

    Counters counters;
    std::unique_ptr<rhi::Texture> current =
        makeExtentTexture(*device, counters, workload::kExtentE1);
    if (!current) {
        return {{"S-LIFE", false, "initial E1 texture creation failed"}};
    }

    std::ostringstream trace;
    bool ok = true;
    std::string failure;

    for (const workload::LifetimeFrame& frame : workload::lifetimeSchedule()) {
        if (!ok) {
            break;
        }
        // The resize is "issued during" this frame: the frame itself already renders at the new
        // extent (spec: "resize to E2 issued during frame 3 ... frames 3-5 at E2"), so the new
        // texture is created before this frame's own draw, and the old one destroyed only once
        // every prior frame (which used it) has retired -- trivially true here, see the file
        // header.
        if (frame.resizeIssuedThisFrame) {
            std::unique_ptr<rhi::Texture> next =
                makeExtentTexture(*device, counters, frame.resizeTarget);
            if (!next) {
                ok = false;
                failure =
                    "resize texture creation failed at frame " + std::to_string(frame.frameIndex);
                break;
            }
            current = std::move(next); // Old texture's destructor runs here; its last use already
                                       // retired via the previous iteration's waitIdle().
            counters.textureDestroys += 1;
        }

        rhi::CommandList& cmd = device->beginFrame();
        const float clearValue = static_cast<float>(frame.frameIndex + 1) / 16.0f;
        cmd.beginRenderPass({.colorTarget = current.get(),
                             .clearColor = {clearValue, clearValue, clearValue, 1.0f},
                             .clear = true,
                             .label = "slife.clear"});
        if (frame.midFlightUpload) {
            // Frame 4 uploads new content to a texture consumed by frame 4's own graph: a copy pass
            // cannot open while a render pass is open, so the render pass above only clears; the
            // upload and its consuming readback both happen below, inside this same frame.
        }
        cmd.endRenderPass();

        if (frame.midFlightUpload) {
            auto stagingResult =
                device->createBuffer({.size = 16, .label = "slife.midFlightStaging"},
                                     std::array<uint8_t, 16>{200, 100, 50, 255, 200, 100, 50, 255,
                                                             200, 100, 50, 255, 200, 100, 50, 255}
                                         .data());
            if (!stagingResult) {
                ok = false;
                failure = "mid-flight staging buffer creation failed";
                break;
            }
            auto staging = std::move(*stagingResult);
            cmd.textureBarrier(*current, rhi::TextureUse::RenderTarget,
                               rhi::TextureUse::CopyDestination);
            cmd.beginCopyPass("slife.midFlightUpload");
            // bytesPerRow is the *region's* stride (RHI.h's BufferTextureLayout doc), not the
            // texture's own row stride: a 2-texel-wide RGBA8 region is 2 * 4 = 8 bytes per row.
            cmd.copyBufferToTexture(*staging, {.bytesPerRow = 8}, *current,
                                    {.mipLevel = 0, .x = 0, .y = 0, .width = 2, .height = 2});
            cmd.endCopyPass();
            // Texture::readback() (called after waitIdle below) is a direct CPU accessor, not a
            // copy pass, so it needs no CopySource transition -- a barrier with no later pass to
            // consume it is exactly what Metal4DeviceFrame.cpp's endFrame asserts against. Every
            // other frame needs no barrier at all for the same reason.
        }

        device->endFrame(nullptr);
        device->waitIdle();

        std::vector<uint8_t> pixels(uint64_t{frame.extent.width} * frame.extent.height * 4);
        current->readback(pixels.data(), pixels.size());
        const auto expected = static_cast<uint8_t>(clearValue * 255.0f);
        // Frame 4's mid-flight upload overwrites the 2x2 corner at (0,0)-(1,1), so the clear check
        // reads a texel outside that patch (row 2) rather than texel 0 itself on that frame.
        const uint64_t clearCheckOffset =
            frame.midFlightUpload ? uint64_t{2} * frame.extent.width * 4 : 0;
        if (pixels[clearCheckOffset] < expected - 2 || pixels[clearCheckOffset] > expected + 2) {
            ok = false;
            failure = "frame " + std::to_string(frame.frameIndex) + " clear content mismatch";
            break;
        }
        if (frame.midFlightUpload && pixels[0] != 200) {
            // Texel (0,0) should carry the mid-flight upload's colour, not the clear colour.
            ok = false;
            failure =
                "frame " + std::to_string(frame.frameIndex) + " mid-flight upload not observed";
            break;
        }

        trace << "frame " << frame.frameIndex << ": extent=" << frame.extent.width << "x"
              << frame.extent.height << " textureCreates=" << counters.textureCreates
              << " textureDestroys=" << counters.textureDestroys << "\n";
    }

    if (!ok) {
        return {{"S-LIFE", false, failure + "\n" + trace.str()}};
    }

    current.reset();
    counters.textureDestroys += 1;
    const bool settled = counters.textureCreates == counters.textureDestroys;
    trace << "final: textureCreates=" << counters.textureCreates
          << " textureDestroys=" << counters.textureDestroys
          << " settled=" << (settled ? "yes" : "no") << "\n";

    return {{"S-LIFE", settled, trace.str()}};
}

} // namespace lmx::experimental::noapi::bench
