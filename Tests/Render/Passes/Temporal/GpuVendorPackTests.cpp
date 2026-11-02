#include "Support/GpuTestSupport.h"

#include "Render/Graph/TransientPool.h"
#include "Render/Passes/Temporal/VendorTemporalScaler.h"

#include <array>
#include <cstring>

//======================================================================================================================
TEST_CASE("vendor packing translates invalid motion and working exposure without a scaler",
          "[gpu][temporal][vendor][pack]") {
    using namespace rojoRHI;
    using namespace lmx::render;
    auto gpu = createTestGpu();
    VendorTemporalScaler adapter(*gpu);
    const auto packing = adapter.preparePacking();
    INFO(errorOf(packing));
    REQUIRE(packing);
    const std::array<uint16_t, 4> motionBits{0x7c00, 0x7c00, 0x3400, 0xb800};
    const std::array<uint8_t, 2> reactiveBytes{64, 128};
    const TextureData motionMip{.bytes = std::as_bytes(std::span{motionBits}), .bytesPerRow = 8};
    const TextureData reactiveMip{.bytes = std::as_bytes(std::span{reactiveBytes}),
                                  .bytesPerRow = 2};
    auto motion = lmx::engine::GpuTexture::create(gpu->device(),
                                                  {.format = Format::RG16Float,
                                                   .extent = {.width = 2, .height = 1, .depth = 1},
                                                   .usage = TextureUsage::Sampled,
                                                   .label = "lmx.test.vendor.pack.motion"},
                                                  std::span{&motionMip, 1});
    auto reactive =
        lmx::engine::GpuTexture::create(gpu->device(),
                                        {.format = Format::R8Unorm,
                                         .extent = {.width = 2, .height = 1, .depth = 1},
                                         .usage = TextureUsage::Sampled,
                                         .label = "lmx.test.vendor.pack.reactive"},
                                        std::span{&reactiveMip, 1});
    const std::array<float, 2> exposurePair{4.0f, 2.0f};
    auto exposure = lmx::engine::GpuBuffer::create(
        gpu->device(), {.size = sizeof(exposurePair), .label = "lmx.test.vendor.pack.exposure"},
        std::as_bytes(std::span{exposurePair}));
    // Each copy lands at its own 512-byte offset with a 256-byte row pitch, the copy layout's
    // alignment.
    constexpr uint64_t kMotionOffset = 0;
    constexpr uint64_t kReactiveOffset = 512;
    constexpr uint64_t kExposureOffset = 1024;
    auto readback = lmx::engine::GpuBuffer::create(
        gpu->device(), {.size = kExposureOffset + 256, .label = "lmx.test.vendor.pack.readback"});
    INFO(errorOf(motion));
    REQUIRE(motion);
    INFO(errorOf(reactive));
    REQUIRE(reactive);
    INFO(errorOf(exposure));
    REQUIRE(exposure);
    INFO(errorOf(readback));
    REQUIRE(readback);

    TransientPool pool(*gpu);
    auto frame = gpu->beginFrame();
    auto& commands = frame.commands;
    pool.beginFrame();
    TextureStateTable states;
    RenderGraph graph(pool);
    graph.bindTextureStates(states);
    TemporalInputs inputs;
    inputs.motion = graph.importPersistentTexture(*motion, "motion");
    inputs.reactive = graph.importPersistentTexture(*reactive, "reactive");
    inputs.exposure = graph.importBuffer(*exposure, "exposure");
    inputs.extents = {.renderWidth = 2, .renderHeight = 1, .outputWidth = 2, .outputHeight = 1};
    const auto packed = adapter.declarePack(graph, commands, inputs);
    const auto output = graph.importBuffer(*readback, "readback");
    CopyPassDesc desc;
    desc.textureSources = {packed.motion, packed.reactive, packed.exposure};
    desc.bufferDestinations = {output};
    graph.addCopyPass(
        "lmx.test.vendor.pack.readback", std::move(desc), [&](const PassResources& resources) {
            const auto copy = [&](GraphTexture source, uint32_t width, uint64_t offset) {
                commands.copyTextureToBuffer(
                    resources.texture(source).texture,
                    {.offset = {}, .extent = {.width = width, .height = 1, .depth = 1}},
                    resources.buffer(output), {.offset = offset, .rowPitch = 256});
            };
            copy(packed.motion, 2, kMotionOffset);
            copy(packed.reactive, 2, kReactiveOffset);
            copy(packed.exposure, 1, kExposureOffset);
        });
    graph.readbackBuffer(nextVersion(output));
    const auto compiled = graph.compile();
    if (!compiled) {
        gpu->submitAndEndFrame(frame);
    }
    INFO((compiled ? std::string{} : compiled.error().message));
    REQUIRE(compiled.has_value());
    graph.execute(commands, gpu->frameNumber());
    gpu->submitAndEndFrame(frame);
    gpu->waitIdle();

    const auto bytes = lmx::render::readbackNow(
        *gpu, *readback,
        {.stage = Stage::Copy, .access = Access::CopyWrite, .layout = Layout::Undefined},
        {.offset = 0, .size = kExposureOffset + 4});
    INFO(errorOf(bytes));
    REQUIRE(bytes);
    std::array<uint16_t, 2> packedMotion0{};
    std::array<uint16_t, 2> packedMotion1{};
    uint16_t packedExposure = 0;
    std::memcpy(packedMotion0.data(), bytes->data() + kMotionOffset, 4);
    std::memcpy(packedMotion1.data(), bytes->data() + kMotionOffset + 4, 4);
    std::memcpy(&packedExposure, bytes->data() + kExposureOffset, 2);
    REQUIRE(packedMotion0[0] == 0);
    REQUIRE(packedMotion0[1] == 0);
    REQUIRE(packedMotion1[0] == motionBits[2]);
    REQUIRE(packedMotion1[1] == motionBits[3]);
    REQUIRE((*bytes)[kReactiveOffset] == 255);
    REQUIRE((*bytes)[kReactiveOffset + 1] == 128);
    REQUIRE(packedExposure ==
            0x3400); // Reciprocal of applied exposure 4, represented as half 0.25.
    REQUIRE(adapter.generation() == 0);
}
