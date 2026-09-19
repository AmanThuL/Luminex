#include "GpuTestSupport.h"

#include "Render/SceneTables.h"

#include <cstring>

//======================================================================================================================
TEST_CASE("CPU buffer uploads preserve surrounding bytes and reach GPU reads",
          "[gpu][scene-tables]") {
    using namespace rojoRHI;
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    std::array<uint8_t, 64> initial{};
    initial.fill(0xA5);
    auto upload = (*device)->createBuffer({.size = initial.size(),
                                           .cpuReadback = true,
                                           .cpuWrite = true,
                                           .label = "lmx.test.upload.source"},
                                          initial.data());
    INFO(errorOf(upload));
    REQUIRE(upload);
    auto destination = (*device)->createBuffer(
        {.size = initial.size(), .cpuReadback = true, .label = "lmx.test.upload.destination"},
        nullptr);
    INFO(errorOf(destination));
    REQUIRE(destination);
    const std::array<uint8_t, 7> patch{1, 19, 33, 47, 101, 203, 255};
    for (const uint64_t offset : {uint64_t{0}, uint64_t{23}, uint64_t{57}}) {
        (*upload)->write(offset, patch.data(), patch.size());
        std::memcpy(initial.data() + offset, patch.data(), patch.size());
    }
    std::array<uint8_t, 64> cpu{};
    (*upload)->readback(cpu.data(), cpu.size());
    REQUIRE(cpu == initial);
    auto& commands = (*device)->beginFrame();
    commands.beginCopyPass("lmx.test.upload.copy");
    commands.copyBuffer(**upload, 0, **destination, 0, initial.size());
    commands.endCopyPass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
    (*destination)->readback(cpu.data(), cpu.size());
    REQUIRE(cpu == initial);

    (*upload)->write(0, patch.data(), patch.size());
    (*upload)->readback(cpu.data(), cpu.size());
    REQUIRE(cpu == initial);
}

//======================================================================================================================
TEST_CASE("device-private placed buffers reject host upload permission", "[gpu][scene-tables]") {
    using namespace rojoRHI;
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    const BufferDesc desc{.size = 256, .cpuWrite = true, .label = "lmx.test.upload.placed"};
    const auto footprint = (*device)->bufferSizeAlign(desc);
    auto heap = (*device)->createHeap({.size = footprint.size, .label = "lmx.test.upload.heap"});
    INFO(errorOf(heap));
    REQUIRE(heap);
    auto buffer = (*device)->createPlacedBuffer(**heap, 0, desc);
    REQUIRE_FALSE(buffer);
    REQUIRE(buffer.error().code == ErrorCode::InvalidDesc);
    REQUIRE(buffer.error().message.contains("cpuWrite"));
}

//======================================================================================================================
TEST_CASE("scene table structured-buffer ABI preserves every field and row stride",
          "[gpu][scene-tables]") {
    using namespace lmx;
    auto device = rojoRHI::createDevice();
    INFO(errorOf(device));
    REQUIRE(device);
    auto library = (*device)->loadShaderLibrary("Shaders/SceneTableAbi");
    INFO(errorOf(library));
    REQUIRE(library);
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeMain",
                                                      .threadsPerThreadgroup = {1, 1, 1},
                                                      .label = "lmx.test.sceneTableAbi.pipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline);
    std::array<render::InstanceRow, 2> instances{};
    std::array<render::MaterialRow, 2> materials{};
    std::array<render::MeshRow, 2> meshes{};
    std::array<render::LightRow, 2> lights{};
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
    auto instanceBuffer = (*device)->createBuffer(
        {.size = sizeof(instances), .label = "lmx.test.sceneTableAbi.instances"}, instances.data());
    auto materialBuffer = (*device)->createBuffer(
        {.size = sizeof(materials), .label = "lmx.test.sceneTableAbi.materials"}, materials.data());
    auto meshBuffer = (*device)->createBuffer(
        {.size = sizeof(meshes), .label = "lmx.test.sceneTableAbi.meshes"}, meshes.data());
    auto lightBuffer = (*device)->createBuffer(
        {.size = sizeof(lights), .label = "lmx.test.sceneTableAbi.lights"}, lights.data());
    REQUIRE(instanceBuffer);
    REQUIRE(materialBuffer);
    REQUIRE(meshBuffer);
    REQUIRE(lightBuffer);
    // The kernel packs one row of every table into this many words, so a wrong structured-buffer
    // stride shifts the second row's words and fails on a specific field rather than in bulk.
    constexpr size_t kRowBytes = sizeof(render::InstanceRow) + sizeof(render::MaterialRow) +
                                 sizeof(render::MeshRow) + sizeof(render::LightRow);
    std::array<uint32_t, 2 * kRowBytes / sizeof(uint32_t)> expected{};
    for (size_t row = 0; row < 2; ++row) {
        auto* bytes = reinterpret_cast<std::byte*>(expected.data()) + row * kRowBytes;
        std::memcpy(bytes, &instances[row], sizeof(render::InstanceRow));
        bytes += sizeof(render::InstanceRow);
        std::memcpy(bytes, &materials[row], sizeof(render::MaterialRow));
        bytes += sizeof(render::MaterialRow);
        std::memcpy(bytes, &meshes[row], sizeof(render::MeshRow));
        bytes += sizeof(render::MeshRow);
        std::memcpy(bytes, &lights[row], sizeof(render::LightRow));
    }
    auto output = (*device)->createBuffer({.size = sizeof(expected),
                                           .storageWrite = true,
                                           .cpuReadback = true,
                                           .label = "lmx.test.sceneTableAbi.output"},
                                          nullptr);
    REQUIRE(output);
    auto& commands = (*device)->beginFrame();
    commands.beginComputePass("lmx.test.sceneTableAbi.read");
    commands.bindComputePipeline(**pipeline);
    commands.bindStorageBuffer(0, **output, rojoRHI::StorageAccess::Write);
    commands.bindBuffer(render::kSceneInstancesSlot, **instanceBuffer);
    commands.bindBuffer(render::kSceneMaterialsSlot, **materialBuffer);
    commands.bindBuffer(render::kSceneMeshesSlot, **meshBuffer);
    commands.bindBuffer(render::kSceneLightsSlot, **lightBuffer);
    commands.dispatch(2, 1, 1);
    commands.endComputePass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
    decltype(expected) actual{};
    (*output)->readback(actual.data(), sizeof(actual));
    for (size_t index = 0; index < expected.size(); ++index) {
        INFO("ABI word " << index);
        REQUIRE(actual[index] == expected[index]);
    }
}
