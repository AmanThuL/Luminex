#include "GpuTestSupport.h"

namespace {

// Mirrors ComputeSmoke.slang's ComputeParams; the layout is pinned by the shader, not chosen here.
struct ComputeParams {
    uint32_t bias = 0;
    uint32_t extent = 0;
};

// computeFillBuffer's [numthreads]. The RHI takes threadgroup counts, so every dispatch below
// derives its count from this.
constexpr uint32_t kFillThreadsPerGroup = 64;

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
