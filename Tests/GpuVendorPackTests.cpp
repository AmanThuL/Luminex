#include "GpuTestSupport.h"

#include "Render/VendorTemporalScaler.h"

#include <array>

//======================================================================================================================
TEST_CASE("vendor packing translates invalid motion and working exposure without a scaler",
          "[gpu][temporal][vendor][pack]") {
    using namespace lmx::rhi;
    using namespace lmx::render;
    auto device = createDevice();
    REQUIRE(device);
    VendorTemporalScaler adapter(**device);
    REQUIRE(adapter.preparePacking());
    const std::array<uint16_t, 4> motionBits{0x7c00, 0x7c00, 0x3400, 0xb800};
    const std::array<uint8_t, 2> reactiveBytes{64, 128};
    const TextureMip motionMip{motionBits.data(), 8};
    const TextureMip reactiveMip{reactiveBytes.data(), 2};
    auto motion = (*device)->createTexture({.width = 2,
                                            .height = 1,
                                            .format = Format::RG16Float,
                                            .sampled = true,
                                            .label = "lmx.test.vendor.pack.motion"},
                                           {&motionMip, 1});
    auto reactive = (*device)->createTexture({.width = 2,
                                              .height = 1,
                                              .format = Format::R8Unorm,
                                              .sampled = true,
                                              .label = "lmx.test.vendor.pack.reactive"},
                                             {&reactiveMip, 1});
    const float exposurePair[] = {4.0f, 2.0f};
    auto exposure = (*device)->createBuffer({.size = sizeof(exposurePair),
                                             .storageRead = true,
                                             .label = "lmx.test.vendor.pack.exposure"},
                                            exposurePair);
    auto readback = (*device)->createBuffer(
        {.size = 12, .cpuReadback = true, .label = "lmx.test.vendor.pack.readback"}, nullptr);
    REQUIRE(motion);
    REQUIRE(reactive);
    REQUIRE(exposure);
    REQUIRE(readback);
    auto& commands = (*device)->beginFrame();
    TransientPool pool(**device);
    pool.beginFrame();
    RenderGraph graph(pool);
    TemporalInputs inputs;
    inputs.motion =
        graph.importTexture(**motion, Format::RG16Float, "motion", TextureUse::ShaderRead);
    inputs.reactive =
        graph.importTexture(**reactive, Format::R8Unorm, "reactive", TextureUse::ShaderRead);
    inputs.exposure = graph.importBuffer(**exposure, "exposure", BufferUse::StorageRead);
    inputs.extents = {2, 1, 2, 1};
    const auto packed = adapter.declarePack(graph, commands, inputs);
    const auto output = graph.importBuffer(**readback, "readback", BufferUse::CopyDestination);
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
            commands.copyTextureToBuffer(**packedMotion, {.width = 2, .height = 1}, **readback,
                                         {.offset = 0, .bytesPerRow = 8});
            commands.copyTextureToBuffer(**packedReactive, {.width = 2, .height = 1}, **readback,
                                         {.offset = 8, .bytesPerRow = 2});
            commands.copyTextureToBuffer(**packedExposure, {.width = 1, .height = 1}, **readback,
                                         {.offset = 10, .bytesPerRow = 2});
        });
    graph.exportBuffer(nextVersion(output));
    graph.execute(commands, (*device)->frameNumber());
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
    std::array<uint16_t, 6> result{};
    (*readback)->readback(result.data(), sizeof(result));
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
