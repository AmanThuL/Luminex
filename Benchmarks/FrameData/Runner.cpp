//----------------------------------------------------------------------------------------------------------------------
/// @file Runner.cpp
/// @brief Implements the timed-region runner for one FrameDataBench workload.
//----------------------------------------------------------------------------------------------------------------------
#include "Runner.h"

#include "DeliverPerDrawData.h"
#include "Digest.h"

#include "Core/IO/File.h"
#include <rojoRHI/RHI.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lmx::bench {
namespace {

/// The leading, shader-read fields of every per-draw block, static or dynamic (must match
/// Shaders/Tests/FrameDataQuad.slang's QuadParams layout exactly). A dynamic workload's larger
/// block carries this struct followed by unread padding out to its frozen size.
struct QuadParams {
    float offset[2] = {0.0f, 0.0f};           ///< NDC-space quad center.
    float halfExtent[2] = {0.0f, 0.0f};       ///< NDC-space quad half-size.
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Solid quad colour.
};
static_assert(sizeof(QuadParams) == 32,
              "must match Shaders/Tests/FrameDataQuad.slang's QuadParams layout");

constexpr uint32_t kCellPixels = 8;
constexpr uint32_t kBytesPerPixel = 4;       // RGBA8Unorm.
constexpr uint64_t kRowPitchAlignment = 256; // VU-rojo-cmd-0035.
constexpr rojoRHI::SubresourceRange kTargetRange{0, 1, 0, 1};

//======================================================================================================================
uint32_t gridWidthFor(uint32_t drawCount) {
    return static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(drawCount))));
}

//======================================================================================================================
// Fast, dependency-free avalanche of (frame, draw) into 64 bits -- a local splitmix64-style
// finalizer, not shared with or copied from the archived NoApi prototype, whose per-draw content
// must stay a deterministic function of frame and draw index so it cannot be hoisted out of the
// timed region.
uint64_t hashFrameDraw(uint32_t frame, uint32_t draw) {
    uint64_t x = (uint64_t{frame} << 32) ^ uint64_t { draw };
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

//======================================================================================================================
// Maps 24 bits of a hash to a float in [0, 1).
float unitFloat(uint64_t hash, uint32_t shift) {
    return static_cast<float>((hash >> shift) & 0xFFFFFFULL) / static_cast<float>(1u << 24);
}

//======================================================================================================================
uint64_t medianOf(std::vector<uint64_t> values) {
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    return values.size() % 2 == 1 ? values[mid] : (values[mid - 1] + values[mid]) / 2;
}

/// Disjoint grid placement shared by every draw index: cell (col, row) = (draw % width, draw /
/// width), tiling a `gridWidth` x `gridHeight` render target of kCellPixels-square cells. NDC y=+1
/// is the top of the viewport, so grid row 0 maps to the +y half, matching a row-major readback.
struct GridLayout {
    uint32_t gridWidth = 0;
    uint32_t gridHeight = 0;
    uint32_t targetWidth = 0;
    uint32_t targetHeight = 0;
    float halfExtent[2] = {0.0f, 0.0f};

    //==================================================================================================================
    explicit GridLayout(uint32_t drawCount)
        : gridWidth(gridWidthFor(drawCount)),
          gridHeight((drawCount + gridWidthFor(drawCount) - 1) / gridWidthFor(drawCount)),
          targetWidth(gridWidth * kCellPixels), targetHeight(gridHeight * kCellPixels) {
        halfExtent[0] = 1.0f / static_cast<float>(gridWidth);
        halfExtent[1] = 1.0f / static_cast<float>(gridHeight);
    }

    //==================================================================================================================
    void centerOf(uint32_t draw, float outOffset[2]) const {
        const uint32_t col = draw % gridWidth;
        const uint32_t row = draw / gridWidth;
        outOffset[0] = -1.0f + 2.0f * halfExtent[0] * (static_cast<float>(col) + 0.5f);
        outOffset[1] = 1.0f - 2.0f * halfExtent[1] * (static_cast<float>(row) + 0.5f);
    }
};

//======================================================================================================================
// Fills the leading QuadParams of a per-draw block with this draw's fixed grid placement and a
// tint that is a deterministic function of (frame, draw) -- fixed (frame 0) for a static workload's
// one-time setup, re-derived every frame for a dynamic one.
QuadParams quadParamsFor(const GridLayout& layout, uint32_t frame, uint32_t draw) {
    QuadParams params;
    layout.centerOf(draw, params.offset);
    params.halfExtent[0] = layout.halfExtent[0];
    params.halfExtent[1] = layout.halfExtent[1];
    const uint64_t hash = hashFrameDraw(frame, draw);
    params.tint[0] = unitFloat(hash, 0);
    params.tint[1] = unitFloat(hash, 13);
    params.tint[2] = unitFloat(hash, 26);
    params.tint[3] = 1.0f;
    return params;
}

/// Owns the run's device: every object the run creates is released with it, on every return path.
struct DeviceOwner {
    rojoRHI::Device device;

    //==================================================================================================================
    explicit DeviceOwner(rojoRHI::Device owned) : device(owned) {}

    //==================================================================================================================
    DeviceOwner(const DeviceOwner&) = delete;

    //==================================================================================================================
    DeviceOwner& operator=(const DeviceOwner&) = delete;

    //==================================================================================================================
    ~DeviceOwner() { rojoRHI::destroyDevice(device); }
};

/// The one graphics queue and its fence: each submission signals the next value, and `wait`
/// blocks until the GPU reaches it. Waits are never inside a timed region.
struct Submitter {
    rojoRHI::Device device;
    rojoRHI::Queue queue;
    rojoRHI::Fence fence;
    uint64_t value = 0;

    //==================================================================================================================
    void submit(rojoRHI::CommandList& commands) {
        commands.end();
        const rojoRHI::FenceValue signal{fence, ++value};
        queue.submit(std::span(&commands, 1), {}, std::span(&signal, 1));
    }

    //==================================================================================================================
    bool wait() { return device.wait({fence, value}, std::numeric_limits<uint64_t>::max()); }
};

//======================================================================================================================
// The pass's color attachment barrier: the first frame discards the target's undefined contents,
// every later one orders after the previous frame's attachment writes.
rojoRHI::TextureBarrier toColorAttachment(rojoRHI::Texture target, bool written) {
    rojoRHI::TextureBarrier barrier{.texture = target,
                                    .range = kTargetRange,
                                    .dstStage = rojoRHI::Stage::ColorAttachment,
                                    .dstAccess = rojoRHI::Access::ColorAttachmentWrite,
                                    .newLayout = rojoRHI::Layout::ColorAttachment};
    if (written) {
        barrier.srcStage = rojoRHI::Stage::ColorAttachment;
        barrier.srcAccess = rojoRHI::Access::ColorAttachmentWrite;
        barrier.oldLayout = rojoRHI::Layout::ColorAttachment;
    }
    return barrier;
}

//======================================================================================================================
// Copies the target, last written as a color attachment, into a readback buffer in one untimed
// frame, waits for it, and digests the rows tightly packed -- the byte layout the v1 harness read
// back, so digests compare across the two.
std::optional<std::string> readbackDigest(rojoRHI::Device& device, Submitter& submitter,
                                          rojoRHI::Texture target, const GridLayout& layout,
                                          uint64_t& digest) {
    const uint64_t packedRow = uint64_t{layout.targetWidth} * kBytesPerPixel;
    const uint64_t rowPitch =
        (packedRow + kRowPitchAlignment - 1) / kRowPitchAlignment * kRowPitchAlignment;
    auto staging = device.createBuffer({.size = rowPitch * layout.targetHeight,
                                        .memory = rojoRHI::Memory::Readback,
                                        .label = "framedatabench.readback"});
    if (!staging) {
        return "readback buffer creation failed: " + staging.error().message;
    }

    device.beginFrame();
    rojoRHI::CommandList commands =
        device.acquireCommandList(rojoRHI::QueueType::Graphics, "framedatabench.readback");
    const rojoRHI::TextureBarrier toCopy{.texture = target,
                                         .range = kTargetRange,
                                         .srcStage = rojoRHI::Stage::ColorAttachment,
                                         .srcAccess = rojoRHI::Access::ColorAttachmentWrite,
                                         .dstStage = rojoRHI::Stage::Copy,
                                         .dstAccess = rojoRHI::Access::CopyRead,
                                         .oldLayout = rojoRHI::Layout::ColorAttachment,
                                         .newLayout = rojoRHI::Layout::CopySrc};
    commands.barrier({}, {}, std::span(&toCopy, 1));
    commands.copyTextureToBuffer(target, {.extent = {layout.targetWidth, layout.targetHeight, 1}},
                                 *staging,
                                 {.offset = 0, .rowPitch = static_cast<uint32_t>(rowPitch)});
    submitter.submit(commands);
    device.endFrame();
    if (!submitter.wait()) {
        return "readback fence wait failed";
    }

    const std::byte* mapped = device.cpuPointer(*staging);
    std::vector<std::byte> pixels(packedRow * layout.targetHeight);
    for (uint32_t row = 0; row < layout.targetHeight; ++row) {
        std::memcpy(pixels.data() + row * packedRow, mapped + row * rowPitch, packedRow);
    }
    digest = fnv1a64(pixels);
    return std::nullopt;
}

} // namespace

//======================================================================================================================
RunResult runWorkload(const WorkloadSpec& spec, const RunConfig& config) {
    RunResult result;

    auto deviceResult = rojoRHI::createDevice({.label = "framedatabench.device"});
    if (!deviceResult) {
        result.error = "device creation failed: " + deviceResult.error().message;
        return result;
    }
    const DeviceOwner owner{*deviceResult};
    rojoRHI::Device device = owner.device;

    const std::string libraryPath =
        std::string("Shaders/FrameDataQuad") + LMX_SHADER_LIBRARY_EXTENSION;
    const auto libraryBytes = readWholeFile(libraryPath);
    if (!libraryBytes || libraryBytes->empty()) {
        result.error = "cannot read shader library '" + libraryPath + "'";
        return result;
    }
    auto library = device.loadShaderLibrary(*libraryBytes, "FrameDataQuad");
    if (!library) {
        result.error = "FrameDataQuad shader load failed: " + library.error().message;
        return result;
    }

    const rojoRHI::ColorTarget color{.format = rojoRHI::Format::RGBA8Unorm};
    auto pipeline = device.createPipeline(
        {.vertex = {*library, "vertexMain"},
         .fragment = {*library, "fragmentMain"},
         .raster = {.cull = rojoRHI::CullMode::None, .fill = rojoRHI::FillMode::Solid},
         .colors = std::span(&color, 1),
         .label = "framedatabench.pipeline"});
    if (!pipeline) {
        result.error = "pipeline creation failed: " + pipeline.error().message;
        return result;
    }

    const GridLayout layout(spec.drawCount);
    auto target = device.createTexture({.format = rojoRHI::Format::RGBA8Unorm,
                                        .extent = {layout.targetWidth, layout.targetHeight, 1},
                                        .usage = rojoRHI::TextureUsage::RenderTarget,
                                        .label = "framedatabench.target"});
    if (!target) {
        result.error = "target texture creation failed: " + target.error().message;
        return result;
    }

    auto fence = device.createFence(0, "framedatabench.fence");
    if (!fence) {
        result.error = "fence creation failed: " + fence.error().message;
        return result;
    }
    Submitter submitter{device, device.queue(rojoRHI::QueueType::Graphics), *fence};

    // Static workloads address already-created, reusable buffers and upload no per-frame parameter
    // bytes, only each draw's root; their one-time content never varies with frame, so it is
    // computed once here.
    std::vector<rojoRHI::Addr> staticParams;
    std::vector<std::byte> dynamicBlock;
    if (spec.kind == WorkloadKind::Static) {
        staticParams.reserve(spec.drawCount);
        for (uint32_t draw = 0; draw < spec.drawCount; ++draw) {
            const QuadParams params = quadParamsFor(layout, 0, draw);
            auto buffer = device.createBuffer(
                {.size = sizeof(params), .label = "framedatabench.staticParams"},
                std::as_bytes(std::span(&params, 1)));
            if (!buffer) {
                result.error = "static params buffer creation failed: " + buffer.error().message;
                return result;
            }
            staticParams.push_back(device.address(*buffer));
        }
    } else {
        dynamicBlock.assign(spec.blockSize, std::byte{0});
    }

    const uint32_t totalFrames = config.warmupFrames + config.measuredFrames;
    result.perFrameTimedRegionNs.reserve(config.measuredFrames);
    const rojoRHI::ColorAttachment attachment{.texture = *target,
                                              .load = rojoRHI::LoadOp::Clear,
                                              .store = rojoRHI::StoreOp::Store,
                                              .clear = {0.0f, 0.0f, 0.0f, 1.0f}};
    const rojoRHI::Rect2D area{0, 0, layout.targetWidth, layout.targetHeight};
    const rojoRHI::Viewport viewport{.width = static_cast<float>(layout.targetWidth),
                                     .height = static_cast<float>(layout.targetHeight)};
    bool targetWritten = false;

    for (uint32_t frame = 0; frame < totalFrames; ++frame) {
        // ---- BEGIN TIMED REGION: the clock starts immediately before Device::beginFrame(), so
        // the region includes everything that call does internally -- the pacing wait and the
        // recycling of the slot's command allocators and frame arena -- then recording, the
        // per-draw frame-data uploads and root pushes, submission and Device::endFrame(). This is
        // the boundary the v1 harness timed, so the two compare on identical terms. Excludes the
        // untimed fence wait below, which only proves the frame retired before the next iteration
        // records.
        const auto start = std::chrono::steady_clock::now();
        device.beginFrame();
        rojoRHI::CommandList commands =
            device.acquireCommandList(rojoRHI::QueueType::Graphics, "framedatabench.frame");
        const rojoRHI::TextureBarrier barrier = toColorAttachment(*target, targetWritten);
        commands.barrier({}, {}, std::span(&barrier, 1));
        commands.beginRenderPass(
            {.colors = std::span(&attachment, 1), .area = area, .label = "framedatabench.draws"});
        commands.setPipeline(*pipeline);
        commands.setViewport(viewport);
        commands.setScissor(area);
        if (spec.kind == WorkloadKind::Static) {
            for (uint32_t draw = 0; draw < spec.drawCount; ++draw) {
                commands.pushRoot(FrameDataQuadRoot{.params = staticParams[draw]});
                commands.draw(6);
            }
        } else {
            for (uint32_t draw = 0; draw < spec.drawCount; ++draw) {
                const QuadParams params = quadParamsFor(layout, frame, draw);
                std::memcpy(dynamicBlock.data(), &params, sizeof(params));
                deliverPerDrawData(commands, dynamicBlock);
                commands.draw(6);
            }
        }
        commands.endRenderPass();
        submitter.submit(commands);
        device.endFrame();
        const auto end = std::chrono::steady_clock::now();
        // ---- END TIMED REGION
        targetWritten = true;

        if (!submitter.wait()) {
            result.error = "frame fence wait failed";
            return result;
        }
        if (frame >= config.warmupFrames) {
            result.perFrameTimedRegionNs.push_back(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        }
        // Snapshot immediately after warm-up retires (frame == config.warmupFrames - 1 just
        // completed), right after the fence wait rather than at the top of the next iteration, so
        // it reflects exactly "after warm-up, before the first measured frame's timed region".
        if (frame + 1 == config.warmupFrames) {
            result.countersAfterWarmup = device.counters();
        }
    }
    result.countersAfterMeasurement = device.counters();
    result.medianNs = medianOf(result.perFrameTimedRegionNs);

    if (config.verify && targetWritten) {
        uint64_t digest = 0;
        if (auto error = readbackDigest(device, submitter, *target, layout, digest)) {
            result.error = *error;
            return result;
        }
        result.digest = digest;
    }

    result.ok = true;
    return result;
}

//======================================================================================================================
// Defined in this translation unit (not a header) so it can reach the anonymous-namespace helpers
// above directly, exercising the exact functions runWorkload() itself uses rather than a
// reimplementation of them.
bool runSelfTests(std::vector<std::string>& failures) {
    const auto check = [&](bool condition, std::string what) {
        if (!condition) {
            failures.push_back(std::move(what));
        }
    };

    // medianOf: odd count, even count, single element, empty.
    check(medianOf({5}) == 5, "medianOf({5}) == 5");
    check(medianOf({1, 2, 3}) == 2, "medianOf({1,2,3}) == 2 (odd count)");
    check(medianOf({2, 4}) == 3, "medianOf({2,4}) == 3 (even count, averaged)");
    check(medianOf({}) == 0, "medianOf({}) == 0 (empty)");

    // The frozen workload table (spec section 11): name, draw count, kind, and block size for
    // every one of the five cases, in table order, plus name lookup.
    check(sizeof(kWorkloads) / sizeof(kWorkloads[0]) == 5, "kWorkloads has exactly 5 entries");
    check(kWorkloads[0].name == "F-FIT-512" && kWorkloads[0].drawCount == 512 &&
              kWorkloads[0].kind == WorkloadKind::Dynamic && kWorkloads[0].blockSize == 64,
          "kWorkloads[0] == {F-FIT-512, 512, Dynamic, 64}");
    check(kWorkloads[1].name == "F-DYNAMIC-1024" && kWorkloads[1].drawCount == 1024 &&
              kWorkloads[1].kind == WorkloadKind::Dynamic && kWorkloads[1].blockSize == 304,
          "kWorkloads[1] == {F-DYNAMIC-1024, 1024, Dynamic, 304}");
    check(kWorkloads[2].name == "F-DYNAMIC-4096" && kWorkloads[2].drawCount == 4096 &&
              kWorkloads[2].kind == WorkloadKind::Dynamic && kWorkloads[2].blockSize == 64,
          "kWorkloads[2] == {F-DYNAMIC-4096, 4096, Dynamic, 64}");
    check(kWorkloads[3].name == "F-STATIC-1024" && kWorkloads[3].drawCount == 1024 &&
              kWorkloads[3].kind == WorkloadKind::Static && kWorkloads[3].blockSize == 0,
          "kWorkloads[3] == {F-STATIC-1024, 1024, Static, 0}");
    check(kWorkloads[4].name == "F-STATIC-4096" && kWorkloads[4].drawCount == 4096 &&
              kWorkloads[4].kind == WorkloadKind::Static && kWorkloads[4].blockSize == 0,
          "kWorkloads[4] == {F-STATIC-4096, 4096, Static, 0}");
    check(findWorkload("F-FIT-512") == &kWorkloads[0],
          "findWorkload(\"F-FIT-512\") finds kWorkloads[0]");
    check(findWorkload("not-a-case") == nullptr, "findWorkload(\"not-a-case\") == nullptr");

    // GridLayout: a non-square draw count (512, per M7/M9's own example) and a square one (1024),
    // pinning the disjoint-grid math every workload's placement depends on.
    const GridLayout layout512(512);
    check(layout512.gridWidth == 23, "GridLayout(512).gridWidth == 23 (ceil(sqrt(512)))");
    check(layout512.gridHeight == 23, "GridLayout(512).gridHeight == 23 (ceil(512 / 23))");
    check(layout512.targetWidth == 23 * kCellPixels,
          "GridLayout(512).targetWidth == 23 * kCellPixels");
    check(layout512.targetHeight == 23 * kCellPixels,
          "GridLayout(512).targetHeight == 23 * kCellPixels");
    const GridLayout layout1024(1024);
    check(layout1024.gridWidth == 32 && layout1024.gridHeight == 32,
          "GridLayout(1024) == 32x32 (exact square)");

    return failures.empty();
}

} // namespace lmx::bench
