#include "GpuTestSupport.h"
#include "Render/Occlusion.h"
#include <bit>
#include <cmath>
#include <limits>

using namespace lmx::render;
using namespace rojoRHI;

namespace {
struct ProbeBounds {
    glm::vec4 minimum;
    glm::vec4 maximum;
};
static_assert(sizeof(ProbeBounds) == 32);
//======================================================================================================================
void compareProbe(Device& device, ComputePipeline& pipeline, const OcclusionParams& params,
                  std::span<const lmx::Aabb> bounds, float depth) {
    std::array<std::vector<float>, 2> pixels{std::vector<float>(32 * 24, depth),
                                             std::vector<float>(16 * 12, depth)};
    const std::array<TextureMip, 2> mips{
        {{pixels[0].data(), 32 * sizeof(float)}, {pixels[1].data(), 16 * sizeof(float)}}};
    auto pyramid = device.createTexture({.width = 32,
                                         .height = 24,
                                         .format = Format::R32Float,
                                         .mipLevels = 2,
                                         .sampled = true,
                                         .label = "lmx.test.occlusion.probe.depth"},
                                        mips);
    INFO(errorOf(pyramid));
    REQUIRE(pyramid);
    std::vector<ProbeBounds> inputs;
    for (const auto& box : bounds)
        inputs.push_back({glm::vec4(box.minimum, 0), glm::vec4(box.maximum, 0)});
    auto input = device.createBuffer(
        {.size = inputs.size() * sizeof(ProbeBounds), .label = "lmx.test.occlusion.probe.bounds"},
        inputs.data());
    auto output = device.createBuffer({.size = inputs.size() * 8 * sizeof(uint32_t),
                                       .storageWrite = true,
                                       .cpuReadback = true,
                                       .label = "lmx.test.occlusion.probe.output"},
                                      nullptr);
    REQUIRE(input);
    REQUIRE(output);
    auto& commands = device.beginFrame();
    commands.beginComputePass("lmx.test.occlusion.probe");
    commands.bindComputePipeline(pipeline);
    commands.bindBuffer(0, **input);
    commands.bindStorageBuffer(1, **output, StorageAccess::Write);
    commands.bindFrameData(13, params);
    commands.bindTexture(0, **pyramid);
    commands.dispatch(static_cast<uint32_t>(bounds.size()), 1, 1);
    commands.endComputePass();
    device.endFrame(nullptr);
    device.waitIdle();
    std::vector<uint32_t> actual(inputs.size() * 8);
    (*output)->readback(actual.data(), actual.size() * sizeof(uint32_t));
    const std::array<OcclusionLevel, 2> levels{{{pixels[0], 32}, {pixels[1], 16}}};
    for (size_t i = 0; i < bounds.size(); ++i) {
        CAPTURE(i, depth, params.flags, params.sourceWidth, params.levelCount);
        const auto expected = testOcclusionBounds(bounds[i], params, levels);
        const std::array<uint32_t, 8> words{std::bit_cast<uint32_t>(expected.rectangle[0]),
                                            std::bit_cast<uint32_t>(expected.rectangle[1]),
                                            std::bit_cast<uint32_t>(expected.rectangle[2]),
                                            std::bit_cast<uint32_t>(expected.rectangle[3]),
                                            expected.level,
                                            std::bit_cast<uint32_t>(expected.zBox),
                                            static_cast<uint32_t>(expected.outcome),
                                            expected.occluded ? 1u : 0u};
        for (size_t word = 0; word < words.size(); ++word) {
            CAPTURE(word);
            REQUIRE(actual[i * 8 + word] == words[word]);
        }
    }
}
} // namespace

//======================================================================================================================
TEST_CASE("occlusion shader matches CPU outcomes rectangles levels and depth bits",
          "[gpu][occlusion-probe]") {
    auto device = createDevice();
    REQUIRE(device);
    auto library = (*device)->loadShaderLibrary("Shaders/OcclusionProbe");
    INFO(errorOf(library));
    REQUIRE(library);
    auto pipeline =
        (*device)->createComputePipeline({.library = library->get(),
                                          .computeEntry = "computeMain",
                                          .threadsPerThreadgroup = {1, 1, 1},
                                          .label = "lmx.test.occlusion.probe.pipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<lmx::Aabb, 8> bounds{{{{-0.01f, -0.01f, 0.1f}, {0.01f, 0.01f, 0.2f}},
                                           {{-0.08f, -0.08f, 0.1f}, {0.08f, 0.08f, 0.2f}},
                                           {{-0.8f, -0.8f, 0.1f}, {0.8f, 0.8f, 0.2f}},
                                           {{-1.0f, -0.01f, 0.1f}, {-0.99f, 0.01f, 0.2f}},
                                           {{0.99f, -0.01f, 0.1f}, {1.0f, 0.01f, 0.2f}},
                                           {{-0.01f, -0.01f, 0.9f}, {0.01f, 0.01f, 1.1f}},
                                           {{nan, -0.01f, 0.1f}, {nan, 0.01f, 0.2f}},
                                           {{0.0625f, 0.0625f, 0.2f}, {0.0625f, 0.0625f, 0.2f}}}};
    auto params = makeOcclusionParams(glm::mat4(1), 64, 48, 2, true, true);
    REQUIRE(projectOcclusionBounds(bounds[0], params).outcome == OcclusionOutcome::Retained);
    REQUIRE(projectOcclusionBounds(bounds[0], params).level == 0);
    REQUIRE(projectOcclusionBounds(bounds[1], params).outcome == OcclusionOutcome::Retained);
    REQUIRE(projectOcclusionBounds(bounds[1], params).level == 1);
    REQUIRE(projectOcclusionBounds(bounds[2], params).outcome == OcclusionOutcome::RectTooLarge);
    REQUIRE(projectOcclusionBounds(bounds[3], params).outcome == OcclusionOutcome::OutsideSource);
    REQUIRE(projectOcclusionBounds(bounds[4], params).outcome == OcclusionOutcome::OutsideSource);
    REQUIRE(projectOcclusionBounds(bounds[5], params).outcome == OcclusionOutcome::NearCrossing);
    REQUIRE(projectOcclusionBounds(bounds[6], params).outcome == OcclusionOutcome::NearCrossing);
    compareProbe(**device, **pipeline, params, bounds, 0.8f);
    const auto small = std::span{bounds}.first(1);
    const float threshold = 0.2f + kOcclusionDepthGuard;
    for (const float depth :
         {std::nextafter(threshold, 0.0f), threshold, std::nextafter(threshold, 1.0f), 0.0f})
        compareProbe(**device, **pipeline, params, small, depth);
    params.levelCount = 1;
    compareProbe(**device, **pipeline, params, bounds, 0.8f);
    params.sourceWidth = 63;
    params.sourceHeight = 47;
    params.levelCount = 2;
    params.sourceRows[0].w = 1.0f / 64.0f;
    params.sourceRows[1].w = -1.0f / 48.0f;
    compareProbe(**device, **pipeline, params, bounds, 0.8f);
    params.flags = 1;
    compareProbe(**device, **pipeline, params, bounds, 0.8f);
    params.flags = 0;
    compareProbe(**device, **pipeline, params, bounds, 0.8f);
    params.flags = 3;
    params.sourceRows[3] = {0, 0, 0, kOcclusionNearGuard};
    compareProbe(**device, **pipeline, params, bounds, 0.8f);
}
