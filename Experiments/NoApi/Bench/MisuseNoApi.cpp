//----------------------------------------------------------------------------------------------------------------------
/// @file MisuseNoApi.cpp
/// @brief Implements M1-M6's misuse triggers (spec section 7) against the address-first prototype.
///        Each function is expected to LMX_ASSERT and abort the process; StressRunner.cpp's
///        runMisuse re-execs this binary in a child process and captures the death.
///
///        M1 mirrors the prototype's own scope contract rather than the RHI's: NoApi's dispatch()
///        asserts when called *inside a render pass* (RHI's dispatch has no render-pass concept to
///        violate; its equivalent contract is "outside a compute pass," which MisuseRhi.cpp
///        triggers). Both are the same family of misuse -- a dispatch recorded in the wrong scope
///        -- realized against each interface's own pass-scope vocabulary.
//----------------------------------------------------------------------------------------------------------------------

#include "Bench/StressRunner.h"
#include "Tests/TestShaders.h"

#include "NoApi/NoApi.h"

#include <string>

namespace lmx::noapi::bench {
namespace {

//======================================================================================================================
void triggerM1() { // Pass-scope violation: dispatch recorded inside a render pass.
    Device* device = *createDevice({.label = "misuse.m1"});
    Queue* queue = mainQueue(device);
    Allocation rootStorage = *allocate(
        device,
        {.size = 4096, .alignment = 256, .kind = MemoryKind::Shared, .label = "misuse.m1.root"});
    LinearAllocator root(rootStorage);
    Allocation textureMem = *allocate(device, {.size = 65536,
                                               .alignment = 65536,
                                               .kind = MemoryKind::Private,
                                               .label = "misuse.m1.textureMem"});
    Texture* target = *createTexture(device,
                                     {.kind = TextureKind::Texture2D,
                                      .extent = {4, 4, 1},
                                      .format = Format::RGBA8Unorm,
                                      .usage = TextureUsage::ColorAttachment,
                                      .label = "misuse.m1.target"},
                                     textureMem.gpu);
    Pipeline* pipeline = *createComputePipeline(
        device, {.compute = {.ir = test::shaderSource(), .entryPoint = "lmxFillBufferKernel"},
                 .label = "misuse.m1.pipeline"});

    CommandBuffer* commands = beginCommands(queue, &root, "misuse.m1");
    const std::array<ColorAttachment, 1> colorTargets{
        ColorAttachment{.texture = target, .load = LoadAction::Clear, .store = StoreAction::Store}};
    beginRenderPass(commands, {.colorTargets = colorTargets, .label = "misuse.m1.pass"});
    setPipeline(commands, pipeline);
    const test::FillRoot fillRoot{.out = 0, .count = 1, .base = 0};
    const GpuAddress rootAddress = pushRoot(commands, fillRoot);
    dispatch(commands, rootAddress, 1, 1, 1); // Inside an open render pass: expected to assert.
}

//======================================================================================================================
void triggerM2() { // Usage mismatch: a texture created without Storage usage bound for storage
                   // write.
    Device* device = *createDevice({.label = "misuse.m2"});
    Result<BindlessTable*> table =
        createBindlessTable(device, {.slotCount = 4, .label = "misuse.m2.table"});
    Allocation textureMem = *allocate(device, {.size = 65536,
                                               .alignment = 65536,
                                               .kind = MemoryKind::Private,
                                               .label = "misuse.m2.textureMem"});
    Texture* texture =
        *createTexture(device,
                       {.kind = TextureKind::Texture2D,
                        .extent = {4, 4, 1},
                        .format = Format::RGBA8Unorm,
                        .usage = TextureUsage::Sampled, // Storage deliberately omitted.
                        .label = "misuse.m2.texture"},
                       textureMem.gpu);
    writeTextureSlot(*table, 0, texture, {.storage = true}); // Expected to assert.
}

//======================================================================================================================
void triggerM3() { // View/range overflow: a view whose mip range exceeds the texture's mip count.
    Device* device = *createDevice({.label = "misuse.m3"});
    Result<BindlessTable*> table =
        createBindlessTable(device, {.slotCount = 4, .label = "misuse.m3.table"});
    Allocation textureMem = *allocate(device, {.size = 65536,
                                               .alignment = 65536,
                                               .kind = MemoryKind::Private,
                                               .label = "misuse.m3.textureMem"});
    Texture* texture = *createTexture(device,
                                      {.kind = TextureKind::Texture2D,
                                       .extent = {4, 4, 1},
                                       .mipCount = 1,
                                       .format = Format::RGBA8Unorm,
                                       .usage = TextureUsage::Sampled,
                                       .label = "misuse.m3.texture"},
                                      textureMem.gpu);
    writeTextureSlot(*table, 0, texture,
                     {.baseMipLevel = 5, .mipCount = 1}); // 5 exceeds the texture's 1 level.
}

//======================================================================================================================
void triggerM4() { // Alignment violation: pushRoot with a non-power-of-two alignment.
    Device* device = *createDevice({.label = "misuse.m4"});
    Queue* queue = mainQueue(device);
    Allocation rootStorage = *allocate(
        device,
        {.size = 4096, .alignment = 256, .kind = MemoryKind::Shared, .label = "misuse.m4.root"});
    LinearAllocator root(rootStorage);
    CommandBuffer* commands = beginCommands(queue, &root, "misuse.m4");
    const uint32_t payload = 0;
    pushRoot(commands, &payload, sizeof(payload),
             /*alignment=*/3); // Not a power of two: expected to assert.
}

//======================================================================================================================
void triggerM5() { // Invalid handle: destroying a texture while a bindless slot still references
                   // it.
    Device* device = *createDevice({.label = "misuse.m5"});
    Result<BindlessTable*> table =
        createBindlessTable(device, {.slotCount = 4, .label = "misuse.m5.table"});
    Allocation textureMem = *allocate(device, {.size = 65536,
                                               .alignment = 65536,
                                               .kind = MemoryKind::Private,
                                               .label = "misuse.m5.textureMem"});
    Texture* texture = *createTexture(device,
                                      {.kind = TextureKind::Texture2D,
                                       .extent = {4, 4, 1},
                                       .format = Format::RGBA8Unorm,
                                       .usage = TextureUsage::Sampled,
                                       .label = "misuse.m5.texture"},
                                      textureMem.gpu);
    writeTextureSlot(*table, 0, texture, {});
    destroyTexture(device, texture); // Slot 0 was never cleared: expected to assert.
}

//======================================================================================================================
void triggerM6() { // Retired frame-slot use: the frame ring's root allocator used with no frame
                   // open.
    Device* device = *createDevice({.label = "misuse.m6"});
    Result<FrameRing> ring =
        FrameRing::create(device, {.bytesPerFrame = 4096, .label = "misuse.m6.ring"});
    ring->rootAllocator(); // No beginFrame() has been called: expected to assert.
}

} // namespace

//======================================================================================================================
int runMisuseChildNoApi(const std::string& caseId) {
    if (caseId == "M1") {
        triggerM1();
    } else if (caseId == "M2") {
        triggerM2();
    } else if (caseId == "M3") {
        triggerM3();
    } else if (caseId == "M4") {
        triggerM4();
    } else if (caseId == "M5") {
        triggerM5();
    } else if (caseId == "M6") {
        triggerM6();
    } else {
        return 2;
    }
    return 1; // Reaching here means the misuse did not abort the process, which is itself a
              // failure.
}

} // namespace lmx::noapi::bench
