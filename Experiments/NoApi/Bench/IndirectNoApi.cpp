//----------------------------------------------------------------------------------------------------------------------
/// @file IndirectNoApi.cpp
/// @brief Implements I1-I4 (spec section 7) against the address-first prototype.
//----------------------------------------------------------------------------------------------------------------------

#include "Bench/StressCommon.h"
#include "Bench/StressShaders.h"
#include "Tests/TestShaders.h"

#include "NoApi/NoApi.h"
#include "Workload/StressCases.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lmx::experimental::noapi::bench {
namespace {

using workload::IndirectArgSource;
using workload::IndirectCase;
using workload::IndirectKind;

struct DrawArgs {
    uint32_t vertexCount = 0, instanceCount = 0, vertexStart = 0, baseInstance = 0;
};
struct DrawIndexedArgs {
    uint32_t indexCount = 0, instanceCount = 0, indexStart = 0;
    int32_t baseVertex = 0;
    uint32_t baseInstance = 0;
};
struct DispatchArgs {
    uint32_t groupsX = 0, groupsY = 0, groupsZ = 0;
};

struct Harness {
    Device* device = nullptr;
    Queue* queue = nullptr;
    ResidencySet* residency = nullptr;
    Semaphore* fence = nullptr;
    uint64_t fenceValue = 0;
    Allocation rootStorage{};
    LinearAllocator root;
    Allocation uploadStorage{};
    LinearAllocator upload;

    //==================================================================================================================
    bool setup() {
        Result<Device*> d = createDevice({.label = "lmx.noapi.indirect"});
        if (!d) {
            return false;
        }
        device = *d;
        queue = mainQueue(device);
        Result<ResidencySet*> r =
            createResidencySet(device, {.initialCapacity = 4, .label = "lmx.noapi.indirect.res"});
        if (!r) {
            return false;
        }
        residency = *r;
        Result<Semaphore*> f = createSemaphore(device, 0, "lmx.noapi.indirect.fence");
        if (!f) {
            return false;
        }
        fence = *f;
        Result<Allocation> rootAlloc = allocate(device, {.size = 16 * 1024,
                                                         .alignment = 256,
                                                         .kind = MemoryKind::Shared,
                                                         .label = "lmx.noapi.indirect.root"});
        Result<Allocation> uploadAlloc = allocate(device, {.size = 16 * 1024,
                                                           .alignment = 256,
                                                           .kind = MemoryKind::Shared,
                                                           .label = "lmx.noapi.indirect.upload"});
        if (!rootAlloc || !uploadAlloc) {
            return false;
        }
        rootStorage = *rootAlloc;
        root = LinearAllocator(rootStorage);
        uploadStorage = *uploadAlloc;
        upload = LinearAllocator(uploadStorage);
        commitResidency(residency);
        return true;
    }

    //==================================================================================================================
    ~Harness() {
        if (device == nullptr) {
            return;
        }
        destroySemaphore(device, fence);
        deallocate(device, uploadStorage);
        deallocate(device, rootStorage);
        destroyResidencySet(device, residency);
        destroyDevice(device);
    }

    //==================================================================================================================
    template <typename T>
    Suballocation stage(const T& value) {
        const Suballocation storage = upload.allocate(sizeof(T), alignof(T) < 16 ? 16 : alignof(T));
        std::memcpy(storage.cpu, &value, sizeof(T));
        return storage;
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
CaseResult runDrawCase(Harness& h, const char* id, bool indexed) {
    Result<Allocation> targetAlloc = allocate(h.device, {.size = 4 * 4 * 4,
                                                         .alignment = 256,
                                                         .kind = MemoryKind::Readback,
                                                         .label = "indirect.readback"});
    if (!targetAlloc) {
        return {id, false, "readback allocation failed"};
    }
    Allocation readback = *targetAlloc;
    Result<Allocation> textureMem = allocate(h.device, {.size = 64 * 1024,
                                                        .alignment = 65536,
                                                        .kind = MemoryKind::Private,
                                                        .label = "indirect.textureMem"});
    if (!textureMem) {
        return {id, false, "texture memory allocation failed"};
    }
    Texture* target;
    {
        TextureDesc desc{.kind = TextureKind::Texture2D,
                         .extent = {4, 4, 1},
                         .mipCount = 1,
                         .format = Format::RGBA8Unorm,
                         .usage = TextureUsage::ColorAttachment | TextureUsage::CopySource,
                         .label = "indirect.target"};
        Result<Texture*> t = createTexture(h.device, desc, textureMem->gpu);
        if (!t) {
            deallocate(h.device, *textureMem);
            deallocate(h.device, readback);
            return {id, false, "target texture creation failed"};
        }
        target = *t;
    }

    const std::array<ColorTargetDesc, 1> targets{
        ColorTargetDesc{.format = Format::RGBA8Unorm, .writeMask = 0xF}};
    Result<Pipeline*> pipelineResult = createGraphicsPipeline(
        h.device, {.vertex = {.ir = test::shaderSource(), .entryPoint = "lmxTriangleVs"},
                   .pixel = {.ir = test::shaderSource(), .entryPoint = "lmxSolidFs"},
                   .raster = {.topology = Topology::TriangleList, .colorTargets = targets},
                   .label = "indirect.drawPipeline"});
    if (!pipelineResult) {
        deallocate(h.device, *textureMem);
        deallocate(h.device, readback);
        return {id, false, "pipeline creation failed"};
    }
    Pipeline* pipeline = *pipelineResult;

    constexpr std::array<test::Vertex, 3> kTriangle{
        test::Vertex{.position = {-1, -1, 0.5f, 1}, .uv = {0, 1}, .pad = {0, 0}},
        test::Vertex{.position = {3, -1, 0.5f, 1}, .uv = {2, 1}, .pad = {0, 0}},
        test::Vertex{.position = {-1, 3, 0.5f, 1}, .uv = {0, -1}, .pad = {0, 0}}};

    CommandBuffer* commands = h.begin(id);
    const Suballocation vertices = h.stage(kTriangle);
    const test::VertexRoot vertexRoot{.tint = {1, 0, 0, 1}, .vertices = vertices.gpu};
    const test::SolidPixelRoot pixelRoot{.scale = {1, 1, 1, 1}};
    const GpuAddress vertexAddress = pushRoot(commands, vertexRoot);
    const GpuAddress pixelAddress = pushRoot(commands, pixelRoot);

    const std::array<ColorAttachment, 1> colorTargets{ColorAttachment{.texture = target,
                                                                      .load = LoadAction::Clear,
                                                                      .store = StoreAction::Store,
                                                                      .clearColor = {0, 0, 0, 1}}};

    Pipeline* argsPipeline = nullptr;
    if (!indexed) { // I1: CPU-written drawIndirect at offset 256.
        constexpr uint64_t kArgsSize = 512;
        std::vector<uint8_t> bytes(kArgsSize, 0);
        DrawArgs args{.vertexCount = 3, .instanceCount = 1};
        std::memcpy(bytes.data() + 256, &args, sizeof(args));
        const Suballocation argsStaging = h.upload.allocate(kArgsSize, 16);
        std::memcpy(argsStaging.cpu, bytes.data(), kArgsSize);

        beginRenderPass(commands, {.colorTargets = colorTargets, .label = id});
        setPipeline(commands, pipeline);
        setViewport(commands, {.x = 0, .y = 0, .width = 4, .height = 4});
        setScissor(commands, {.x = 0, .y = 0, .width = 4, .height = 4});
        setCullMode(commands, CullMode::None);
        drawIndirect(commands, vertexAddress, pixelAddress, argsStaging.gpu + 256);
        endRenderPass(commands);
    } else { // I2: a prior compute dispatch writes drawIndexedIndirect's args at offset 0, ordered
             // by a barrier before the draw.
        std::array<uint32_t, 3> indexData{0, 1, 2};
        const Suballocation indices = h.stage(indexData);
        const Suballocation argsStorage = h.upload.allocate(256, 256);
        std::memset(argsStorage.cpu, 0, 256);

        Result<Pipeline*> argsPipelineResult = createComputePipeline(
            h.device,
            {.compute = {.ir = stressShaderSource(), .entryPoint = "lmxWriteIndirectArgsKernel"},
             .label = "indirect.i2ArgsPipeline"});
        argsPipeline = *argsPipelineResult;
        const IndirectArgsWriteRoot writeRoot{.args = argsStorage.gpu,
                                              .argsIndex = 0,
                                              .valueA = 3, // indexCount
                                              .valueB = 1, // instanceCount
                                              .valueC = 0, // firstIndex
                                              .valueD = 0, // baseVertex
                                              .valueE = 0, // firstInstance
                                              .wordCount = 5};
        const GpuAddress writeRootAddress = pushRoot(commands, writeRoot);
        setPipeline(commands, argsPipeline);
        dispatch(commands, writeRootAddress, 1, 1, 1);
        barrier(commands, Stage::Compute, Stage::VertexShader, Hazard::DrawArguments);

        beginRenderPass(commands, {.colorTargets = colorTargets, .label = id});
        setPipeline(commands, pipeline);
        setViewport(commands, {.x = 0, .y = 0, .width = 4, .height = 4});
        setScissor(commands, {.x = 0, .y = 0, .width = 4, .height = 4});
        setCullMode(commands, CullMode::None);
        drawIndexedIndirect(commands, vertexAddress, pixelAddress, indices.gpu, IndexKind::Uint32,
                            argsStorage.gpu);
        endRenderPass(commands);
    }

    Result<Allocation> resultReadback = allocate(h.device, {.size = 4 * 4 * 4,
                                                            .alignment = 256,
                                                            .kind = MemoryKind::Readback,
                                                            .label = "indirect.result"});
    barrier(commands, Stage::RasterColorOut, Stage::Copy);
    copyFromTexture(commands, resultReadback->gpu, {.bytesPerRow = 16}, target,
                    {.mipLevel = 0, .origin = {}, .extent = {4, 4, 1}});
    h.submitAndWait(commands);

    const auto* pixel = static_cast<const uint8_t*>(resultReadback->cpu);
    const bool ok = pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0;

    deallocate(h.device, *resultReadback);
    if (argsPipeline != nullptr) {
        destroyPipeline(h.device, argsPipeline);
    }
    destroyPipeline(h.device, pipeline);
    destroyTexture(h.device, target);
    deallocate(h.device, *textureMem);
    deallocate(h.device, readback);

    return {id, ok, ok ? "" : "target pixel is not the solid draw colour"};
}

//======================================================================================================================
CaseResult runDispatchCase(Harness& h, const char* id, bool computeWritten) {
    Result<Allocation> storageAlloc = allocate(h.device, {.size = 1024,
                                                          .alignment = 256,
                                                          .kind = MemoryKind::Shared,
                                                          .label = "indirect.dispatchStorage"});
    if (!storageAlloc) {
        return {id, false, "storage allocation failed"};
    }
    commitResidency(h.residency);

    Result<Pipeline*> fillPipelineResult = createComputePipeline(
        h.device, {.compute = {.ir = test::shaderSource(), .entryPoint = "lmxFillBufferKernel"},
                   .label = "indirect.fillPipeline"});
    Pipeline* fillPipeline = *fillPipelineResult;

    CommandBuffer* commands = h.begin(id);
    constexpr uint32_t kBias = 100;
    const test::FillRoot fillRoot{.out = storageAlloc->gpu, .count = 64, .base = kBias};
    const GpuAddress fillRootAddress = pushRoot(commands, fillRoot);

    Pipeline* argsPipeline = nullptr;
    if (!computeWritten) { // I3: CPU-written at offset 64.
        constexpr uint64_t kArgsSize = 256;
        DispatchArgs args{.groupsX = 1, .groupsY = 1, .groupsZ = 1};
        std::vector<uint8_t> bytes(kArgsSize, 0);
        std::memcpy(bytes.data() + 64, &args, sizeof(args));
        const Suballocation argsStaging = h.upload.allocate(kArgsSize, 16);
        std::memcpy(argsStaging.cpu, bytes.data(), kArgsSize);
        setPipeline(commands, fillPipeline);
        dispatchIndirect(commands, fillRootAddress, argsStaging.gpu + 64);
    } else { // I4: compute-written at offset 128.
        const Suballocation argsStorage = h.upload.allocate(256, 256);
        std::memset(argsStorage.cpu, 0, 256);
        Result<Pipeline*> argsPipelineResult = createComputePipeline(
            h.device,
            {.compute = {.ir = stressShaderSource(), .entryPoint = "lmxWriteIndirectArgsKernel"},
             .label = "indirect.i4ArgsPipeline"});
        argsPipeline = *argsPipelineResult;
        const IndirectArgsWriteRoot writeRoot{.args = argsStorage.gpu,
                                              .argsIndex = 128 / 4,
                                              .valueA = 1,
                                              .valueB = 1,
                                              .valueC = 1,
                                              .wordCount = 3};
        const GpuAddress writeRootAddress = pushRoot(commands, writeRoot);
        setPipeline(commands, argsPipeline);
        dispatch(commands, writeRootAddress, 1, 1, 1);
        // Hazard::DrawArguments always folds in Stage::VertexShader (Metal4Internal.cpp's
        // hazardStages: "the dependency has to reach the earliest stage", covering both
        // drawIndirect and dispatchIndirect with one flag), which this dispatch-only command buffer
        // never opens a render encoder to consume -- the pending bit would otherwise leak to
        // endCommands's "barrier recorded but no later pass consumed it" assert. Plain
        // Stage::Compute orders the write correctly for this dispatch-only case without naming a
        // hazard this sequence has no render work to retire.
        barrier(commands, Stage::Compute, Stage::Compute);
        setPipeline(commands, fillPipeline);
        dispatchIndirect(commands, fillRootAddress, argsStorage.gpu + 128);
    }

    barrier(commands, Stage::Compute, Stage::Copy);
    Result<Allocation> readback = allocate(h.device, {.size = 64 * sizeof(uint32_t),
                                                      .alignment = 256,
                                                      .kind = MemoryKind::Readback,
                                                      .label = "indirect.dispatchReadback"});
    copyMemory(commands, readback->gpu, storageAlloc->gpu, 64 * sizeof(uint32_t));
    h.submitAndWait(commands);

    // lmxFillBufferKernel (Tests/TestShaders.h) writes root.base + index, unlike the RHI side's
    // Shaders/ComputeSmoke.slang computeFillBuffer (tid.x * 3 + bias) -- the two kernels are
    // deliberately different oracles, so each side's expected pattern is its own kernel's formula.
    const auto* values = static_cast<const uint32_t*>(readback->cpu);
    const bool ok = values[0] == kBias && values[63] == 63u + kBias;
    if (!ok && std::getenv("LMX_NOAPI_HAZARD_DEBUG") != nullptr) {
        std::fprintf(stderr,
                     "[debug] %s values[0]=%u values[1]=%u values[32]=%u values[63]=%u kBias=%u\n",
                     id, values[0], values[1], values[32], values[63], kBias);
    }

    deallocate(h.device, *readback);
    if (argsPipeline != nullptr) {
        destroyPipeline(h.device, argsPipeline);
    }
    destroyPipeline(h.device, fillPipeline);
    deallocate(h.device, *storageAlloc);

    return {id, ok, ok ? "" : "dispatched fill did not produce the expected pattern"};
}

} // namespace

//======================================================================================================================
std::vector<CaseResult> runIndirectCasesNoApi(const std::string& caseId) {
    std::vector<CaseResult> results;
    Harness h;
    if (!h.setup()) {
        return {{"I1-I4", false, "prototype harness setup failed"}};
    }
    for (const IndirectCase& indirectCase : workload::indirectCases()) {
        if (caseId != "all" && caseId != indirectCase.id) {
            continue;
        }
        if (indirectCase.kind == IndirectKind::DrawIndirect) {
            results.push_back(runDrawCase(h, "I1", false));
        } else if (indirectCase.kind == IndirectKind::DrawIndexedIndirect) {
            results.push_back(runDrawCase(h, "I2", true));
        } else if (indirectCase.argSource == IndirectArgSource::CpuWritten) {
            results.push_back(runDispatchCase(h, "I3", false));
        } else {
            results.push_back(runDispatchCase(h, "I4", true));
        }
    }
    return results;
}

} // namespace lmx::experimental::noapi::bench
