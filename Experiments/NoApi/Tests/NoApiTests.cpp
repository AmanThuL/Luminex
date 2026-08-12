//----------------------------------------------------------------------------------------------------------------------
/// @file NoApiTests.cpp
/// @brief Smoke-tests the address-first prototype end to end on Metal 4 with readback oracles.
//----------------------------------------------------------------------------------------------------------------------

#include "TestShaders.h"

#include "NoApi/NoApi.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace lmx::noapi::test {
namespace {

constexpr uint32_t kTargetExtent = 64;
constexpr uint64_t kTargetRowBytes = uint64_t{kTargetExtent} * 4;
constexpr uint32_t kTextureSlotA = 0;
constexpr uint32_t kTextureSlotB = 1;
constexpr uint32_t kSamplerSlot = 2;
constexpr uint32_t kStorageSlot = 3;
constexpr uint32_t kSampledStorageSlot = 4;
constexpr uint32_t kTableSlotCount = 8;

// The triangle every raster case draws: one oversized primitive covering the whole target.
constexpr std::array<Vertex, 3> kTriangle{
    Vertex{.position = {-1.0f, -1.0f, 0.5f, 1.0f}, .uv = {0.0f, 1.0f}, .pad = {0.0f, 0.0f}},
    Vertex{.position = {3.0f, -1.0f, 0.5f, 1.0f}, .uv = {2.0f, 1.0f}, .pad = {0.0f, 0.0f}},
    Vertex{.position = {-1.0f, 3.0f, 0.5f, 1.0f}, .uv = {0.0f, -1.0f}, .pad = {0.0f, 0.0f}}};

// Metal's indirect draw and dispatch argument blocks, declared here because the prototype passes
// them by address and never names their type.
struct DrawArguments {
    uint32_t vertexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t vertexStart = 0;
    uint32_t baseInstance = 0;
};

struct DrawIndexedArguments {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t indexStart = 0;
    int32_t baseVertex = 0;
    uint32_t baseInstance = 0;
};

struct DispatchArguments {
    uint32_t groupsX = 0;
    uint32_t groupsY = 0;
    uint32_t groupsZ = 0;
};

// Owns one device and the objects every case shares, so each test records commands rather than
// rebuilding the world.
class Harness {
public:
    //==================================================================================================================
    Harness() {
        Result<Device*> device = createDevice({.label = "lmx.noapi.tests"});
        REQUIRE(device.has_value());
        m_device = *device;
        m_queue = mainQueue(m_device);

        Result<ResidencySet*> residency =
            createResidencySet(m_device, {.initialCapacity = 8, .label = "lmx.noapi.residency"});
        REQUIRE(residency.has_value());
        m_residency = *residency;

        Result<BindlessTable*> table = createBindlessTable(
            m_device, {.slotCount = kTableSlotCount, .label = "lmx.noapi.table"});
        REQUIRE(table.has_value());
        m_table = *table;

        Result<Semaphore*> fence = createSemaphore(m_device, 0, "lmx.noapi.fence");
        REQUIRE(fence.has_value());
        m_fence = *fence;

        m_rootStorage = allocateOrFail(64 * 1024, 256, MemoryKind::Shared, "lmx.noapi.rootRing");
        m_root = LinearAllocator(m_rootStorage);
        m_uploadStorage = allocateOrFail(64 * 1024, 256, MemoryKind::Shared, "lmx.noapi.upload");
        m_upload = LinearAllocator(m_uploadStorage);
        m_readbackStorage =
            allocateOrFail(256 * 1024, 256, MemoryKind::Readback, "lmx.noapi.readback");
        m_textureStorage =
            allocateOrFail(8 * 1024 * 1024, 65536, MemoryKind::Private, "lmx.noapi.textures");

        commitResidency(m_residency);
    }

    //==================================================================================================================
    ~Harness() {
        for (uint32_t slot = 0; slot < kTableSlotCount; ++slot) {
            clearBindlessSlot(m_table, slot);
        }
        for (Texture* texture : m_textures) {
            destroyTexture(m_device, texture);
        }
        for (Pipeline* pipeline : m_pipelines) {
            destroyPipeline(m_device, pipeline);
        }
        for (Sampler* sampler : m_samplers) {
            destroySampler(m_device, sampler);
        }
        for (DepthStencilState* state : m_depthStates) {
            destroyDepthStencilState(m_device, state);
        }
        destroyBindlessTable(m_device, m_table);
        destroySemaphore(m_device, m_fence);
        deallocate(m_device, m_textureStorage);
        deallocate(m_device, m_readbackStorage);
        deallocate(m_device, m_uploadStorage);
        deallocate(m_device, m_rootStorage);
        destroyResidencySet(m_device, m_residency);
        destroyDevice(m_device);
    }

    //==================================================================================================================
    Harness(const Harness&) = delete;
    //==================================================================================================================
    Harness& operator=(const Harness&) = delete;

    //==================================================================================================================
    Device* device() { return m_device; }
    //==================================================================================================================
    Queue* queue() { return m_queue; }
    //==================================================================================================================
    BindlessTable* table() { return m_table; }
    //==================================================================================================================
    ResidencySet* residency() { return m_residency; }
    //==================================================================================================================
    Semaphore* fence() { return m_fence; }
    //==================================================================================================================
    const Allocation& readbackStorage() const { return m_readbackStorage; }
    //==================================================================================================================
    LinearAllocator& rootAllocator() { return m_root; }

    //==================================================================================================================
    Allocation allocateOrFail(uint64_t size, uint64_t alignment, MemoryKind kind,
                              std::string_view label) {
        Result<Allocation> allocation = allocate(
            m_device, {.size = size, .alignment = alignment, .kind = kind, .label = label});
        REQUIRE(allocation.has_value());
        return *allocation;
    }

    //==================================================================================================================
    // Suballocates upload memory and copies `value` into it, returning the address pair.
    template <typename T>
    Suballocation stage(const T& value) {
        const Suballocation storage =
            m_upload.allocate(sizeof(T), alignof(T) < 16 ? 16 : alignof(T));
        std::memcpy(storage.cpu, &value, sizeof(T));
        return storage;
    }

    //==================================================================================================================
    Texture* makeTexture(const TextureDesc& desc) {
        const SizeAlign required = textureSizeAlign(m_device, desc);
        const uint64_t base = m_textureStorage.gpu;
        const uint64_t aligned =
            (m_textureCursor + required.alignment - 1) & ~(required.alignment - 1);
        REQUIRE(aligned + required.size <= m_textureStorage.size);
        Result<Texture*> texture = createTexture(m_device, desc, base + aligned);
        REQUIRE(texture.has_value());
        m_textureCursor = aligned + required.size;
        m_textures.push_back(*texture);
        return *texture;
    }

    //==================================================================================================================
    Texture* makeColorTarget(std::string_view label) {
        return makeTexture({.kind = TextureKind::Texture2D,
                            .extent = {.width = kTargetExtent, .height = kTargetExtent, .depth = 1},
                            .mipCount = 1,
                            .arrayLayers = 1,
                            .sampleCount = 1,
                            .format = Format::RGBA8Unorm,
                            .usage = TextureUsage::ColorAttachment | TextureUsage::CopySource,
                            .label = label});
    }

    //==================================================================================================================
    Pipeline* makeGraphics(std::string_view vertexEntry, std::string_view pixelEntry,
                           Format depthFormat, std::string_view label) {
        const std::array<ColorTargetDesc, 1> targets{
            ColorTargetDesc{.format = Format::RGBA8Unorm, .writeMask = 0xF}};
        Result<Pipeline*> pipeline = createGraphicsPipeline(
            m_device, {.vertex = {.ir = shaderSource(), .entryPoint = vertexEntry},
                       .pixel = {.ir = shaderSource(), .entryPoint = pixelEntry},
                       .raster = {.topology = Topology::TriangleList,
                                  .sampleCount = 1,
                                  .alphaToCoverage = false,
                                  .depthFormat = depthFormat,
                                  .colorTargets = targets,
                                  .blend = nullptr},
                       .specConstants = {},
                       .label = label});
        REQUIRE(pipeline.has_value());
        m_pipelines.push_back(*pipeline);
        return *pipeline;
    }

    //==================================================================================================================
    Pipeline* makeCompute(std::string_view entry, std::string_view label) {
        Result<Pipeline*> pipeline =
            createComputePipeline(m_device, {.compute = {.ir = shaderSource(), .entryPoint = entry},
                                             .specConstants = {},
                                             .label = label});
        REQUIRE(pipeline.has_value());
        m_pipelines.push_back(*pipeline);
        return *pipeline;
    }

    //==================================================================================================================
    Sampler* makeSampler(std::string_view label) {
        Result<Sampler*> sampler = createSampler(m_device, {.minFilter = FilterMode::Nearest,
                                                            .magFilter = FilterMode::Nearest,
                                                            .mipFilter = FilterMode::Nearest,
                                                            .addressU = AddressMode::ClampToEdge,
                                                            .addressV = AddressMode::ClampToEdge,
                                                            .addressW = AddressMode::ClampToEdge,
                                                            .maxAnisotropy = 1,
                                                            .compare = false,
                                                            .compareOp = CompareOp::Less,
                                                            .label = label});
        REQUIRE(sampler.has_value());
        m_samplers.push_back(*sampler);
        return *sampler;
    }

    //==================================================================================================================
    DepthStencilState* makeDepthState(std::string_view label) {
        Result<DepthStencilState*> state =
            createDepthStencilState(m_device, {.depthTestEnabled = true,
                                               .depthWriteEnabled = true,
                                               .depthTest = CompareOp::LessEqual,
                                               .stencilEnabled = false,
                                               .stencilReadMask = 0xFF,
                                               .stencilWriteMask = 0xFF,
                                               .front = {},
                                               .back = {},
                                               .label = label});
        REQUIRE(state.has_value());
        m_depthStates.push_back(*state);
        return *state;
    }

    //==================================================================================================================
    CommandBuffer* begin(std::string_view label) {
        m_root.reset();
        return beginCommands(m_queue, &m_root, label);
    }

    //==================================================================================================================
    void submitAndWait(CommandBuffer* commands) {
        endCommands(commands);
        const std::array<CommandBuffer*, 1> list{commands};
        m_fenceValue += 1;
        submit(m_queue, list, m_fence, m_fenceValue);
        waitSemaphore(m_fence, m_fenceValue);
    }

    //==================================================================================================================
    // Orders the pass that produced `texture` before the copy, then reads one row band back.
    void readTexture(CommandBuffer* commands, const Texture* texture, GpuAddress destination,
                     uint32_t height) {
        barrier(commands, Stage::RasterColorOut | Stage::Compute, Stage::Copy);
        copyFromTexture(commands, destination, {.bytesPerRow = kTargetRowBytes, .bytesPerImage = 0},
                        texture,
                        {.mipLevel = 0,
                         .arrayLayer = 0,
                         .origin = {},
                         .extent = {.width = kTargetExtent, .height = height, .depth = 1}});
    }

    //==================================================================================================================
    const uint8_t* readbackBytes(uint64_t offset) const {
        return static_cast<const uint8_t*>(m_readbackStorage.cpu) + offset;
    }

    //==================================================================================================================
    GpuAddress readbackAddress(uint64_t offset) const { return m_readbackStorage.gpu + offset; }

private:
    Device* m_device = nullptr;
    Queue* m_queue = nullptr;
    ResidencySet* m_residency = nullptr;
    BindlessTable* m_table = nullptr;
    Semaphore* m_fence = nullptr;
    uint64_t m_fenceValue = 0;

    Allocation m_rootStorage{};
    Allocation m_uploadStorage{};
    Allocation m_readbackStorage{};
    Allocation m_textureStorage{};
    LinearAllocator m_root;
    LinearAllocator m_upload;
    uint64_t m_textureCursor = 0;

    std::vector<Texture*> m_textures;
    std::vector<Pipeline*> m_pipelines;
    std::vector<Sampler*> m_samplers;
    std::vector<DepthStencilState*> m_depthStates;
};

//======================================================================================================================
// Uploads a solid RGBA8 color into a small sampled texture and leaves it ready to sample.
Texture* makeSolidTexture(Harness& harness, CommandBuffer* commands, std::array<uint8_t, 4> color,
                          std::string_view label) {
    constexpr uint32_t kExtent = 4;
    Texture* texture =
        harness.makeTexture({.kind = TextureKind::Texture2D,
                             .extent = {.width = kExtent, .height = kExtent, .depth = 1},
                             .mipCount = 1,
                             .arrayLayers = 1,
                             .sampleCount = 1,
                             .format = Format::RGBA8Unorm,
                             .usage = TextureUsage::Sampled | TextureUsage::CopyDestination,
                             .label = label});

    std::array<uint8_t, kExtent * kExtent * 4> pixels{};
    for (uint32_t texel = 0; texel < kExtent * kExtent; ++texel) {
        std::memcpy(pixels.data() + texel * 4, color.data(), color.size());
    }
    const Suballocation staging = harness.stage(pixels);
    copyToTexture(commands, texture,
                  {.mipLevel = 0,
                   .arrayLayer = 0,
                   .origin = {},
                   .extent = {.width = kExtent, .height = kExtent, .depth = 1}},
                  staging.gpu, {.bytesPerRow = kExtent * 4, .bytesPerImage = 0});
    return texture;
}

} // namespace

//======================================================================================================================
TEST_CASE_METHOD(Harness, "the device reports the Metal 4 values a caller must not guess",
                 "[noapi][smoke]") {
    const Capabilities& caps = capabilities(device());

    // A resource ID is 64-bit, so the model's 32-bit slot costs eight bytes of table space here.
    REQUIRE(caps.bindlessSlotStride == 8);
    REQUIRE(caps.maxBindlessSlots >= kTableSlotCount);
    REQUIRE(caps.minRootDataAlignment == 4);
    REQUIRE(caps.minCopyAlignment == 4);
    REQUIRE(caps.minIndirectAlignment == 4);
    REQUIRE_FALSE(caps.separateBlendState);
    REQUIRE_FALSE(caps.memoryBackedFences);
    REQUIRE(caps.residencyRequired);
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "allocations hand back addresses the caller suballocates",
                 "[noapi][smoke]") {
    const uint64_t before = residentBytes(residency());

    const Allocation shared =
        allocateOrFail(4096, 256, MemoryKind::Shared, "lmx.noapi.test.shared");
    const Allocation privateMemory =
        allocateOrFail(65536, 4096, MemoryKind::Private, "lmx.noapi.test.private");
    const Allocation readback =
        allocateOrFail(4096, 16, MemoryKind::Readback, "lmx.noapi.test.readback");

    REQUIRE(shared.cpu != nullptr);
    REQUIRE(readback.cpu != nullptr);
    // Private memory is reachable only through copy commands, which is what a null host address
    // means here.
    REQUIRE(privateMemory.cpu == nullptr);
    REQUIRE(shared.gpu != kNullAddress);
    REQUIRE(shared.gpu % 256 == 0);
    REQUIRE(privateMemory.gpu % 4096 == 0);
    REQUIRE(residentBytes(residency()) > before);

    // Pointer arithmetic on an address is the whole suballocation model.
    LinearAllocator allocator(shared);
    const Suballocation first = allocator.allocate(64, 16);
    const Suballocation second = allocator.allocate(64, 16);
    REQUIRE(first.gpu == shared.gpu);
    REQUIRE(second.gpu == shared.gpu + 64);
    REQUIRE(static_cast<uint8_t*>(second.cpu) - static_cast<uint8_t*>(first.cpu) == 64);

    deallocate(device(), readback);
    deallocate(device(), privateMemory);
    deallocate(device(), shared);
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "root data reaches both shader stages by address", "[noapi][smoke]") {
    Texture* target = makeColorTarget("lmx.noapi.test.rootTarget");
    Texture* depth =
        makeTexture({.kind = TextureKind::Texture2D,
                     .extent = {.width = kTargetExtent, .height = kTargetExtent, .depth = 1},
                     .mipCount = 1,
                     .arrayLayers = 1,
                     .sampleCount = 1,
                     .format = Format::D32Float,
                     .usage = TextureUsage::DepthStencilAttachment,
                     .label = "lmx.noapi.test.rootDepth"});
    Pipeline* pipeline =
        makeGraphics("lmxTriangleVs", "lmxSolidFs", Format::D32Float, "lmx.noapi.test.solid");
    DepthStencilState* depthState = makeDepthState("lmx.noapi.test.depthState");

    CommandBuffer* commands = begin("lmx.noapi.test.rootData");
    pushDebugGroup(commands, "root-data");
    const Suballocation vertices = stage(kTriangle);

    const VertexRoot vertexRoot{.tint = {1.0f, 0.5f, 0.25f, 1.0f}, .vertices = vertices.gpu};
    const SolidPixelRoot pixelRoot{.scale = {1.0f, 1.0f, 1.0f, 1.0f}};
    const GpuAddress vertexAddress = pushRoot(commands, vertexRoot);
    const GpuAddress pixelAddress = pushRoot(commands, pixelRoot);

    const std::array<ColorAttachment, 1> colorTargets{
        ColorAttachment{.texture = target,
                        .mipLevel = 0,
                        .arrayLayer = 0,
                        .load = LoadAction::Clear,
                        .store = StoreAction::Store,
                        .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}};
    const DepthAttachment depthTarget{.texture = depth,
                                      .mipLevel = 0,
                                      .arrayLayer = 0,
                                      .load = LoadAction::Clear,
                                      .store = StoreAction::DontCare,
                                      .clearDepth = 1.0f};
    beginRenderPass(
        commands,
        {.colorTargets = colorTargets, .depth = &depthTarget, .label = "lmx.noapi.test.pass"});
    setPipeline(commands, pipeline);
    setDepthStencilState(commands, depthState);
    setViewport(commands, {.x = 0.0f,
                           .y = 0.0f,
                           .width = static_cast<float>(kTargetExtent),
                           .height = static_cast<float>(kTargetExtent),
                           .minDepth = 0.0f,
                           .maxDepth = 1.0f});
    setScissor(commands, {.x = 0, .y = 0, .width = kTargetExtent, .height = kTargetExtent});
    setCullMode(commands, CullMode::None);
    setFrontFace(commands, Winding::CounterClockwise);
    draw(commands, vertexAddress, pixelAddress, 3);
    endRenderPass(commands);
    popDebugGroup(commands);

    readTexture(commands, target, readbackAddress(0), 1);
    submitAndWait(commands);

    const uint8_t* pixel = readbackBytes(0);
    REQUIRE(pixel[0] == 255);
    REQUIRE(pixel[1] == 128);
    REQUIRE(pixel[2] == 64);
    REQUIRE(pixel[3] == 255);
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "a bindless slot samples the texture it names", "[noapi][smoke]") {
    Texture* target = makeColorTarget("lmx.noapi.test.bindlessTarget");
    Pipeline* pipeline = makeGraphics("lmxTriangleVs", "lmxBindlessFs", Format::Undefined,
                                      "lmx.noapi.test.bindless");
    Sampler* sampler = makeSampler("lmx.noapi.test.sampler");

    CommandBuffer* commands = begin("lmx.noapi.test.bindless");
    Texture* red = makeSolidTexture(*this, commands, {255, 0, 0, 255}, "lmx.noapi.test.red");
    Texture* green = makeSolidTexture(*this, commands, {0, 255, 0, 255}, "lmx.noapi.test.green");

    const TextureHandle redHandle = writeTextureSlot(table(), kTextureSlotA, red, {});
    const TextureHandle greenHandle = writeTextureSlot(table(), kTextureSlotB, green, {});
    const SamplerHandle samplerHandle = writeSamplerSlot(table(), kSamplerSlot, sampler);
    REQUIRE(redHandle.slot == kTextureSlotA);
    REQUIRE(greenHandle.generation >= 1);

    setBindlessTable(commands, table());
    const Suballocation vertices = stage(kTriangle);
    const VertexRoot vertexRoot{.tint = {1.0f, 1.0f, 1.0f, 1.0f}, .vertices = vertices.gpu};
    // The table is addressable memory, so the same address serves as the sampler array too.
    const BindlessPixelRoot pixelRoot{.samplers = bindlessTableAddress(table()),
                                      .textureSlot = greenHandle.slot,
                                      .samplerSlot = samplerHandle.slot};
    const GpuAddress vertexAddress = pushRoot(commands, vertexRoot);
    const GpuAddress pixelAddress = pushRoot(commands, pixelRoot);

    // The upload wrote the texture through a copy; the sample is a shader read of it.
    barrier(commands, Stage::Copy, Stage::PixelShader, Hazard::Descriptors);

    const std::array<ColorAttachment, 1> colorTargets{
        ColorAttachment{.texture = target,
                        .mipLevel = 0,
                        .arrayLayer = 0,
                        .load = LoadAction::Clear,
                        .store = StoreAction::Store,
                        .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}};
    beginRenderPass(
        commands,
        {.colorTargets = colorTargets, .depth = nullptr, .label = "lmx.noapi.test.bindlessPass"});
    setPipeline(commands, pipeline);
    draw(commands, vertexAddress, pixelAddress, 3);
    endRenderPass(commands);

    readTexture(commands, target, readbackAddress(0), 1);
    submitAndWait(commands);

    const uint8_t* pixel = readbackBytes(0);
    REQUIRE(pixel[0] == 0);
    REQUIRE(pixel[1] == 255);
    REQUIRE(pixel[2] == 0);
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "a compute dispatch writes through a storage address", "[noapi][smoke]") {
    Pipeline* pipeline = makeCompute("lmxFillBufferKernel", "lmx.noapi.test.fill");
    const Allocation storage =
        allocateOrFail(1024, 256, MemoryKind::Shared, "lmx.noapi.test.storage");
    commitResidency(residency());

    CommandBuffer* commands = begin("lmx.noapi.test.dispatch");
    const FillRoot root{.out = storage.gpu, .count = 64, .base = 100};
    const GpuAddress rootAddress = pushRoot(commands, root);
    setPipeline(commands, pipeline);
    dispatch(commands, rootAddress, 1, 1, 1);

    barrier(commands, Stage::Compute, Stage::Copy);
    copyMemory(commands, readbackAddress(0), storage.gpu, 64 * sizeof(uint32_t));
    submitAndWait(commands);

    const auto* values = reinterpret_cast<const uint32_t*>(readbackBytes(0));
    REQUIRE(values[0] == 100);
    REQUIRE(values[63] == 163);

    deallocate(device(), storage);
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "copies round-trip through private memory and fills write a pattern",
                 "[noapi][smoke]") {
    const Allocation source =
        allocateOrFail(1024, 256, MemoryKind::Shared, "lmx.noapi.test.copySource");
    const Allocation staging =
        allocateOrFail(1024, 256, MemoryKind::Private, "lmx.noapi.test.copyStaging");
    commitResidency(residency());

    auto* words = static_cast<uint32_t*>(source.cpu);
    for (uint32_t index = 0; index < 64; ++index) {
        words[index] = 0xA0000000u + index;
    }

    CommandBuffer* commands = begin("lmx.noapi.test.copy");
    copyMemory(commands, staging.gpu, source.gpu, 256);
    barrier(commands, Stage::Copy, Stage::Copy);
    copyMemory(commands, readbackAddress(0), staging.gpu, 256);
    fillMemory(commands, readbackAddress(512), 256, 0xAB);
    submitAndWait(commands);

    const auto* roundTripped = reinterpret_cast<const uint32_t*>(readbackBytes(0));
    REQUIRE(roundTripped[0] == 0xA0000000u);
    REQUIRE(roundTripped[63] == 0xA000003Fu);
    REQUIRE(readbackBytes(512)[0] == 0xAB);
    REQUIRE(readbackBytes(512)[255] == 0xAB);

    deallocate(device(), staging);
    deallocate(device(), source);
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "indexed and indirect draws read their arguments from memory",
                 "[noapi][smoke]") {
    Texture* target = makeColorTarget("lmx.noapi.test.indirectTarget");
    Pipeline* pipeline =
        makeGraphics("lmxTriangleVs", "lmxSolidFs", Format::Undefined, "lmx.noapi.test.indirect");

    CommandBuffer* commands = begin("lmx.noapi.test.indirect");
    const Suballocation vertices = stage(kTriangle);
    const std::array<uint32_t, 3> indexData{0, 1, 2};
    const Suballocation indices = stage(indexData);
    // The frozen indirect cases place arguments at a non-zero offset, which is plain pointer
    // arithmetic on the allocation's address here.
    const Suballocation drawArgs = stage(DrawArguments{.vertexCount = 3, .instanceCount = 1});
    const Suballocation indexedArgs =
        stage(DrawIndexedArguments{.indexCount = 3, .instanceCount = 1});

    const VertexRoot vertexRoot{.tint = {0.0f, 0.0f, 1.0f, 1.0f}, .vertices = vertices.gpu};
    const SolidPixelRoot pixelRoot{.scale = {1.0f, 1.0f, 1.0f, 1.0f}};
    const GpuAddress vertexAddress = pushRoot(commands, vertexRoot);
    const GpuAddress pixelAddress = pushRoot(commands, pixelRoot);

    const std::array<ColorAttachment, 1> colorTargets{
        ColorAttachment{.texture = target,
                        .mipLevel = 0,
                        .arrayLayer = 0,
                        .load = LoadAction::Clear,
                        .store = StoreAction::Store,
                        .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}};
    beginRenderPass(
        commands,
        {.colorTargets = colorTargets, .depth = nullptr, .label = "lmx.noapi.test.indirectPass"});
    setPipeline(commands, pipeline);
    drawIndexed(commands, vertexAddress, pixelAddress, indices.gpu, IndexKind::Uint32, 3);
    drawIndirect(commands, vertexAddress, pixelAddress, drawArgs.gpu);
    drawIndexedIndirect(commands, vertexAddress, pixelAddress, indices.gpu, IndexKind::Uint32,
                        indexedArgs.gpu);
    endRenderPass(commands);

    readTexture(commands, target, readbackAddress(0), 1);
    submitAndWait(commands);

    const uint8_t* pixel = readbackBytes(0);
    REQUIRE(pixel[2] == 255);
    REQUIRE(pixel[0] == 0);
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "an indirect dispatch reads its threadgroup counts from memory",
                 "[noapi][smoke]") {
    Pipeline* pipeline = makeCompute("lmxFillBufferKernel", "lmx.noapi.test.indirectFill");
    const Allocation storage =
        allocateOrFail(1024, 256, MemoryKind::Shared, "lmx.noapi.test.indirectStorage");
    commitResidency(residency());
    std::memset(storage.cpu, 0, 1024);

    CommandBuffer* commands = begin("lmx.noapi.test.dispatchIndirect");
    const Suballocation arguments =
        stage(DispatchArguments{.groupsX = 1, .groupsY = 1, .groupsZ = 1});
    const FillRoot root{.out = storage.gpu, .count = 64, .base = 7};
    const GpuAddress rootAddress = pushRoot(commands, root);

    setPipeline(commands, pipeline);
    // Nothing on the GPU wrote these arguments, so no DrawArguments hazard is owed here.
    dispatchIndirect(commands, rootAddress, arguments.gpu);
    barrier(commands, Stage::Compute, Stage::Copy);
    copyMemory(commands, readbackAddress(0), storage.gpu, 64 * sizeof(uint32_t));
    submitAndWait(commands);

    const auto* values = reinterpret_cast<const uint32_t*>(readbackBytes(0));
    REQUIRE(values[0] == 7);
    REQUIRE(values[63] == 70);

    deallocate(device(), storage);
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "a barrier orders a compute write before a raster sample",
                 "[noapi][smoke]") {
    Texture* target = makeColorTarget("lmx.noapi.test.hazardTarget");
    Texture* image =
        makeTexture({.kind = TextureKind::Texture2D,
                     .extent = {.width = kTargetExtent, .height = kTargetExtent, .depth = 1},
                     .mipCount = 1,
                     .arrayLayers = 1,
                     .sampleCount = 1,
                     .format = Format::RGBA8Unorm,
                     .usage = TextureUsage::Storage | TextureUsage::Sampled,
                     .label = "lmx.noapi.test.image"});
    Pipeline* writer = makeCompute("lmxWriteImageKernel", "lmx.noapi.test.imageWriter");
    Pipeline* reader = makeGraphics("lmxTriangleVs", "lmxBindlessFs", Format::Undefined,
                                    "lmx.noapi.test.imageReader");
    Sampler* sampler = makeSampler("lmx.noapi.test.hazardSampler");

    writeTextureSlot(table(), kStorageSlot, image, {.storage = true});
    writeTextureSlot(table(), kSampledStorageSlot, image, {});
    writeSamplerSlot(table(), kSamplerSlot, sampler);

    CommandBuffer* commands = begin("lmx.noapi.test.hazard");
    setBindlessTable(commands, table());

    const ImageRoot imageRoot{.slot = kStorageSlot,
                              .width = kTargetExtent,
                              .height = kTargetExtent,
                              .pad = 0,
                              .color = {0.25f, 0.5f, 0.75f, 1.0f}};
    const GpuAddress imageRootAddress = pushRoot(commands, imageRoot);
    setPipeline(commands, writer);
    dispatch(commands, imageRootAddress, kTargetExtent / 8, kTargetExtent / 8, 1);

    // Without this the sample below may read texels the dispatch has not written yet.
    barrier(commands, Stage::Compute, Stage::PixelShader);

    const Suballocation vertices = stage(kTriangle);
    const VertexRoot vertexRoot{.tint = {1.0f, 1.0f, 1.0f, 1.0f}, .vertices = vertices.gpu};
    const BindlessPixelRoot pixelRoot{.samplers = bindlessTableAddress(table()),
                                      .textureSlot = kSampledStorageSlot,
                                      .samplerSlot = kSamplerSlot};
    const GpuAddress vertexAddress = pushRoot(commands, vertexRoot);
    const GpuAddress pixelAddress = pushRoot(commands, pixelRoot);

    const std::array<ColorAttachment, 1> colorTargets{
        ColorAttachment{.texture = target,
                        .mipLevel = 0,
                        .arrayLayer = 0,
                        .load = LoadAction::Clear,
                        .store = StoreAction::Store,
                        .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}};
    beginRenderPass(
        commands,
        {.colorTargets = colorTargets, .depth = nullptr, .label = "lmx.noapi.test.hazardPass"});
    setPipeline(commands, reader);
    draw(commands, vertexAddress, pixelAddress, 3);
    endRenderPass(commands);

    readTexture(commands, target, readbackAddress(0), 1);
    submitAndWait(commands);

    const uint8_t* pixel = readbackBytes(0);
    REQUIRE(pixel[0] == 64);
    REQUIRE(pixel[1] == 128);
    REQUIRE(pixel[2] == 191);
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "a split barrier signals and waits on a counter in memory",
                 "[noapi][smoke]") {
    Pipeline* pipeline = makeCompute("lmxFillBufferKernel", "lmx.noapi.test.splitFill");
    const Allocation storage =
        allocateOrFail(1024, 256, MemoryKind::Shared, "lmx.noapi.test.splitStorage");
    const Allocation counters =
        allocateOrFail(64, 16, MemoryKind::Readback, "lmx.noapi.test.counters");
    commitResidency(residency());
    std::memset(counters.cpu, 0, 64);

    CommandBuffer* commands = begin("lmx.noapi.test.split");
    const FillRoot root{.out = storage.gpu, .count = 64, .base = 1};
    const GpuAddress rootAddress = pushRoot(commands, root);
    setPipeline(commands, pipeline);
    dispatch(commands, rootAddress, 1, 1, 1);

    signalAfter(commands, Stage::Compute, counters.gpu, 0x1234, SignalOp::Set);
    signalAfter(commands, Stage::Compute, counters.gpu + 8, 5, SignalOp::AtomicMax);
    signalAfter(commands, Stage::Compute, counters.gpu + 8, 9, SignalOp::AtomicMax);
    waitBefore(commands, Stage::Copy, counters.gpu, 0x1234, CompareOp::GreaterEqual);

    copyMemory(commands, readbackAddress(0), storage.gpu, 64 * sizeof(uint32_t));
    submitAndWait(commands);

    const auto* published = static_cast<const uint64_t*>(counters.cpu);
    REQUIRE(published[0] == 0x1234);
    REQUIRE(published[1] == 9);
    const auto* values = reinterpret_cast<const uint32_t*>(readbackBytes(0));
    REQUIRE(values[63] == 64);

    deallocate(device(), counters);
    deallocate(device(), storage);
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "the frame ring recycles three slots under load", "[noapi][smoke]") {
    // One target per slot: three frames run in flight, so a single shared target would let a
    // later frame overwrite the one an earlier frame's readback is still copying.
    std::array<Texture*, FrameRing::kFramesInFlight> targets{};
    for (uint32_t slot = 0; slot < FrameRing::kFramesInFlight; ++slot) {
        targets[slot] = makeColorTarget("lmx.noapi.test.ringTarget");
    }
    Pipeline* pipeline =
        makeGraphics("lmxTriangleVs", "lmxSolidFs", Format::Undefined, "lmx.noapi.test.ringSolid");

    Result<FrameRing> created =
        FrameRing::create(device(), {.bytesPerFrame = 4096, .label = "lmx.noapi.test.ring"});
    REQUIRE(created.has_value());
    FrameRing ring = std::move(*created);
    commitResidency(residency());

    constexpr uint32_t kFrames = 6;
    std::array<uint32_t, kFrames> slots{};
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        slots[frame] = ring.beginFrame();
        REQUIRE(ring.frameIndex() == frame + 1);

        CommandBuffer* commands =
            beginCommands(queue(), &ring.rootAllocator(), "lmx.noapi.test.ringFrame");
        const Suballocation vertices = ring.rootAllocator().allocate(sizeof(kTriangle), 16);
        std::memcpy(vertices.cpu, kTriangle.data(), sizeof(kTriangle));

        const float level = static_cast<float>(frame + 1) / 10.0f;
        const VertexRoot vertexRoot{.tint = {level, level, level, 1.0f}, .vertices = vertices.gpu};
        const SolidPixelRoot pixelRoot{.scale = {1.0f, 1.0f, 1.0f, 1.0f}};
        const GpuAddress vertexAddress = pushRoot(commands, vertexRoot);
        const GpuAddress pixelAddress = pushRoot(commands, pixelRoot);

        const std::array<ColorAttachment, 1> colorTargets{
            ColorAttachment{.texture = targets[slots[frame]],
                            .mipLevel = 0,
                            .arrayLayer = 0,
                            .load = LoadAction::Clear,
                            .store = StoreAction::Store,
                            .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}};
        beginRenderPass(
            commands,
            {.colorTargets = colorTargets, .depth = nullptr, .label = "lmx.noapi.test.ringPass"});
        setPipeline(commands, pipeline);
        draw(commands, vertexAddress, pixelAddress, 3);
        endRenderPass(commands);

        readTexture(commands, targets[slots[frame]],
                    readbackAddress(uint64_t{frame} * kTargetRowBytes), 1);
        endCommands(commands);
        const std::array<CommandBuffer*, 1> list{commands};
        ring.endFrame(queue(), list);
    }

    // Three slots serve six frames, so every slot is reused exactly once.
    REQUIRE(slots[0] == slots[3]);
    REQUIRE(slots[1] == slots[4]);
    REQUIRE(slots[2] == slots[5]);

    bool retired = false;
    for (uint32_t attempt = 0; attempt < 1000 && !retired; ++attempt) {
        retired = ring.isSlotRetired(0) && ring.isSlotRetired(1) && ring.isSlotRetired(2);
        if (!retired) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    REQUIRE(retired);

    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        const uint8_t* pixel = readbackBytes(uint64_t{frame} * kTargetRowBytes);
        const auto expected = static_cast<uint8_t>((frame + 1) * 255 / 10);
        REQUIRE(pixel[0] >= expected - 1);
        REQUIRE(pixel[0] <= expected + 1);
    }
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "a semaphore reports the GPU's progress", "[noapi][smoke]") {
    Pipeline* pipeline = makeCompute("lmxFillBufferKernel", "lmx.noapi.test.paceFill");
    const Allocation storage =
        allocateOrFail(1024, 256, MemoryKind::Shared, "lmx.noapi.test.paceStorage");
    Result<Semaphore*> semaphore = createSemaphore(device(), 10, "lmx.noapi.test.pacing");
    REQUIRE(semaphore.has_value());
    commitResidency(residency());

    REQUIRE(semaphoreValue(*semaphore) == 10);

    CommandBuffer* commands = begin("lmx.noapi.test.pacing");
    const FillRoot root{.out = storage.gpu, .count = 64, .base = 0};
    const GpuAddress rootAddress = pushRoot(commands, root);
    setPipeline(commands, pipeline);
    dispatch(commands, rootAddress, 1, 1, 1);
    endCommands(commands);

    const std::array<CommandBuffer*, 1> list{commands};
    submit(queue(), list, *semaphore, 11);
    waitSemaphore(*semaphore, 11);
    REQUIRE(semaphoreValue(*semaphore) == 11);

    destroySemaphore(device(), *semaphore);
    deallocate(device(), storage);
}

//======================================================================================================================
TEST_CASE_METHOD(Harness, "a GPU capture records an address-first frame", "[noapi][capture]") {
    NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
    MTL::CaptureManager* manager = MTL::CaptureManager::sharedCaptureManager();
    if (manager == nullptr ||
        !manager->supportsDestination(MTL::CaptureDestinationGPUTraceDocument)) {
        // Metal reads MTL_CAPTURE_ENABLED at launch, so an unenabled process skips rather than
        // fails: this case is a capture-quality probe, not a correctness gate.
        SUCCEED("GPU capture is not enabled for this process");
        return;
    }

    const char* requested = std::getenv("LMX_NOAPI_CAPTURE_PATH");
    const std::string path = requested != nullptr ? requested : "/tmp/lmx-noapi-smoke.gputrace";
    std::error_code removeError;
    std::filesystem::remove_all(path, removeError);

    auto url = NS::TransferPtr(NS::URL::alloc()->initFileURLWithPath(
        NS::String::string(path.c_str(), NS::UTF8StringEncoding)));
    auto descriptor = NS::TransferPtr(MTL::CaptureDescriptor::alloc()->init());
    // The prototype hands out no Metal handles, so the capture object is the process's default
    // device -- the same object `createDevice` opened.
    NS::SharedPtr<MTL::Device> metalDevice = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    descriptor->setCaptureObject(metalDevice.get());
    descriptor->setDestination(MTL::CaptureDestinationGPUTraceDocument);
    descriptor->setOutputURL(url.get());

    NS::Error* error = nullptr;
    const bool started = manager->startCapture(descriptor.get(), &error);
    REQUIRE(started);

    Texture* target = makeColorTarget("lmx.noapi.capture.target");
    Pipeline* pipeline =
        makeGraphics("lmxTriangleVs", "lmxSolidFs", Format::Undefined, "lmx.noapi.capture.solid");

    CommandBuffer* commands = begin("lmx.noapi.capture.frame");
    pushDebugGroup(commands, "capture-probe");
    const Suballocation vertices = stage(kTriangle);
    const VertexRoot vertexRoot{.tint = {0.2f, 0.4f, 0.6f, 1.0f}, .vertices = vertices.gpu};
    const SolidPixelRoot pixelRoot{.scale = {1.0f, 1.0f, 1.0f, 1.0f}};
    const GpuAddress vertexAddress = pushRoot(commands, vertexRoot);
    const GpuAddress pixelAddress = pushRoot(commands, pixelRoot);

    const std::array<ColorAttachment, 1> colorTargets{
        ColorAttachment{.texture = target,
                        .mipLevel = 0,
                        .arrayLayer = 0,
                        .load = LoadAction::Clear,
                        .store = StoreAction::Store,
                        .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}}};
    beginRenderPass(
        commands,
        {.colorTargets = colorTargets, .depth = nullptr, .label = "lmx.noapi.capture.pass"});
    setPipeline(commands, pipeline);
    draw(commands, vertexAddress, pixelAddress, 3);
    endRenderPass(commands);
    popDebugGroup(commands);
    readTexture(commands, target, readbackAddress(0), 1);
    submitAndWait(commands);

    manager->stopCapture();
    REQUIRE(std::filesystem::exists(path));

    // The addresses this frame actually bound, reported so that the recorded trace can be searched
    // for them: whether a capture preserves them is the open `capturePersistsAddresses` question.
    WARN("capture probe wrote " << path << "; vertex root address 0x" << std::hex << vertexAddress
                                << ", pixel root address 0x" << pixelAddress
                                << ", bindless table address 0x" << bindlessTableAddress(table())
                                << ", readback address 0x" << readbackAddress(0) << std::dec);
}

} // namespace lmx::noapi::test
