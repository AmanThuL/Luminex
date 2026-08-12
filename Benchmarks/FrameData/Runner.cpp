//----------------------------------------------------------------------------------------------------------------------
/// @file Runner.cpp
/// @brief Implements the timed-region runner for one FrameDataBench workload.
//----------------------------------------------------------------------------------------------------------------------
#include "Runner.h"

#include "DeliverPerDrawData.h"
#include "Digest.h"

#include "RHI/RHI.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace lmx::bench {
namespace {

/// The leading, shader-read fields of every per-draw block, static or dynamic (must match
/// Shaders/FrameDataQuad.slang's QuadParams layout exactly). A dynamic workload's larger block
/// carries this struct followed by unread padding out to its frozen size.
struct QuadParams {
    float offset[2] = {0.0f, 0.0f};     ///< NDC-space quad center.
    float halfExtent[2] = {0.0f, 0.0f}; ///< NDC-space quad half-size.
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Solid quad colour.
};
static_assert(sizeof(QuadParams) == 32,
             "must match Shaders/FrameDataQuad.slang's QuadParams layout");

constexpr uint32_t kBufferSlot = 0;
constexpr uint32_t kCellPixels = 8;

//======================================================================================================================
uint32_t gridWidthFor(uint32_t drawCount) {
    return static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(drawCount))));
}

//======================================================================================================================
// Fast, dependency-free avalanche of (frame, draw) into 64 bits -- a local splitmix64-style
// finalizer, not shared with or copied from Experiments/NoApi, whose per-draw content must stay a
// deterministic function of frame and draw index so it cannot be hoisted out of the timed region.
uint64_t hashFrameDraw(uint32_t frame, uint32_t draw) {
    uint64_t x = (uint64_t{frame} << 32) ^ uint64_t{draw};
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

} // namespace

//======================================================================================================================
RunResult runWorkload(const WorkloadSpec& spec, const RunConfig& config) {
    RunResult result;

    auto deviceResult = rhi::createDevice({.enableValidation = false});
    if (!deviceResult) {
        result.error = "device creation failed: " + deviceResult.error().message;
        return result;
    }
    std::unique_ptr<rhi::Device> device = std::move(*deviceResult);

    auto libraryResult = device->loadShaderLibrary("Shaders/FrameDataQuad");
    if (!libraryResult) {
        result.error = "FrameDataQuad shader load failed: " + libraryResult.error().message;
        return result;
    }
    std::unique_ptr<rhi::ShaderLibrary> library = std::move(*libraryResult);

    auto pipelineResult =
        device->createGraphicsPipeline({.library = library.get(),
                                        .vertexEntry = "vertexMain",
                                        .fragmentEntry = "fragmentMain",
                                        .colorFormat = rhi::Format::RGBA8Unorm,
                                        .fillMode = rhi::FillMode::Solid,
                                        .cullMode = rhi::CullMode::None,
                                        .label = "framedatabench.pipeline"});
    if (!pipelineResult) {
        result.error = "pipeline creation failed: " + pipelineResult.error().message;
        return result;
    }
    std::unique_ptr<rhi::GraphicsPipeline> pipeline = std::move(*pipelineResult);

    const GridLayout layout(spec.drawCount);
    auto targetResult = device->createTexture({.width = layout.targetWidth,
                                               .height = layout.targetHeight,
                                               .format = rhi::Format::RGBA8Unorm,
                                               .renderTarget = true,
                                               .cpuReadback = true,
                                               .label = "framedatabench.target"});
    if (!targetResult) {
        result.error = "target texture creation failed: " + targetResult.error().message;
        return result;
    }
    std::unique_ptr<rhi::Texture> target = std::move(*targetResult);

    // Static workloads bind already-created, reusable buffers and deliver zero per-frame frame-data
    // bytes; their one-time content never varies with frame, so it is computed once here.
    std::vector<std::unique_ptr<rhi::Buffer>> staticBuffers;
    std::vector<std::byte> dynamicBlock;
    if (spec.kind == WorkloadKind::Static) {
        staticBuffers.reserve(spec.drawCount);
        for (uint32_t draw = 0; draw < spec.drawCount; ++draw) {
            const QuadParams params = quadParamsFor(layout, 0, draw);
            auto bufferResult = device->createBuffer(
                {.size = sizeof(params), .label = "framedatabench.staticParams"}, &params);
            if (!bufferResult) {
                result.error = "static params buffer creation failed: " + bufferResult.error().message;
                return result;
            }
            staticBuffers.push_back(std::move(*bufferResult));
        }
    } else {
        dynamicBlock.assign(spec.blockSize, std::byte{0});
    }

    const uint32_t totalFrames = config.warmupFrames + config.measuredFrames;
    result.perFrameTimedRegionNs.reserve(config.measuredFrames);

    for (uint32_t frame = 0; frame < totalFrames; ++frame) {
        // ---- BEGIN TIMED REGION (spec section 11): pacing wait, arena/ring reset, allocation,
        // copy, binding, command encoding, and submit. Excludes the untimed waitIdle() below, which
        // only proves the frame retired before the next iteration reuses its slot -- exactly the
        // boundary Experiments/NoApi's own M5.1 harness times its RHI side against.
        const auto start = std::chrono::steady_clock::now();
        rhi::CommandList& commands = device->beginFrame();
        commands.beginRenderPass(
            {.colorTarget = target.get(), .clear = true, .label = "framedatabench.draws"});
        commands.bindPipeline(*pipeline);
        if (spec.kind == WorkloadKind::Static) {
            for (uint32_t draw = 0; draw < spec.drawCount; ++draw) {
                commands.bindBuffer(kBufferSlot, *staticBuffers[draw]);
                commands.draw(6);
            }
        } else {
            for (uint32_t draw = 0; draw < spec.drawCount; ++draw) {
                const QuadParams params = quadParamsFor(layout, frame, draw);
                std::memcpy(dynamicBlock.data(), &params, sizeof(params));
                deliverPerDrawData(commands, kBufferSlot, dynamicBlock.data(), dynamicBlock.size());
                commands.draw(6);
            }
        }
        commands.endRenderPass();
        device->endFrame(nullptr);
        const auto end = std::chrono::steady_clock::now();
        // ---- END TIMED REGION

        device->waitIdle();
        if (frame >= config.warmupFrames) {
            result.perFrameTimedRegionNs.push_back(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        }
    }

    result.medianNs = medianOf(result.perFrameTimedRegionNs);

    if (config.verify) {
        std::vector<uint8_t> pixels(uint64_t{layout.targetWidth} * layout.targetHeight * 4);
        target->readback(pixels.data(), pixels.size());
        result.digest =
            fnv1a64(std::as_bytes(std::span<const uint8_t>(pixels.data(), pixels.size())));
    }

    result.ok = true;
    return result;
}

} // namespace lmx::bench
