#include "Support/GpuTestSupport.h"

#include "Engine/Scene/SceneTables.h"

#include <cstring>

//======================================================================================================================
TEST_CASE("CPU buffer uploads preserve surrounding bytes and reach GPU reads",
          "[gpu][scene-tables]") {
    using namespace rojoRHI;
    auto gpu = createTestGpu();
    auto* device = &gpu->device();
    REQUIRE(gpu);
    std::array<uint8_t, 64> initial{};
    initial.fill(0xA5);
    auto upload = lmx::engine::GpuBuffer::create(
        *device,
        {.size = initial.size(), .memory = Memory::Upload, .label = "lmx.test.upload.source"},
        std::as_bytes(std::span(initial)));
    INFO(errorOf(upload));
    REQUIRE(upload);
    auto destination = lmx::engine::GpuBuffer::create(
        *device,
        {.size = initial.size(), .memory = Memory::Gpu, .label = "lmx.test.upload.destination"},
        {});
    INFO(errorOf(destination));
    REQUIRE(destination);
    const std::array<uint8_t, 7> patch{1, 19, 33, 47, 101, 203, 255};
    for (const uint64_t offset : {uint64_t{0}, uint64_t{23}, uint64_t{57}}) {
        std::memcpy(upload->cpuPointer() + offset, patch.data(), patch.size());
        std::memcpy(initial.data() + offset, patch.data(), patch.size());
    }
    std::array<uint8_t, 64> cpu{};
    {
        const auto bytes = lmx::render::readbackNow(
            *gpu, *upload,
            lmx::render::SyncPoint{Stage::None, Access::None, rojoRHI::Layout::Undefined},
            {0, cpu.size()});
        INFO(errorOf(bytes));
        REQUIRE(bytes.has_value());
        std::memcpy(cpu.data(), bytes->data(), cpu.size());
    }
    REQUIRE(cpu == initial);
    auto gpuFrame1 = gpu->beginFrame();
    auto& commands = gpuFrame1.commands;
    commands.beginScope("lmx.test.upload.copy");
    commands.copyBuffer(upload->rhi(), 0, destination->rhi(), 0, initial.size());
    commands.endScope();
    gpu->submitAndEndFrame(gpuFrame1);
    gpu->waitIdle();
    {
        const auto bytes = lmx::render::readbackNow(
            *gpu, *destination,
            lmx::render::SyncPoint{Stage::Copy, Access::CopyWrite, rojoRHI::Layout::Undefined},
            {0, cpu.size()});
        INFO(errorOf(bytes));
        REQUIRE(bytes.has_value());
        std::memcpy(cpu.data(), bytes->data(), cpu.size());
    }
    REQUIRE(cpu == initial);

    std::memcpy(upload->cpuPointer(), patch.data(), patch.size());
    {
        const auto bytes = lmx::render::readbackNow(
            *gpu, *upload,
            lmx::render::SyncPoint{Stage::Copy, Access::CopyRead, rojoRHI::Layout::Undefined},
            {0, cpu.size()});
        INFO(errorOf(bytes));
        REQUIRE(bytes.has_value());
        std::memcpy(cpu.data(), bytes->data(), cpu.size());
    }
    REQUIRE(cpu == initial);
}

//======================================================================================================================
TEST_CASE("scene table structured-buffer ABI preserves every field and row stride",
          "[gpu][scene-tables]") {
    using namespace lmx;
    auto gpu = createTestGpu();
    auto* device = &gpu->device();
    REQUIRE(gpu);
    auto library = gpu->loadShaderLibrary("SceneTableAbi");
    INFO(errorOf(library));
    REQUIRE(library);
    auto pipeline = engine::adoptGpu(*device, device->createPipeline(rojoRHI::ComputePipelineDesc{
                                                  .compute = {library->get(), "computeMain"},
                                                  .label = "lmx.test.sceneTableAbi.pipeline"}));
    INFO(errorOf(pipeline));
    REQUIRE(pipeline);
    std::array<engine::InstanceRow, 2> instances{};
    std::array<engine::MaterialRow, 2> materials{};
    std::array<engine::MeshRow, 2> meshes{};
    std::array<engine::LightRow, 2> lights{};
    for (uint32_t row = 0; row < 2; ++row) {
        auto& instance = instances[row];
        auto& material = materials[row];
        for (int column = 0; column < 4; ++column) {
            for (int component = 0; component < 4; ++component) {
                const float value = static_cast<float>(row * 128 + column * 4 + component);
                instance.model[column][component] = value + 0.25f;
                instance.previousModel[column][component] = -value - 1.5f;
                instance.normalMatrix[column][component] = value + 32.75f;
                material.uvTransform[column][component] = -value - 64.125f;
            }
        }
        instance.meshRow = 0xA1234000u + row;
        instance.materialRow = 0xB2345000u + row;
        instance.flags = 0xC3456000u + row;
        instance.emissiveScale = 1.125f + static_cast<float>(row);
        instance.worldBoundsMin = glm::vec3(-12.5f, -6.25f, -3.125f) - float(row);
        instance.worldBoundsMax = glm::vec3(7.25f, 9.5f, 13.75f) + float(row);
        instance.boundsPadding0 = 0x12345678u + row;
        instance.boundsPadding1 = 0x23456789u + row;
        material.albedo = glm::vec4(0.125f, 0.375f, 0.625f, 0.875f) + float(row);
        material.emissive = glm::vec3(-0.25f, 0.5f, 1.75f) + float(row);
        material.roughness = 0.3125f + float(row);
        material.metallic = 0.5625f + float(row);
        material.occlusionStrength = 0.8125f + float(row);
        material.alphaCutoff = 0.4375f + float(row);
        material.flags = 0xD4567000u + row;
        meshes[row] = {.firstIndex = 17 + row,
                       .indexCount = 31 + row,
                       .firstVertex = 47 + row,
                       .vertexCount = 61 + row,
                       .boundsMin = glm::vec3(-1.25f, -2.5f, -3.75f) - float(row),
                       .boundsPadding0 = 0x34567890u + row,
                       .boundsMax = glm::vec3(4.125f, 5.25f, 6.5f) + float(row),
                       .boundsPadding1 = 0x45678901u + row};
        lights[row] = {.position = glm::vec3(-2.5f, 7.75f, 0.125f) + float(row),
                       .range = 11.5f + float(row),
                       .strength = glm::vec3(3.25f, 2.125f, 1.0625f) + float(row),
                       .spotScale = 5.75f + float(row),
                       .direction = glm::vec3(0.0f, -1.0f, 0.0f),
                       .spotOffset = -4.375f - float(row),
                       .boundCentre = glm::vec3(-2.5f, 1.25f, 0.125f) + float(row),
                       .boundRadius = 8.875f + float(row)};
    }
    auto instanceBuffer = engine::GpuBuffer::create(
        *device, {.size = sizeof(instances), .label = "lmx.test.sceneTableAbi.instances"},
        std::as_bytes(std::span(instances)));
    auto materialBuffer = engine::GpuBuffer::create(
        *device, {.size = sizeof(materials), .label = "lmx.test.sceneTableAbi.materials"},
        std::as_bytes(std::span(materials)));
    auto meshBuffer = engine::GpuBuffer::create(
        *device, {.size = sizeof(meshes), .label = "lmx.test.sceneTableAbi.meshes"},
        std::as_bytes(std::span(meshes)));
    auto lightBuffer = engine::GpuBuffer::create(
        *device, {.size = sizeof(lights), .label = "lmx.test.sceneTableAbi.lights"},
        std::as_bytes(std::span(lights)));
    REQUIRE(instanceBuffer);
    REQUIRE(materialBuffer);
    REQUIRE(meshBuffer);
    REQUIRE(lightBuffer);
    // The kernel packs one row of every table into this many words, so a wrong structured-buffer
    // stride shifts the second row's words and fails on a specific field rather than in bulk.
    constexpr size_t kRowBytes = sizeof(engine::InstanceRow) + sizeof(engine::MaterialRow) +
                                 sizeof(engine::MeshRow) + sizeof(engine::LightRow);
    std::array<uint32_t, 2 * kRowBytes / sizeof(uint32_t)> expected{};
    for (size_t row = 0; row < 2; ++row) {
        auto* bytes = reinterpret_cast<std::byte*>(expected.data()) + row * kRowBytes;
        std::memcpy(bytes, &instances[row], sizeof(engine::InstanceRow));
        bytes += sizeof(engine::InstanceRow);
        std::memcpy(bytes, &materials[row], sizeof(engine::MaterialRow));
        bytes += sizeof(engine::MaterialRow);
        std::memcpy(bytes, &meshes[row], sizeof(engine::MeshRow));
        bytes += sizeof(engine::MeshRow);
        std::memcpy(bytes, &lights[row], sizeof(engine::LightRow));
    }
    auto output = lmx::engine::GpuBuffer::create(*device,
                                                 {.size = sizeof(expected),
                                                  .memory = rojoRHI::Memory::Gpu,
                                                  .usage = rojoRHI::BufferUsage::ShaderWrite,
                                                  .label = "lmx.test.sceneTableAbi.output"},
                                                 {});
    REQUIRE(output);
    auto gpuFrame2 = gpu->beginFrame();
    auto& commands = gpuFrame2.commands;
    commands.beginScope("lmx.test.sceneTableAbi.read");
    commands.setPipeline(pipeline->get());
    commands.pushRoot(test::SceneTableAbiRoot{.output = output->address(),
                                              .instances = instanceBuffer->address(),
                                              .materials = materialBuffer->address(),
                                              .meshes = meshBuffer->address(),
                                              .lights = lightBuffer->address()});
#ifndef NDEBUG
    const std::array<rojoRHI::ResourceAccess, 5> accesses{
        {{.buffer = output->rhi(),
          .offset = 0,
          .size = sizeof(expected),
          .stage = rojoRHI::Stage::ComputeShader,
          .access = rojoRHI::Access::ShaderWrite},
         {.buffer = instanceBuffer->rhi(),
          .offset = 0,
          .size = sizeof(instances),
          .stage = rojoRHI::Stage::ComputeShader,
          .access = rojoRHI::Access::ShaderRead},
         {.buffer = materialBuffer->rhi(),
          .offset = 0,
          .size = sizeof(materials),
          .stage = rojoRHI::Stage::ComputeShader,
          .access = rojoRHI::Access::ShaderRead},
         {.buffer = meshBuffer->rhi(),
          .offset = 0,
          .size = sizeof(meshes),
          .stage = rojoRHI::Stage::ComputeShader,
          .access = rojoRHI::Access::ShaderRead},
         {.buffer = lightBuffer->rhi(),
          .offset = 0,
          .size = sizeof(lights),
          .stage = rojoRHI::Stage::ComputeShader,
          .access = rojoRHI::Access::ShaderRead}}};
    commands.declareAccess(accesses);
#endif
    commands.dispatch(2, 1, 1);
    commands.endScope();
    gpu->submitAndEndFrame(gpuFrame2);
    gpu->waitIdle();
    decltype(expected) actual{};
    {
        const auto bytes = lmx::render::readbackNow(
            *gpu, *output,
            lmx::render::SyncPoint{rojoRHI::Stage::ComputeShader, rojoRHI::Access::ShaderWrite,
                                   rojoRHI::Layout::Undefined},
            {0, sizeof(actual)});
        INFO(errorOf(bytes));
        REQUIRE(bytes.has_value());
        std::memcpy(actual.data(), bytes->data(), sizeof(actual));
    }
    for (size_t index = 0; index < expected.size(); ++index) {
        INFO("ABI word " << index);
        REQUIRE(actual[index] == expected[index]);
    }
}
