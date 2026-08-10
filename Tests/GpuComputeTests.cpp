#include "GpuTestSupport.h"

namespace {

// Mirrors ComputeSmoke.slang's ComputeParams; the layout is pinned by the shader, not chosen here.
struct ComputeParams {
    uint32_t bias = 0;
    uint32_t extent = 0;
};

// Mirrors ComputeImageSmoke.slang's ImageParams.
struct ImageParams {
    uint32_t extent = 0;
};

// computeFillBuffer's [numthreads]. The RHI takes threadgroup counts, so every dispatch below
// derives its count from this.
constexpr uint32_t kFillThreadsPerGroup = 64;

// The image kernels' [numthreads] in x and y.
constexpr uint32_t kImageThreadsPerGroup = 8;

// The argument-table slots ComputeImageSmoke.slang's globals compile to.
constexpr uint32_t kImageSlot = 0;
constexpr uint32_t kParamsSlot = 1;

//======================================================================================================================
// The gradient computeWriteImage writes, as the 8-bit channel value at a texel of a kSize image.
uint8_t gradientChannel(uint32_t coordinate) {
    const float value = static_cast<float>(coordinate) / static_cast<float>(kSize - 1);
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

} // namespace

//======================================================================================================================
// The element values are distinct arithmetic results rather than a constant, so a dispatch that
// writes the wrong index, skips a threadgroup, or never runs at all fails on a specific element.
TEST_CASE("a dispatch fills a storage buffer the CPU reads back", "[gpu]") {
    using namespace lmx::rhi;

    constexpr uint32_t kElements = 256;
    constexpr uint32_t kBias = 11;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/ComputeSmoke");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto pipeline =
        (*device)->createComputePipeline({.library = library->get(),
                                          .computeEntry = "computeFillBuffer",
                                          .threadsPerThreadgroup = {kFillThreadsPerGroup, 1, 1},
                                          .label = "lmx.test.compute.fillPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    auto storage = (*device)->createBuffer({.size = sizeof(uint32_t) * kElements,
                                            .storageWrite = true,
                                            .cpuReadback = true,
                                            .label = "lmx.test.compute.fillStorage"},
                                           nullptr);
    INFO(errorOf(storage));
    REQUIRE(storage.has_value());

    const ComputeParams params{.bias = kBias, .extent = 0};

    CommandList& commands = (*device)->beginFrame();
    commands.beginComputePass("lmx.test.compute.fill");
    commands.bindComputePipeline(**pipeline);
    commands.bindStorageBuffer(0, **storage, StorageAccess::Write);
    commands.setUniforms(1, &params, sizeof(params));
    commands.dispatch(kElements / kFillThreadsPerGroup, 1, 1);
    commands.endComputePass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint32_t> values(kElements, 0);
    (*storage)->readback(values.data(), values.size() * sizeof(uint32_t));

    for (uint32_t index = 0; index < kElements; ++index) {
        INFO("element " + std::to_string(index));
        REQUIRE(values[index] == index * 3 + kBias);
    }
}

//======================================================================================================================
// The gradient is written by a compute kernel and read straight back, so the case pins the storage
// binding itself: usage, texel addressing, and channel order, with no second pass in between.
TEST_CASE("a dispatch writes a storage texture the CPU reads back", "[gpu]") {
    using namespace lmx::rhi;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/ComputeImageSmoke");
    INFO(errorOf(library));
    REQUIRE(library.has_value());

    auto pipeline = (*device)->createComputePipeline(
        {.library = library->get(),
         .computeEntry = "computeWriteImage",
         .threadsPerThreadgroup = {kImageThreadsPerGroup, kImageThreadsPerGroup, 1},
         .label = "lmx.test.compute.writeImagePipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    auto image = (*device)->createTexture({.width = kSize,
                                           .height = kSize,
                                           .format = Format::RGBA8Unorm,
                                           .storageWrite = true,
                                           .cpuReadback = true,
                                           .label = "lmx.test.compute.image"});
    INFO(errorOf(image));
    REQUIRE(image.has_value());

    const ImageParams params{.extent = kSize};

    CommandList& commands = (*device)->beginFrame();
    commands.beginComputePass("lmx.test.compute.writeImage");
    commands.bindComputePipeline(**pipeline);
    commands.bindStorageTexture(kImageSlot, **image, {}, StorageAccess::Write);
    commands.setUniforms(kParamsSlot, &params, sizeof(params));
    commands.dispatch(kSize / kImageThreadsPerGroup, kSize / kImageThreadsPerGroup, 1);
    commands.endComputePass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint8_t> pixels(size_t{kSize} * kSize * 4);
    (*image)->readback(pixels.data(), pixels.size());

    // Corners and one interior texel: enough to catch a transposed, flipped, or constant write.
    const std::array<std::pair<uint32_t, uint32_t>, 4> probes = {
        {{0, 0}, {kSize - 1, 0}, {0, kSize - 1}, {21, 42}}};
    for (const auto& [x, y] : probes) {
        const size_t offset = (size_t{y} * kSize + x) * 4;
        INFO("texel (" + std::to_string(x) + "," + std::to_string(y) + ")");
        REQUIRE(pixels[offset] == gradientChannel(x));
        REQUIRE(pixels[offset + 1] == gradientChannel(y));
        REQUIRE(channelNear(pixels[offset + 2], 64, 1));
        REQUIRE(pixels[offset + 3] == 255);
    }
}
