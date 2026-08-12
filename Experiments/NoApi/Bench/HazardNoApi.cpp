//----------------------------------------------------------------------------------------------------------------------
/// @file HazardNoApi.cpp
/// @brief Implements HazardNoApi for the NoApi experiment.
//----------------------------------------------------------------------------------------------------------------------

/// @details Implements H01-H24 (spec section 7's hazard matrix) against the address-first
/// prototype.
///
/// Mirrors HazardRhi.cpp's RAW/WAR/WAW shape exactly (see that file's header comment for the shared
/// per-role semantics); every op here is realised through `lmx::noapi` instead of the production
/// RHI. Unlike the RHI side, `RenderPassDesc::ColorAttachment` names an explicit `mipLevel`
/// (NoApi/RenderPass.h), so every per-mip case -- including the five raster-at-a-non-zero-mip cases
/// the RHI side reports inexpressible -- runs here without a gap.

#include "Bench/StressCommon.h"
#include "Bench/StressShaders.h"
#include "Tests/TestShaders.h"

#include "NoApi/NoApi.h"
#include "Workload/StressCases.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace lmx::noapi::bench {
namespace {

using workload::HazardCase;
using workload::HazardKind;
using workload::HazardOpKind;

enum class Role { Write, Read };
// Which of the case's two ops this call realizes -- selects a disjoint bindless slot pair so the
// producer's and consumer's writeTextureSlot calls (both immediate CPU-side stores, see
// kSlotSourceProducer's comment below) never clobber each other while the command buffer records.
enum class Phase { Producer, Consumer };

// writeTextureSlot is an immediate CPU-side store into the table's memory (BindlessTable.h: "The
// write is immediate and CPU-side"), not a GPU-timeline-ordered command -- so writing the same slot
// twice while *recording* one command buffer (once for the producer op, again for the consumer op)
// overwrites the first write before the GPU ever executes the dispatch or draw that referenced it,
// independent of where the two ops sit relative to a barrier. Every producer-phase touch of a slot
// therefore uses a different index than every consumer-phase touch of the same logical role.
constexpr uint32_t kSlotSourceProducer = 0;
constexpr uint32_t kSlotTargetProducer = 1;
constexpr uint32_t kSlotSourceConsumer = 4;
constexpr uint32_t kSlotTargetConsumer = 5;
constexpr uint32_t kSlotSampler = 3;
constexpr uint32_t kTableSlotCount = 8;

// One oversized triangle covering the whole clip-space quad, matching Tests/NoApiTests.cpp's
// kTriangle exactly (this file cannot see that anonymous-namespace constant, so it is restated).
constexpr std::array<test::Vertex, 3> kTriangle{
    test::Vertex{.position = {-1.0f, -1.0f, 0.5f, 1.0f}, .uv = {0.0f, 1.0f}, .pad = {0.0f, 0.0f}},
    test::Vertex{.position = {3.0f, -1.0f, 0.5f, 1.0f}, .uv = {2.0f, 1.0f}, .pad = {0.0f, 0.0f}},
    test::Vertex{.position = {-1.0f, 3.0f, 0.5f, 1.0f}, .uv = {0.0f, -1.0f}, .pad = {0.0f, 0.0f}}};

struct HazardHarnessNoApi {
    Device* device = nullptr;
    Queue* queue = nullptr;
    ResidencySet* residency = nullptr;
    BindlessTable* table = nullptr;
    Semaphore* fence = nullptr;
    uint64_t fenceValue = 0;

    Allocation rootStorage{};
    LinearAllocator root;
    Allocation uploadStorage{};
    LinearAllocator upload;
    Allocation textureStorage{};
    uint64_t textureCursor = 0;
    Allocation readbackStorage{};

    Texture* target = nullptr;
    std::vector<Texture*> textures;
    std::vector<Pipeline*> pipelines;
    Sampler* sampler = nullptr;

    //==================================================================================================================
    ~HazardHarnessNoApi() {
        if (device == nullptr) {
            return;
        }
        for (uint32_t slot = 0; slot < kTableSlotCount; ++slot) {
            clearBindlessSlot(table, slot);
        }
        for (Texture* texture : textures) {
            destroyTexture(device, texture);
        }
        for (Pipeline* pipeline : pipelines) {
            destroyPipeline(device, pipeline);
        }
        if (sampler != nullptr) {
            destroySampler(device, sampler);
        }
        destroyBindlessTable(device, table);
        destroySemaphore(device, fence);
        deallocate(device, textureStorage);
        deallocate(device, readbackStorage);
        deallocate(device, uploadStorage);
        deallocate(device, rootStorage);
        destroyResidencySet(device, residency);
        destroyDevice(device);
    }

    //==================================================================================================================
    Suballocation stage(const void* data, uint64_t size) {
        const Suballocation storage = upload.allocate(size, 16);
        std::memcpy(storage.cpu, data, size);
        return storage;
    }

    //==================================================================================================================
    Texture* makeTexture(const TextureDesc& desc) {
        const SizeAlign required = textureSizeAlign(device, desc);
        const uint64_t aligned =
            (textureCursor + required.alignment - 1) & ~(required.alignment - 1);
        Result<Texture*> texture = createTexture(device, desc, textureStorage.gpu + aligned);
        textureCursor = aligned + required.size;
        textures.push_back(*texture);
        return *texture;
    }

    //==================================================================================================================
    CommandBuffer* begin(std::string_view label) {
        root.reset();
        return beginCommands(queue, &root, label);
    }

    //==================================================================================================================
    void submitAndWait(CommandBuffer* commands) {
        endCommands(commands);
        const std::array<CommandBuffer*, 1> list{commands};
        fenceValue += 1;
        submit(queue, list, fence, fenceValue);
        waitSemaphore(fence, fenceValue);
    }
};

//======================================================================================================================
bool setupHarness(HazardHarnessNoApi& h) {
    Result<Device*> device = createDevice({.label = "lmx.noapi.hazard"});
    if (!device) {
        return false;
    }
    h.device = *device;
    h.queue = mainQueue(h.device);

    Result<ResidencySet*> residency =
        createResidencySet(h.device, {.initialCapacity = 8, .label = "lmx.noapi.hazard.residency"});
    if (!residency) {
        return false;
    }
    h.residency = *residency;

    Result<BindlessTable*> table = createBindlessTable(
        h.device, {.slotCount = kTableSlotCount, .label = "lmx.noapi.hazard.table"});
    if (!table) {
        return false;
    }
    h.table = *table;

    Result<Semaphore*> fence = createSemaphore(h.device, 0, "lmx.noapi.hazard.fence");
    if (!fence) {
        return false;
    }
    h.fence = *fence;

    Result<Allocation> rootStorage = allocate(h.device, {.size = 64 * 1024,
                                                         .alignment = 256,
                                                         .kind = MemoryKind::Shared,
                                                         .label = "lmx.noapi.hazard.root"});
    Result<Allocation> uploadStorage = allocate(h.device, {.size = 512 * 1024,
                                                           .alignment = 256,
                                                           .kind = MemoryKind::Shared,
                                                           .label = "lmx.noapi.hazard.upload"});
    Result<Allocation> readbackStorage = allocate(h.device, {.size = 512 * 1024,
                                                             .alignment = 256,
                                                             .kind = MemoryKind::Readback,
                                                             .label = "lmx.noapi.hazard.readback"});
    Result<Allocation> textureStorage = allocate(h.device, {.size = 8 * 1024 * 1024,
                                                            .alignment = 65536,
                                                            .kind = MemoryKind::Private,
                                                            .label = "lmx.noapi.hazard.textures"});
    if (!rootStorage || !uploadStorage || !readbackStorage || !textureStorage) {
        return false;
    }
    h.rootStorage = *rootStorage;
    h.root = LinearAllocator(h.rootStorage);
    h.uploadStorage = *uploadStorage;
    h.upload = LinearAllocator(h.uploadStorage);
    h.readbackStorage = *readbackStorage;
    h.textureStorage = *textureStorage;

    Result<Sampler*> sampler = createSampler(h.device, {.minFilter = FilterMode::Nearest,
                                                        .magFilter = FilterMode::Nearest,
                                                        .mipFilter = FilterMode::Nearest,
                                                        .addressU = AddressMode::ClampToEdge,
                                                        .addressV = AddressMode::ClampToEdge,
                                                        .label = "lmx.noapi.hazard.sampler"});
    if (!sampler) {
        return false;
    }
    h.sampler = *sampler;
    writeSamplerSlot(h.table, kSlotSampler, h.sampler);

    commitResidency(h.residency);
    return true;
}

//======================================================================================================================
Pipeline* makeCopyPipeline(HazardHarnessNoApi& h) {
    Result<Pipeline*> pipeline = createComputePipeline(
        h.device, {.compute = {.ir = stressShaderSource(), .entryPoint = "lmxCopyImageKernel"},
                   .label = "lmx.noapi.hazard.copy"});
    h.pipelines.push_back(*pipeline);
    return *pipeline;
}
//======================================================================================================================
Pipeline* makeReadPipeline(HazardHarnessNoApi& h) {
    Result<Pipeline*> pipeline = createComputePipeline(
        h.device,
        {.compute = {.ir = stressShaderSource(), .entryPoint = "lmxReadImageToBufferKernel"},
         .label = "lmx.noapi.hazard.read"});
    h.pipelines.push_back(*pipeline);
    return *pipeline;
}
//======================================================================================================================
Pipeline* makeRasterPipeline(HazardHarnessNoApi& h, Format depthFormat = Format::Undefined) {
    const std::array<ColorTargetDesc, 1> targets{
        ColorTargetDesc{.format = Format::RGBA8Unorm, .writeMask = 0xF}};
    Result<Pipeline*> pipeline = createGraphicsPipeline(
        h.device, {.vertex = {.ir = test::shaderSource(), .entryPoint = "lmxTriangleVs"},
                   .pixel = {.ir = test::shaderSource(), .entryPoint = "lmxBindlessFs"},
                   .raster = {.topology = Topology::TriangleList,
                              .sampleCount = 1,
                              .depthFormat = depthFormat,
                              .colorTargets = targets},
                   .label = "lmx.noapi.hazard.raster"});
    h.pipelines.push_back(*pipeline);
    return *pipeline;
}

//======================================================================================================================
// Executes one Write op against `h.target` mip `mip`/`extent`, writing `content`. `phase` selects
// the producer- or consumer-phase slot pair (see kSlotSourceProducer's comment above).
void executeWrite(HazardHarnessNoApi& h, CommandBuffer* commands, Pipeline* copyPipeline,
                  Pipeline* rasterPipeline, HazardOpKind kind, uint32_t mip, uint32_t extent,
                  const std::vector<uint8_t>& content, Phase phase) {
    const uint32_t slotSource =
        phase == Phase::Producer ? kSlotSourceProducer : kSlotSourceConsumer;
    const uint32_t slotTarget =
        phase == Phase::Producer ? kSlotTargetProducer : kSlotTargetConsumer;
    switch (kind) {
    case HazardOpKind::Copy: {
        const Suballocation staging = h.stage(content.data(), content.size());
        copyToTexture(commands, h.target,
                      {.mipLevel = mip, .origin = {}, .extent = {extent, extent, 1}}, staging.gpu,
                      {.bytesPerRow = uint64_t{extent} * 4});
        break;
    }
    case HazardOpKind::Compute: {
        Texture* source =
            h.makeTexture({.kind = TextureKind::Texture2D,
                           .extent = {extent, extent, 1},
                           .mipCount = 1,
                           .format = Format::RGBA8Unorm,
                           .usage = TextureUsage::Storage | TextureUsage::CopyDestination,
                           .label = "lmx.noapi.hazard.computeSource"});
        const Suballocation staging = h.stage(content.data(), content.size());
        copyToTexture(commands, source,
                      {.mipLevel = 0, .origin = {}, .extent = {extent, extent, 1}}, staging.gpu,
                      {.bytesPerRow = uint64_t{extent} * 4});
        barrier(commands, Stage::Copy, Stage::Compute, Hazard::Descriptors);
        writeTextureSlot(h.table, slotSource, source, {.storage = true});
        writeTextureSlot(h.table, slotTarget, h.target,
                         {.baseMipLevel = mip, .mipCount = 1, .storage = true});
        setBindlessTable(commands, h.table);
        const CopyImageRoot root{.dstSlot = slotTarget, .srcSlot = slotSource, .extent = extent};
        const GpuAddress rootAddress = pushRoot(commands, root);
        setPipeline(commands, copyPipeline);
        const uint32_t groups = (extent + 7) / 8;
        dispatch(commands, rootAddress, groups, groups, 1);
        break;
    }
    case HazardOpKind::Raster: {
        Texture* source =
            h.makeTexture({.kind = TextureKind::Texture2D,
                           .extent = {extent, extent, 1},
                           .mipCount = 1,
                           .format = Format::RGBA8Unorm,
                           .usage = TextureUsage::Sampled | TextureUsage::CopyDestination,
                           .label = "lmx.noapi.hazard.rasterSource"});
        const Suballocation staging = h.stage(content.data(), content.size());
        copyToTexture(commands, source,
                      {.mipLevel = 0, .origin = {}, .extent = {extent, extent, 1}}, staging.gpu,
                      {.bytesPerRow = uint64_t{extent} * 4});
        barrier(commands, Stage::Copy, Stage::PixelShader, Hazard::Descriptors);
        const TextureHandle sourceHandle = writeTextureSlot(h.table, slotSource, source, {});
        setBindlessTable(commands, h.table);
        const Suballocation vertices = h.stage(kTriangle.data(), sizeof(kTriangle));
        const test::VertexRoot vertexRoot{.tint = {1, 1, 1, 1}, .vertices = vertices.gpu};
        const test::BindlessPixelRoot pixelRoot{.samplers = bindlessTableAddress(h.table),
                                                .textureSlot = sourceHandle.slot,
                                                .samplerSlot = kSlotSampler};
        const GpuAddress vertexAddress = pushRoot(commands, vertexRoot);
        const GpuAddress pixelAddress = pushRoot(commands, pixelRoot);

        const std::array<ColorAttachment, 1> colorTargets{
            ColorAttachment{.texture = h.target,
                            .mipLevel = mip,
                            .load = LoadAction::DontCare,
                            .store = StoreAction::Store}};
        beginRenderPass(commands,
                        {.colorTargets = colorTargets, .label = "lmx.noapi.hazard.write"});
        setPipeline(commands, rasterPipeline);
        setViewport(commands, {.x = 0,
                               .y = 0,
                               .width = static_cast<float>(extent),
                               .height = static_cast<float>(extent)});
        setScissor(commands, {.x = 0, .y = 0, .width = extent, .height = extent});
        setCullMode(commands, CullMode::None);
        draw(commands, vertexAddress, pixelAddress, 3);
        endRenderPass(commands);
        break;
    }
    }
}

//======================================================================================================================
// Executes one Read op against `h.target` mip `mip`/`extent`, relaying the observed content into
// `h.readbackStorage` at `readbackOffset` as extent*extent*4 tightly-packed RGBA8 bytes. `phase`
// selects the producer- or consumer-phase slot pair, as executeWrite's does.
void executeRead(HazardHarnessNoApi& h, CommandBuffer* commands, Pipeline* readPipeline,
                 Pipeline* rasterPipeline, HazardOpKind kind, uint32_t mip, uint32_t extent,
                 uint64_t readbackOffset, Phase phase) {
    const uint32_t slotTarget =
        phase == Phase::Producer ? kSlotTargetProducer : kSlotTargetConsumer;
    const GpuAddress outAddress = h.readbackStorage.gpu + readbackOffset;
    switch (kind) {
    case HazardOpKind::Copy: {
        copyFromTexture(commands, outAddress, {.bytesPerRow = uint64_t{extent} * 4}, h.target,
                        {.mipLevel = mip, .origin = {}, .extent = {extent, extent, 1}});
        break;
    }
    case HazardOpKind::Compute: {
        writeTextureSlot(h.table, slotTarget, h.target,
                         {.baseMipLevel = mip, .mipCount = 1, .storage = true});
        setBindlessTable(commands, h.table);
        const ReadImageRoot root{.out = outAddress, .srcSlot = slotTarget, .extent = extent};
        const GpuAddress rootAddress = pushRoot(commands, root);
        setPipeline(commands, readPipeline);
        const uint32_t groups = (extent + 7) / 8;
        dispatch(commands, rootAddress, groups, groups, 1);
        break;
    }
    case HazardOpKind::Raster: {
        Texture* result =
            h.makeTexture({.kind = TextureKind::Texture2D,
                           .extent = {extent, extent, 1},
                           .mipCount = 1,
                           .format = Format::RGBA8Unorm,
                           .usage = TextureUsage::ColorAttachment | TextureUsage::CopySource,
                           .label = "lmx.noapi.hazard.rasterResult"});
        const TextureHandle targetHandle =
            writeTextureSlot(h.table, slotTarget, h.target, {.baseMipLevel = mip, .mipCount = 1});
        setBindlessTable(commands, h.table);
        const Suballocation vertices = h.stage(kTriangle.data(), sizeof(kTriangle));
        const test::VertexRoot vertexRoot{.tint = {1, 1, 1, 1}, .vertices = vertices.gpu};
        const test::BindlessPixelRoot pixelRoot{.samplers = bindlessTableAddress(h.table),
                                                .textureSlot = targetHandle.slot,
                                                .samplerSlot = kSlotSampler};
        const GpuAddress vertexAddress = pushRoot(commands, vertexRoot);
        const GpuAddress pixelAddress = pushRoot(commands, pixelRoot);

        const std::array<ColorAttachment, 1> colorTargets{ColorAttachment{
            .texture = result, .load = LoadAction::DontCare, .store = StoreAction::Store}};
        beginRenderPass(commands, {.colorTargets = colorTargets, .label = "lmx.noapi.hazard.read"});
        setPipeline(commands, rasterPipeline);
        setViewport(commands, {.x = 0,
                               .y = 0,
                               .width = static_cast<float>(extent),
                               .height = static_cast<float>(extent)});
        setScissor(commands, {.x = 0, .y = 0, .width = extent, .height = extent});
        setCullMode(commands, CullMode::None);
        draw(commands, vertexAddress, pixelAddress, 3);
        endRenderPass(commands);
        barrier(commands, Stage::RasterColorOut, Stage::Copy);
        copyFromTexture(commands, outAddress, {.bytesPerRow = uint64_t{extent} * 4}, result,
                        {.mipLevel = 0, .origin = {}, .extent = {extent, extent, 1}});
        break;
    }
    }
}

//======================================================================================================================
// The prototype's `barrier` tracks pendingConsumer precisely (NoApi/CommandBuffer.h: "a barrier was
// recorded but no later pass consumed it" is fatal at endCommands), so every barrier here names
// exactly the stage the very next command belongs to -- never a broader union of "whatever kind
// might follow."
// The Metal 4 backend maps Stage::Copy and Stage::Compute onto the identical encoder family
// (Metal4Internal.cpp's toStages: both fold into kComputeStages, because this backend issues copies
// through the compute encoder), and Stage::PixelShader/Stage::RasterColorOut onto the render
// encoder's family (StageFragment) -- so the two-family view below is what "the next command's
// stage" actually reduces to, regardless of which of the two same-family enumerators is named.
Stage opFamily(HazardOpKind kind) {
    return kind == HazardOpKind::Raster ? (Stage::PixelShader | Stage::RasterColorOut)
                                        : (Stage::Copy | Stage::Compute);
}

//======================================================================================================================
CaseResult verifyOffset(const std::string& id, const char* what,
                        const std::vector<uint8_t>& expected, const HazardHarnessNoApi& h,
                        uint64_t offset, uint64_t barrierCalls) {
    std::vector<uint8_t> actual(expected.size());
    std::memcpy(actual.data(), static_cast<const uint8_t*>(h.readbackStorage.cpu) + offset,
                expected.size());
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
                std::string(what) + " mismatch at byte offset " + std::to_string(mismatch),
                barrierCalls};
    }
    return {id, true, "", barrierCalls};
}

//======================================================================================================================
CaseResult runOneHazardCaseNoApi(const HazardCase& hc) {
    const uint32_t textureSize =
        hc.perMip ? workload::kHazardPerMipTextureSize : workload::kHazardWholeResourceTextureSize;
    const uint32_t mipCount = hc.perMip ? workload::kHazardPerMipTextureMipCount : 1;
    const uint32_t producerMip = hc.perMip ? hc.producerMip : 0;
    const uint32_t consumerMip = hc.perMip ? hc.consumerMip : 0;
    const uint32_t producerExtent = textureSize >> producerMip;
    const uint32_t consumerExtent = textureSize >> consumerMip;

    HazardHarnessNoApi h;
    if (!setupHarness(h)) {
        return {hc.id, false, "prototype device/harness setup failed"};
    }

    h.target = h.makeTexture({.kind = TextureKind::Texture2D,
                              .extent = {textureSize, textureSize, 1},
                              .mipCount = mipCount,
                              .format = Format::RGBA8Unorm,
                              .usage = TextureUsage::ColorAttachment | TextureUsage::Sampled |
                                       TextureUsage::Storage | TextureUsage::CopySource |
                                       TextureUsage::CopyDestination,
                              .label = "lmx.noapi.hazard.target"});

    Pipeline* copyPipeline = makeCopyPipeline(h);
    Pipeline* readPipeline = makeReadPipeline(h);
    Pipeline* rasterPipeline = makeRasterPipeline(h);

    const std::vector<uint8_t> producerExpected = hazardExpectedRgba(hc, producerExtent);
    const std::vector<uint8_t> consumerExpected = hazardExpectedRgba(hc, consumerExtent);
    const std::vector<uint8_t> producerOld = hazardOldRgba(hc, producerExtent);

    CommandBuffer* commands = h.begin("lmx.noapi.hazard.frame");

    // RAW's consumer read needs consumerMip pre-populated; WAR's producer read needs producerMip
    // pre-populated with the deliberately-different "old" content (see HazardRhi.cpp's header
    // comment for why both sides share this shape).
    if (hc.hazard == HazardKind::ReadAfterWrite) {
        const Suballocation staging = h.stage(consumerExpected.data(), consumerExpected.size());
        copyToTexture(
            commands, h.target,
            {.mipLevel = consumerMip, .origin = {}, .extent = {consumerExtent, consumerExtent, 1}},
            staging.gpu, {.bytesPerRow = uint64_t{consumerExtent} * 4});
        barrier(commands, Stage::Copy | Stage::Compute, opFamily(hc.producer));
    } else if (hc.hazard == HazardKind::WriteAfterRead) {
        const Suballocation staging = h.stage(producerOld.data(), producerOld.size());
        copyToTexture(
            commands, h.target,
            {.mipLevel = producerMip, .origin = {}, .extent = {producerExtent, producerExtent, 1}},
            staging.gpu, {.bytesPerRow = uint64_t{producerExtent} * 4});
        barrier(commands, Stage::Copy | Stage::Compute, opFamily(hc.producer));
    }

    constexpr uint64_t kProducerVerifyOffset = 0;
    constexpr uint64_t kConsumerVerifyOffset = 256 * 1024;

    if (hc.hazard == HazardKind::ReadAfterWrite) {
        executeWrite(h, commands, copyPipeline, rasterPipeline, hc.producer, producerMip,
                     producerExtent, producerExpected, Phase::Producer);
        barrier(commands, opFamily(hc.producer), opFamily(hc.consumer), Hazard::Descriptors);
        executeRead(h, commands, readPipeline, rasterPipeline, hc.consumer, consumerMip,
                    consumerExtent, kConsumerVerifyOffset, Phase::Consumer);
        // executeRead's Raster branch already ends on a compute-family encoder (its own internal
        // relay copy); every other kind ends on compute-family too, so the next barrier's producer
        // is always compute-family here.
        barrier(commands, Stage::Copy | Stage::Compute, Stage::Copy | Stage::Compute);
        copyFromTexture(
            commands, h.readbackStorage.gpu + kProducerVerifyOffset,
            {.bytesPerRow = uint64_t{producerExtent} * 4}, h.target,
            {.mipLevel = producerMip, .origin = {}, .extent = {producerExtent, producerExtent, 1}});
    } else if (hc.hazard == HazardKind::WriteAfterRead) {
        executeRead(h, commands, readPipeline, rasterPipeline, hc.producer, producerMip,
                    producerExtent, kProducerVerifyOffset, Phase::Producer);
        barrier(commands, Stage::Copy | Stage::Compute, opFamily(hc.consumer), Hazard::Descriptors);
        executeWrite(h, commands, copyPipeline, rasterPipeline, hc.consumer, consumerMip,
                     consumerExtent, consumerExpected, Phase::Consumer);
        barrier(commands, opFamily(hc.consumer), Stage::Copy | Stage::Compute);
        copyFromTexture(
            commands, h.readbackStorage.gpu + kConsumerVerifyOffset,
            {.bytesPerRow = uint64_t{consumerExtent} * 4}, h.target,
            {.mipLevel = consumerMip, .origin = {}, .extent = {consumerExtent, consumerExtent, 1}});
    } else { // WriteAfterWrite
        executeWrite(h, commands, copyPipeline, rasterPipeline, hc.producer, producerMip,
                     producerExtent, producerOld, Phase::Producer);
        barrier(commands, opFamily(hc.producer), opFamily(hc.consumer), Hazard::Descriptors);
        executeWrite(h, commands, copyPipeline, rasterPipeline, hc.consumer, consumerMip,
                     consumerExtent, consumerExpected, Phase::Consumer);
        barrier(commands, opFamily(hc.consumer), Stage::Copy | Stage::Compute);
        copyFromTexture(
            commands, h.readbackStorage.gpu + kProducerVerifyOffset,
            {.bytesPerRow = uint64_t{producerExtent} * 4}, h.target,
            {.mipLevel = producerMip, .origin = {}, .extent = {producerExtent, producerExtent, 1}});
        copyFromTexture(
            commands, h.readbackStorage.gpu + kConsumerVerifyOffset,
            {.bytesPerRow = uint64_t{consumerExtent} * 4}, h.target,
            {.mipLevel = consumerMip, .origin = {}, .extent = {consumerExtent, consumerExtent, 1}});
    }

    h.submitAndWait(commands);
    // M5.1 barrier-count instrumentation (spec section 9): `commands` is retired but not yet
    // reused (the next `beginCommands` call resets it), so its accumulated stats are still valid to
    // read here -- the same choke point RhiAdapter/NoApiAdapter's own instrumentation reads,
    // applied to a stress case instead of a full frame.
    const uint64_t barrierCalls = commandBufferStats(commands).barrierCalls;

    if (hc.hazard == HazardKind::ReadAfterWrite) {
        CaseResult producerCheck = verifyOffset(hc.id, "producer write", producerExpected, h,
                                                kProducerVerifyOffset, barrierCalls);
        if (!producerCheck.passed) {
            return producerCheck;
        }
        return verifyOffset(hc.id, "consumer read", consumerExpected, h, kConsumerVerifyOffset,
                            barrierCalls);
    }
    if (hc.hazard == HazardKind::WriteAfterRead) {
        CaseResult producerCheck = verifyOffset(hc.id, "producer read (old value)", producerOld, h,
                                                kProducerVerifyOffset, barrierCalls);
        if (!producerCheck.passed) {
            return producerCheck;
        }
        return verifyOffset(hc.id, "consumer write", consumerExpected, h, kConsumerVerifyOffset,
                            barrierCalls);
    }
    // WriteAfterWrite. A whole-resource case's producer and consumer write the identical mip 0
    // subresource, so the consumer's write physically overwrites the producer's "old" bytes --
    // there is nothing left to observe there, and the correctness property collapses to "the final
    // state is exactly what the consumer wrote" (proving the producer's write did not race or
    // corrupt it). A per-mip case's two mips are disjoint memory, so both are independently
    // checked.
    if (hc.perMip) {
        CaseResult producerCheck = verifyOffset(hc.id, "producer write (old value)", producerOld, h,
                                                kProducerVerifyOffset, barrierCalls);
        if (!producerCheck.passed) {
            return producerCheck;
        }
    }
    return verifyOffset(hc.id, "consumer write", consumerExpected, h, kConsumerVerifyOffset,
                        barrierCalls);
}

} // namespace

//======================================================================================================================
std::vector<CaseResult> runHazardCasesNoApi(const std::string& caseId) {
    std::vector<CaseResult> results;
    for (const HazardCase& hc : workload::hazardCases()) {
        if (caseId != "all" && caseId != hc.id) {
            continue;
        }
        results.push_back(runOneHazardCaseNoApi(hc));
    }
    return results;
}

} // namespace lmx::noapi::bench
