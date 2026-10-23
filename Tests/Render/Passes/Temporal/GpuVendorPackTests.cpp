#include "Support/GpuTestSupport.h"

#include <cstring>

#include "Render/Passes/Temporal/VendorTemporalScaler.h"

#include <array>

//======================================================================================================================
TEST_CASE("vendor packing translates invalid motion and working exposure without a scaler",
          "[gpu][temporal][vendor][pack]") {
    using namespace rojoRHI;
    using namespace lmx::render;
    auto gpu = createTestGpu();
    VendorTemporalScaler adapter(*gpu);
    REQUIRE(adapter.preparePacking());
    const std::array<uint16_t, 4> motionBits{0x7c00, 0x7c00, 0x3400, 0xb800};
    const std::array<uint8_t, 2> reactiveBytes{64, 128};
    const TextureMip motionMip{motionBits.data(), 8};
    const TextureMip reactiveMip{reactiveBytes.data(), 2};
    auto motion = lmx::engine::GpuTexture::create(gpu->device(),
                                                  {.width = 2,
                                                   .height = 1,
                                                   .format = Format::RG16Float,
                                                   .sampled = true,
                                                   .label = "lmx.test.vendor.pack.motion"},
                                                  {&motionMip, 1});
    auto reactive = lmx::engine::GpuTexture::create(gpu->device(),
                                                    {.width = 2,
                                                     .height = 1,
                                                     .format = Format::R8Unorm,
                                                     .sampled = true,
                                                     .label = "lmx.test.vendor.pack.reactive"},
                                                    {&reactiveMip, 1});
    const float exposurePair[] = {4.0f, 2.0f};
    auto exposure = lmx::engine::GpuBuffer::create(gpu->device(),
                                                   {.size = sizeof(exposurePair),
                                                    .storageRead = true,
                                                    .label = "lmx.test.vendor.pack.exposure"},
                                                   exposurePair);
    auto readback = lmx::engine::GpuBuffer::create(
        gpu->device(), {.size = 12, .cpuReadback = true, .label = "lmx.test.vendor.pack.readback"},
        nullptr);
    REQUIRE(motion);
    REQUIRE(reactive);
    REQUIRE(exposure);
    REQUIRE(readback);
    auto commandsFrame = gpu->beginFrame();
    auto& commands = commandsFrame.commands;
    TransientPool pool(gpu->device());
    pool.beginFrame();
    TextureStateTable states;
    states.set(motion->rhi(), lmx::render::TextureUse::ShaderRead);
    states.set(reactive->rhi(), lmx::render::TextureUse::ShaderRead);
    RenderGraph graph(pool);
    graph.bindTextureStates(states);
    TemporalInputs inputs;
    inputs.motion = graph.importPersistentTexture(motion->rhi(), Format::RG16Float, "motion");
    inputs.reactive = graph.importPersistentTexture(reactive->rhi(), Format::R8Unorm, "reactive");
    inputs.exposure =
        graph.importBuffer(exposure->rhi(), "exposure", lmx::render::BufferUse::StorageRead);
    inputs.extents = {2, 1, 2, 1};
    const auto packed = adapter.declarePack(graph, commands, inputs);
    const auto output =
        graph.importBuffer(readback->rhi(), "readback", lmx::render::BufferUse::CopyDestination);
    CopyPassDesc desc;
    desc.textureSources = {packed.motion, packed.reactive, packed.exposure};
    desc.bufferDestinations = {output};
    graph.addCopyPass(
        "lmx.test.vendor.pack.readback", std::move(desc), [&](const PassResources& resources) {
            const auto packedMotion = resources.texture(packed.motion);
            const auto packedReactive = resources.texture(packed.reactive);
            const auto packedExposure = resources.texture(packed.exposure);
            REQUIRE(packedMotion);
            REQUIRE(packedReactive);
            REQUIRE(packedExposure);
            commands.copyTextureToBuffer(**packedMotion, {.width = 2, .height = 1}, readback->rhi(),
                                         {.offset = 0, .bytesPerRow = 8});
            commands.copyTextureToBuffer(**packedReactive, {.width = 2, .height = 1},
                                         readback->rhi(), {.offset = 8, .bytesPerRow = 2});
            commands.copyTextureToBuffer(**packedExposure, {.width = 1, .height = 1},
                                         readback->rhi(), {.offset = 10, .bytesPerRow = 2});
        });
    graph.exportBuffer(nextVersion(output));
    graph.execute(commands, gpu->frameNumber());
    gpu->submitAndEndFrame(commandsFrame);
    gpu->waitIdle();
    std::array<uint16_t, 6> result{};
    {
        const auto readbackBytes = lmx::render::readbackNow(
            *gpu, *readback, lmx::render::BufferUse::CopyDestination, {0, sizeof(result)});
        std::memcpy(result.data(), readbackBytes.data(), sizeof(result));
    }
    REQUIRE(result[0] == 0);
    REQUIRE(result[1] == 0);
    REQUIRE(result[2] == motionBits[2]);
    REQUIRE(result[3] == motionBits[3]);
    const auto* bytes = reinterpret_cast<const uint8_t*>(result.data());
    REQUIRE(bytes[8] == 255);
    REQUIRE(bytes[9] == 128);
    REQUIRE(result[5] == 0x3400); // Reciprocal of applied exposure 4, represented as half 0.25.
    REQUIRE(adapter.generation() == 0);
}
