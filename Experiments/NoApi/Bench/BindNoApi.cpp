//----------------------------------------------------------------------------------------------------------------------
/// @file BindNoApi.cpp
/// @brief Implements BindNoApi for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Implements S-BIND (spec section 7) against the address-first prototype. See
/// BindRhi.cpp's
///        header comment for the shared grid-tiling design and setup/per-frame split; this file
///        realises the same case through bindless slot writes and root-data pushes instead of
///        CommandList::bindTexture/setUniforms.
///
///        Setup/per-frame split: device, table, residency, the 256 source textures and their
///        upload, the target texture, the pipeline, and the sampler are all created once, before
///        the loop. The per-`drawCount` loop's body -- one writeTextureSlot, one pushRoot, and one
///        draw call per draw -- is the per-frame encode path; writeTextureSlot is this model's own
///        answer to a per-draw texture bind, and it is exactly as timed as CommandList::bindTexture
///        is on the RHI side (spec section 8: "binding and root-data work cannot be moved outside
///        the timed region").

#include "Bench/StressCommon.h"
#include "Bench/StressShaders.h"
#include "Workload/Splitmix64.h"
#include "Workload/StressCases.h"

#include "NoApi/NoApi.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace lmx::experimental::noapi::bench {
namespace {

constexpr uint32_t kSamplerSlot = 0;
// Texture slots 1..256 hold the 256 source textures; slot 0 is the sampler's own array position
// (a texture handle and a sampler handle occupy the same flat table, so the two never collide since
// writeTextureSlot/writeSamplerSlot address disjoint ranges here by construction).
constexpr uint32_t kFirstTextureSlot = 1;

//======================================================================================================================
CaseResult runBindScaleNoApi(uint32_t drawCount) {
    const std::string id = "S-BIND-" + std::to_string(drawCount);

    Result<Device*> deviceResult = createDevice({.label = "sbind"});
    if (!deviceResult) {
        return {id, false, "device creation failed"};
    }
    Device* device = *deviceResult;
    Queue* queue = mainQueue(device);

    Result<ResidencySet*> residencyResult =
        createResidencySet(device, {.initialCapacity = 260, .label = "sbind.residency"});
    Result<BindlessTable*> tableResult = createBindlessTable(
        device, {.slotCount = workload::kBindTextureCount + 4, .label = "sbind.table"});
    Result<Semaphore*> fenceResult = createSemaphore(device, 0, "sbind.fence");
    if (!residencyResult || !tableResult || !fenceResult) {
        return {id, false, "harness object creation failed"};
    }
    ResidencySet* residency = *residencyResult;
    BindlessTable* table = *tableResult;
    Semaphore* fence = *fenceResult;

    Result<Allocation> rootStorageResult = allocate(
        device,
        {.size = 1024 * 1024, .alignment = 256, .kind = MemoryKind::Shared, .label = "sbind.root"});
    // 256 textures x 64x64x4 bytes = 4 MiB of staging content alone; rounded up generously since
    // per-texture alignment (16 bytes) adds negligible overhead.
    Result<Allocation> uploadStorageResult = allocate(device, {.size = 6 * 1024 * 1024,
                                                               .alignment = 256,
                                                               .kind = MemoryKind::Shared,
                                                               .label = "sbind.upload"});
    Result<Allocation> textureMemResult = allocate(device, {.size = 8 * 1024 * 1024,
                                                            .alignment = 65536,
                                                            .kind = MemoryKind::Private,
                                                            .label = "sbind.textureMem"});
    Result<Allocation> readbackResult = allocate(
        device, {.size = uint64_t{workload::kBindTargetWidth} * workload::kBindTargetHeight * 4,
                 .alignment = 256,
                 .kind = MemoryKind::Readback,
                 .label = "sbind.readback"});
    if (!rootStorageResult || !uploadStorageResult || !textureMemResult || !readbackResult) {
        return {id, false, "memory allocation failed"};
    }
    LinearAllocator root(*rootStorageResult);
    LinearAllocator upload(*uploadStorageResult);
    uint64_t textureCursor = 0;

    const auto makeTexture = [&](const TextureDesc& desc) -> Texture* {
        const SizeAlign required = textureSizeAlign(device, desc);
        const uint64_t aligned =
            (textureCursor + required.alignment - 1) & ~(required.alignment - 1);
        Result<Texture*> t = createTexture(device, desc, textureMemResult->gpu + aligned);
        textureCursor = aligned + required.size;
        return *t;
    };

    std::vector<Texture*> textures(workload::kBindTextureCount);
    std::vector<std::array<uint8_t, 4>> colors(workload::kBindTextureCount);
    std::vector<uint8_t> pixelBuffer(uint64_t{workload::kBindTextureSize} *
                                     workload::kBindTextureSize * 4);

    CommandBuffer* setupCommands = beginCommands(queue, &upload, "sbind.setup");
    for (uint32_t t = 0; t < workload::kBindTextureCount; ++t) {
        const uint64_t draw = workload::splitmix64(workload::kSeed, {5000, t});
        workload::unitRgba8(draw, colors[t].data());
        colors[t][3] = 255;
        for (uint32_t texel = 0; texel < workload::kBindTextureSize * workload::kBindTextureSize;
             ++texel) {
            std::memcpy(pixelBuffer.data() + uint64_t{texel} * 4, colors[t].data(), 4);
        }
        textures[t] =
            makeTexture({.kind = TextureKind::Texture2D,
                         .extent = {workload::kBindTextureSize, workload::kBindTextureSize, 1},
                         .format = Format::RGBA8Unorm,
                         .usage = TextureUsage::Sampled | TextureUsage::CopyDestination,
                         .label = "sbind.source"});
        const Suballocation staging = upload.allocate(pixelBuffer.size(), 256);
        std::memcpy(staging.cpu, pixelBuffer.data(), pixelBuffer.size());
        copyToTexture(setupCommands, textures[t],
                      {.mipLevel = 0,
                       .origin = {},
                       .extent = {workload::kBindTextureSize, workload::kBindTextureSize, 1}},
                      staging.gpu, {.bytesPerRow = uint64_t{workload::kBindTextureSize} * 4});
        writeTextureSlot(table, kFirstTextureSlot + t, textures[t], {});
    }

    Texture* target =
        makeTexture({.kind = TextureKind::Texture2D,
                     .extent = {workload::kBindTargetWidth, workload::kBindTargetHeight, 1},
                     .format = Format::RGBA8Unorm,
                     .usage = TextureUsage::ColorAttachment | TextureUsage::CopySource,
                     .label = "sbind.target"});

    Result<Sampler*> samplerResult = createSampler(device, {.minFilter = FilterMode::Nearest,
                                                            .magFilter = FilterMode::Nearest,
                                                            .mipFilter = FilterMode::Nearest,
                                                            .addressU = AddressMode::ClampToEdge,
                                                            .addressV = AddressMode::ClampToEdge,
                                                            .label = "sbind.sampler"});
    if (!samplerResult) {
        return {id, false, "sampler creation failed"};
    }
    writeSamplerSlot(table, kSamplerSlot, *samplerResult);

    Result<Pipeline*> pipelineResult = createGraphicsPipeline(
        device, {.vertex = {.ir = stressShaderSource(), .entryPoint = "lmxQuadVs"},
                 .pixel = {.ir = stressShaderSource(), .entryPoint = "lmxQuadFs"},
                 .raster = {.topology = Topology::TriangleList,
                            .colorTargets = std::array<ColorTargetDesc, 1>{ColorTargetDesc{
                                .format = Format::RGBA8Unorm, .writeMask = 0xF}}},
                 .label = "sbind.pipeline"});
    if (!pipelineResult) {
        return {id, false, "pipeline creation failed"};
    }
    Pipeline* pipeline = *pipelineResult;

    commitResidency(residency);
    endCommands(setupCommands);
    const std::array<CommandBuffer*, 1> setupList{setupCommands};
    submit(queue, setupList, fence, 1);
    waitSemaphore(fence, 1);

    const uint32_t gridSize = static_cast<uint32_t>(std::lround(std::sqrt(double(drawCount))));
    const float cellNdc = 2.0f / static_cast<float>(gridSize);

    root.reset();
    CommandBuffer* commands = beginCommands(queue, &root, "sbind.draws");
    setBindlessTable(commands, table);
    const std::array<ColorAttachment, 1> colorTargets{ColorAttachment{.texture = target,
                                                                      .load = LoadAction::Clear,
                                                                      .store = StoreAction::Store,
                                                                      .clearColor = {0, 0, 0, 1}}};
    beginRenderPass(commands, {.colorTargets = colorTargets, .label = "sbind.pass"});
    setPipeline(commands, pipeline);
    setViewport(commands, {.x = 0,
                           .y = 0,
                           .width = static_cast<float>(workload::kBindTargetWidth),
                           .height = static_cast<float>(workload::kBindTargetHeight)});
    setScissor(commands, {.x = 0,
                          .y = 0,
                          .width = workload::kBindTargetWidth,
                          .height = workload::kBindTargetHeight});
    setCullMode(commands, CullMode::None);

    for (uint32_t d = 0; d < drawCount; ++d) {
        const uint32_t col = d % gridSize;
        const uint32_t row = d / gridSize;
        const uint32_t textureIndex = workload::bindTextureIndexForDraw(d);
        // See BindRhi.cpp's comment: NDC y=+1 is the framebuffer's row 0, so grid row 0 maps there.
        const QuadRoot vertexRoot{.offset = {-1.0f + cellNdc * (static_cast<float>(col) + 0.5f),
                                             1.0f - cellNdc * (static_cast<float>(row) + 0.5f)},
                                  .halfExtent = {cellNdc * 0.5f, cellNdc * 0.5f}};
        const QuadPixelRoot pixelRoot{.textures = bindlessTableAddress(table),
                                      .samplers = bindlessTableAddress(table),
                                      .textureSlot = kFirstTextureSlot + textureIndex,
                                      .samplerSlot = kSamplerSlot,
                                      .tint = {1.0f, 1.0f, 1.0f, 1.0f}};
        const GpuAddress vertexAddress = pushRoot(commands, vertexRoot);
        const GpuAddress pixelAddress = pushRoot(commands, pixelRoot);
        draw(commands, vertexAddress, pixelAddress, 6);
    }
    endRenderPass(commands);
    barrier(commands, Stage::RasterColorOut, Stage::Copy);
    copyFromTexture(commands, readbackResult->gpu,
                    {.bytesPerRow = uint64_t{workload::kBindTargetWidth} * 4}, target,
                    {.mipLevel = 0,
                     .origin = {},
                     .extent = {workload::kBindTargetWidth, workload::kBindTargetHeight, 1}});
    endCommands(commands);
    const std::array<CommandBuffer*, 1> list{commands};
    submit(queue, list, fence, 2);
    waitSemaphore(fence, 2);

    const auto* actual = static_cast<const uint8_t*>(readbackResult->cpu);
    bool ok = true;
    std::string message;
    for (uint32_t d = 0; d < drawCount && ok; ++d) {
        const uint32_t col = d % gridSize;
        const uint32_t row = d / gridSize;
        const uint32_t cellPixels = workload::kBindTargetWidth / gridSize;
        const uint32_t px = col * cellPixels + cellPixels / 2;
        const uint32_t py = row * cellPixels + cellPixels / 2;
        const uint64_t offset = (uint64_t{py} * workload::kBindTargetWidth + px) * 4;
        const auto& expected = colors[workload::bindTextureIndexForDraw(d)];
        if (actual[offset] != expected[0] || actual[offset + 1] != expected[1] ||
            actual[offset + 2] != expected[2]) {
            ok = false;
            message =
                "draw " + std::to_string(d) + " cell centre does not match its texture's colour";
            if (std::getenv("LMX_NOAPI_HAZARD_DEBUG") != nullptr) {
                std::fprintf(
                    stderr,
                    "[debug] draw=%u px=%u py=%u expected=%02x%02x%02x actual=%02x%02x%02x\n", d,
                    px, py, expected[0], expected[1], expected[2], actual[offset],
                    actual[offset + 1], actual[offset + 2]);
            }
        }
    }

    for (uint32_t slot = 0; slot < workload::kBindTextureCount + 4; ++slot) {
        clearBindlessSlot(table, slot);
    }
    for (Texture* texture : textures) {
        destroyTexture(device, texture);
    }
    destroyTexture(device, target);
    destroyPipeline(device, pipeline);
    destroySampler(device, *samplerResult);
    destroyBindlessTable(device, table);
    destroySemaphore(device, fence);
    deallocate(device, *readbackResult);
    deallocate(device, *textureMemResult);
    deallocate(device, *uploadStorageResult);
    deallocate(device, *rootStorageResult);
    destroyResidencySet(device, residency);
    destroyDevice(device);

    return {id, ok, message};
}

} // namespace

//======================================================================================================================
std::vector<CaseResult> runBindCasesNoApi(const std::string& caseId) {
    std::vector<CaseResult> results;
    for (uint32_t drawCount : workload::kBindDrawCounts) {
        const std::string id = "S-BIND-" + std::to_string(drawCount);
        if (caseId != "all" && caseId != id) {
            continue;
        }
        results.push_back(runBindScaleNoApi(drawCount));
    }
    return results;
}

//======================================================================================================================
MeasuredRun measureBindScaleNoApi(uint32_t drawCount, uint32_t warmupFrames,
                                  uint32_t measuredFrames) {
    MeasuredRun result;

    Result<Device*> deviceResult = createDevice({.label = "sbind.measure"});
    if (!deviceResult) {
        result.error = "device creation failed";
        return result;
    }
    Device* device = *deviceResult;
    Queue* queue = mainQueue(device);

    Result<ResidencySet*> residencyResult =
        createResidencySet(device, {.initialCapacity = 260, .label = "sbind.measure.residency"});
    Result<BindlessTable*> tableResult = createBindlessTable(
        device, {.slotCount = workload::kBindTextureCount + 4, .label = "sbind.measure.table"});
    Result<Semaphore*> fenceResult = createSemaphore(device, 0, "sbind.measure.fence");
    if (!residencyResult || !tableResult || !fenceResult) {
        result.error = "harness object creation failed";
        return result;
    }
    ResidencySet* residency = *residencyResult;
    BindlessTable* table = *tableResult;
    Semaphore* fence = *fenceResult;

    Result<Allocation> rootStorageResult = allocate(
        device,
        {.size = 1024 * 1024, .alignment = 256, .kind = MemoryKind::Shared, .label = "sbind.root"});
    Result<Allocation> uploadStorageResult = allocate(device, {.size = 6 * 1024 * 1024,
                                                               .alignment = 256,
                                                               .kind = MemoryKind::Shared,
                                                               .label = "sbind.upload"});
    Result<Allocation> textureMemResult = allocate(device, {.size = 8 * 1024 * 1024,
                                                            .alignment = 65536,
                                                            .kind = MemoryKind::Private,
                                                            .label = "sbind.textureMem"});
    Result<Allocation> readbackResult = allocate(
        device, {.size = uint64_t{workload::kBindTargetWidth} * workload::kBindTargetHeight * 4,
                 .alignment = 256,
                 .kind = MemoryKind::Readback,
                 .label = "sbind.readback"});
    if (!rootStorageResult || !uploadStorageResult || !textureMemResult || !readbackResult) {
        result.error = "memory allocation failed";
        return result;
    }
    LinearAllocator root(*rootStorageResult);
    LinearAllocator upload(*uploadStorageResult);
    uint64_t textureCursor = 0;

    const auto makeTexture = [&](const TextureDesc& desc) -> Texture* {
        const SizeAlign required = textureSizeAlign(device, desc);
        const uint64_t aligned =
            (textureCursor + required.alignment - 1) & ~(required.alignment - 1);
        Result<Texture*> t = createTexture(device, desc, textureMemResult->gpu + aligned);
        textureCursor = aligned + required.size;
        return *t;
    };

    std::vector<Texture*> textures(workload::kBindTextureCount);
    std::vector<std::array<uint8_t, 4>> colors(workload::kBindTextureCount);
    std::vector<uint8_t> pixelBuffer(uint64_t{workload::kBindTextureSize} *
                                     workload::kBindTextureSize * 4);

    CommandBuffer* setupCommands = beginCommands(queue, &upload, "sbind.measure.setup");
    for (uint32_t t = 0; t < workload::kBindTextureCount; ++t) {
        const uint64_t draw = workload::splitmix64(workload::kSeed, {5000, t});
        workload::unitRgba8(draw, colors[t].data());
        colors[t][3] = 255;
        for (uint32_t texel = 0; texel < workload::kBindTextureSize * workload::kBindTextureSize;
             ++texel) {
            std::memcpy(pixelBuffer.data() + uint64_t{texel} * 4, colors[t].data(), 4);
        }
        textures[t] =
            makeTexture({.kind = TextureKind::Texture2D,
                         .extent = {workload::kBindTextureSize, workload::kBindTextureSize, 1},
                         .format = Format::RGBA8Unorm,
                         .usage = TextureUsage::Sampled | TextureUsage::CopyDestination,
                         .label = "sbind.source"});
        const Suballocation staging = upload.allocate(pixelBuffer.size(), 256);
        std::memcpy(staging.cpu, pixelBuffer.data(), pixelBuffer.size());
        copyToTexture(setupCommands, textures[t],
                      {.mipLevel = 0,
                       .origin = {},
                       .extent = {workload::kBindTextureSize, workload::kBindTextureSize, 1}},
                      staging.gpu, {.bytesPerRow = uint64_t{workload::kBindTextureSize} * 4});
        writeTextureSlot(table, kFirstTextureSlot + t, textures[t], {});
    }

    Texture* target =
        makeTexture({.kind = TextureKind::Texture2D,
                     .extent = {workload::kBindTargetWidth, workload::kBindTargetHeight, 1},
                     .format = Format::RGBA8Unorm,
                     .usage = TextureUsage::ColorAttachment | TextureUsage::CopySource,
                     .label = "sbind.target"});

    Result<Sampler*> samplerResult = createSampler(device, {.minFilter = FilterMode::Nearest,
                                                            .magFilter = FilterMode::Nearest,
                                                            .mipFilter = FilterMode::Nearest,
                                                            .addressU = AddressMode::ClampToEdge,
                                                            .addressV = AddressMode::ClampToEdge,
                                                            .label = "sbind.sampler"});
    if (!samplerResult) {
        result.error = "sampler creation failed";
        return result;
    }
    writeSamplerSlot(table, kSamplerSlot, *samplerResult);

    Result<Pipeline*> pipelineResult = createGraphicsPipeline(
        device, {.vertex = {.ir = stressShaderSource(), .entryPoint = "lmxQuadVs"},
                 .pixel = {.ir = stressShaderSource(), .entryPoint = "lmxQuadFs"},
                 .raster = {.topology = Topology::TriangleList,
                            .colorTargets = std::array<ColorTargetDesc, 1>{ColorTargetDesc{
                                .format = Format::RGBA8Unorm, .writeMask = 0xF}}},
                 .label = "sbind.pipeline"});
    if (!pipelineResult) {
        result.error = "pipeline creation failed";
        return result;
    }
    Pipeline* pipeline = *pipelineResult;

    commitResidency(residency);
    endCommands(setupCommands);
    const std::array<CommandBuffer*, 1> setupList{setupCommands};
    uint64_t fenceValue = 1;
    submit(queue, setupList, fence, fenceValue);
    waitSemaphore(fence, fenceValue);

    // Both byte totals are unscored descriptive evidence (Bench/Metrics.h); the Metal-reported
    // value remains prototype-only and is never compared with incumbent requested bytes.
    result.endOfSetup = {.textureCreateCalls = workload::kBindTextureCount + 1,
                         .bufferCreateCalls = 0,
                         .samplerCreateCalls = 1,
                         .pipelineCreateCalls = 1,
                         .requestedBytes = deviceCreationStats(device).requestedBytes,
                         .metalReportedBytes = residentBytes(residency)};
    const BindlessTableStats oneTimeTableStats = bindlessTableStats(table);

    const uint32_t gridSize = static_cast<uint32_t>(std::lround(std::sqrt(double(drawCount))));
    const float cellNdc = 2.0f / static_cast<float>(gridSize);

    const uint32_t totalFrames = warmupFrames + measuredFrames;
    result.perFrameTimedRegionNs.reserve(measuredFrames);
    bool countersEverSet = false;
    for (uint32_t frame = 0; frame < totalFrames; ++frame) {
        root.reset();

        // Begin timed region (spec section 8; same clock and boundary as measureBindScaleRhi and
        // the representative-graph adapters).
        const auto start = std::chrono::steady_clock::now();
        CommandBuffer* commands = beginCommands(queue, &root, "sbind.measure.draws");
        setBindlessTable(commands, table);
        const std::array<ColorAttachment, 1> colorTargets{
            ColorAttachment{.texture = target,
                            .load = LoadAction::Clear,
                            .store = StoreAction::Store,
                            .clearColor = {0, 0, 0, 1}}};
        beginRenderPass(commands, {.colorTargets = colorTargets, .label = "sbind.measure.pass"});
        setPipeline(commands, pipeline);
        setViewport(commands, {.x = 0,
                               .y = 0,
                               .width = static_cast<float>(workload::kBindTargetWidth),
                               .height = static_cast<float>(workload::kBindTargetHeight)});
        setScissor(commands, {.x = 0,
                              .y = 0,
                              .width = workload::kBindTargetWidth,
                              .height = workload::kBindTargetHeight});
        setCullMode(commands, CullMode::None);
        for (uint32_t d = 0; d < drawCount; ++d) {
            const uint32_t col = d % gridSize;
            const uint32_t row = d / gridSize;
            const uint32_t textureIndex = workload::bindTextureIndexForDraw(d);
            const QuadRoot vertexRoot{.offset = {-1.0f + cellNdc * (static_cast<float>(col) + 0.5f),
                                                 1.0f - cellNdc * (static_cast<float>(row) + 0.5f)},
                                      .halfExtent = {cellNdc * 0.5f, cellNdc * 0.5f}};
            const QuadPixelRoot pixelRoot{.textures = bindlessTableAddress(table),
                                          .samplers = bindlessTableAddress(table),
                                          .textureSlot = kFirstTextureSlot + textureIndex,
                                          .samplerSlot = kSamplerSlot,
                                          .tint = {1.0f, 1.0f, 1.0f, 1.0f}};
            const GpuAddress vertexAddress = pushRoot(commands, vertexRoot);
            const GpuAddress pixelAddress = pushRoot(commands, pixelRoot);
            draw(commands, vertexAddress, pixelAddress, 6);
        }
        endRenderPass(commands);
        endCommands(commands);
        const CommandBufferStats frameStats = commandBufferStats(commands);
        const std::array<CommandBuffer*, 1> list{commands};
        ++fenceValue;
        submit(queue, list, fence, fenceValue);
        const auto end = std::chrono::steady_clock::now();
        // End timed region.

        waitSemaphore(fence, fenceValue);
        if (frame >= warmupFrames) {
            result.perFrameTimedRegionNs.push_back(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
            const FrameBindingCounters frameCounters{
                .setAddressCalls = frameStats.setAddressCalls,
                .pushRootCalls = frameStats.rootCalls,
                .pushRootBytes = frameStats.rootBytes,
                .oneTimeTableWriteCalls = oneTimeTableStats.writeCalls,
                .oneTimeTableWriteBytes = oneTimeTableStats.writeBytes,
                .barrierCalls = frameStats.barrierCalls};
            if (!countersEverSet) {
                result.counters = frameCounters;
                countersEverSet = true;
            } else if (result.counters != frameCounters) {
                result.countersStableAcrossFrames = false;
            }
        }
    }

    result.endOfRun = result.endOfSetup;
    result.endOfRun.requestedBytes = deviceCreationStats(device).requestedBytes;
    result.endOfRun.metalReportedBytes = residentBytes(residency);
    result.ok = true;

    for (uint32_t slot = 0; slot < workload::kBindTextureCount + 4; ++slot) {
        clearBindlessSlot(table, slot);
    }
    for (Texture* texture : textures) {
        destroyTexture(device, texture);
    }
    destroyTexture(device, target);
    destroyPipeline(device, pipeline);
    destroySampler(device, *samplerResult);
    destroyBindlessTable(device, table);
    destroySemaphore(device, fence);
    deallocate(device, *readbackResult);
    deallocate(device, *textureMemResult);
    deallocate(device, *uploadStorageResult);
    deallocate(device, *rootStorageResult);
    destroyResidencySet(device, residency);
    destroyDevice(device);

    return result;
}

} // namespace lmx::experimental::noapi::bench
