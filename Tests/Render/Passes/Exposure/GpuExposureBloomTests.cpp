//----------------------------------------------------------------------------------------------------------------------
/// @file GpuExposureBloomTests.cpp
/// @brief GPU conformance tests for the histogram exposure kernels vs CPU references.
//----------------------------------------------------------------------------------------------------------------------

// Run directly against CPU reference implementations that mirror the shaders line for line --
// Shaders/Passes/Exposure/HistogramAccumulate.slang and
// Shaders/Passes/Exposure/ExposureResolve.slang
// -- rather than through Renderer::declarePasses(), so a failure here points at one kernel's math
// instead of the whole frame's wiring (spec 9/10).

#include "Engine/Gpu/GpuOwned.h"
#include "Render/Common/FrameData.h"
#include "Render/Passes/Display/DisplayRoots.h"
#include "Render/Passes/Exposure/ExposureFrameData.h"
#include "Render/Passes/Exposure/ExposureRoots.h"
#include "Support/DisplayTransformOracle.h"

#include "Support/ExposureBloomTestSupport.h"
#include "Support/GpuTestSupport.h"
#include <cstring>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Engine/Asset/Model/GeometryGenerator.h"
#include "Engine/Geometry/Mesh.h"
#include "Engine/View/Camera.h"
#include "Render/Renderer/Renderer.h"
#include <rojoRHI/RHI.h>

#include <glm/glm.hpp>

#include "Support/SceneTableTestSupport.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

using lmx::render::ExposureResolveParams;
using lmx::render::ExposureSeedParams;
using lmx::render::HistogramParams;
using lmx::test::FixtureDrawItem;
using lmx::test::FixtureMaterial;
using lmx::test::FixtureMesh;
using lmx::test::fixtureMesh;
using lmx::test::FixtureSceneView;

namespace {

using namespace rojoRHI;

using lmx::test::halfPow2;
using lmx::test::makeSceneColorTexture;

// Mirrors HistogramAccumulate.slang's kHistogramBins/kLuminanceWeights and its own and
// ExposureResolve.slang's shared binning range -- Renderer.cpp's kExposureLogLuminanceMin/Max.
constexpr uint32_t kBins = 256;
constexpr glm::vec3 kLuminanceWeights{0.2126f, 0.7152f, 0.0722f};
constexpr float kLogLuminanceMin = -12.0f;
constexpr float kLogLuminanceMax = 4.0f;

//======================================================================================================================
// Mirrors HistogramAccumulate.slang's computeHistogramAccumulate, texel for texel.
std::array<uint32_t, kBins> cpuHistogram(std::span<const glm::vec3> pixels, float preExposure) {
    std::array<uint32_t, kBins> bins{};
    const float range = std::max(kLogLuminanceMax - kLogLuminanceMin, 1e-6f);
    for (const glm::vec3& color : pixels) {
        const float displayedLuminance = glm::dot(color, kLuminanceWeights);
        const float sceneLuminance = displayedLuminance / std::max(preExposure, 1e-6f);
        const float logLuminance = std::log2(std::max(sceneLuminance, 1e-6f));
        const float t = std::clamp((logLuminance - kLogLuminanceMin) / range, 0.0f, 1.0f);
        const uint32_t bin =
            std::min(static_cast<uint32_t>(t * static_cast<float>(kBins)), kBins - 1u);
        ++bins[bin];
    }
    return bins;
}

// The two floats Shaders/Passes/Exposure/ExposureResolve.slang and
// Shaders/Passes/Exposure/ExposureSeed.slang keep in lmx.render.exposureBuffer: what the scene pass
// multiplies by next, and what it multiplied by on the previous declared temporal frame (M6.2 spec
// 7).
struct ExposurePair {
    float applied = 1.0f;
    float previous = 1.0f;
};

//======================================================================================================================
// Mirrors ExposureResolve.slang's computeExposureResolve, statement for statement -- including the
// bounded EV step it adapts by, which is why it takes the exposure the frame applied and the two
// rates alongside spec 9's metering parameters.
ExposurePair cpuResolveExposure(const std::array<uint32_t, kBins>& bins, float lowPercentile,
                                float highPercentile, float targetGrey, float evMin, float evMax,
                                float compensationEv, float applied, float adaptUpStopsPerSecond,
                                float adaptDownStopsPerSecond, float deltaSeconds) {
    uint64_t totalCount = 0;
    for (uint32_t count : bins) {
        totalCount += count;
    }
    if (totalCount == 0) {
        return {.applied = 1.0f, .previous = applied};
    }

    const float lowCount = lowPercentile * 0.01f * static_cast<float>(totalCount);
    const float highCount = highPercentile * 0.01f * static_cast<float>(totalCount);
    const float binWidth = (kLogLuminanceMax - kLogLuminanceMin) / static_cast<float>(kBins);

    float weightedLogLuminance = 0.0f;
    float weight = 0.0f;
    float cumulative = 0.0f;
    for (uint32_t i = 0; i < kBins; ++i) {
        const float count = static_cast<float>(bins[i]);
        const float binStart = cumulative;
        const float binEnd = cumulative + count;
        const float overlap =
            std::max(std::min(binEnd, highCount) - std::max(binStart, lowCount), 0.0f);
        const float binCenterLogLuminance =
            kLogLuminanceMin + (static_cast<float>(i) + 0.5f) * binWidth;
        weightedLogLuminance += overlap * binCenterLogLuminance;
        weight += overlap;
        cumulative = binEnd;
    }

    const float averageLogLuminance = weight > 0.0f ? weightedLogLuminance / weight : 0.0f;
    const float averageLuminance = std::exp2(averageLogLuminance);
    const float exposure =
        (targetGrey / std::max(averageLuminance, 1e-6f)) * std::exp2(compensationEv);

    const float rate = exposure > applied ? adaptUpStopsPerSecond : adaptDownStopsPerSecond;
    float adapted = exposure;
    if (rate > 0.0f) {
        const float maxStep = rate * deltaSeconds;
        const float evApplied = std::log2(std::max(applied, 1e-6f));
        const float evTarget = std::log2(std::max(exposure, 1e-6f));
        adapted = std::exp2(evApplied + std::clamp(evTarget - evApplied, -maxStep, maxStep));
    }
    return {.applied = std::clamp(adapted, std::exp2(evMin), std::exp2(evMax)),
            .previous = applied};
}

} // namespace

//======================================================================================================================
// A known 4x4 image (13 texels at luminance 1, one each at 2, 4, and 0.5 -- all exact powers of
// two, so both the display->scene-referred division and the log2 binning are exact) run through
// the real compute kernel and compared bin-for-bin against the CPU port above.
TEST_CASE("the GPU histogram matches a CPU reference on a known image", "[gpu]") {
    constexpr uint32_t kWidth = 4, kHeight = 4;
    constexpr float kPreExposure = 1.0f;

    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);

    std::vector<glm::vec3> pixels(size_t{kWidth} * kHeight,
                                  glm::vec3(1.0f)); // luminance 1 (bin 192)
    pixels[0] = glm::vec3(2.0f);                    // luminance 2 (bin 208)
    pixels[1] = glm::vec3(4.0f);                    // luminance 4 (bin 224)
    pixels[2] = glm::vec3(0.5f);                    // luminance 0.5 (bin 176)

    std::vector<uint16_t> rgbaHalf(pixels.size() * 4);
    for (size_t i = 0; i < pixels.size(); ++i) {
        const int exponent = static_cast<int>(std::round(std::log2(pixels[i].r)));
        const uint16_t channel = halfPow2(exponent);
        rgbaHalf[i * 4 + 0] = channel;
        rgbaHalf[i * 4 + 1] = channel;
        rgbaHalf[i * 4 + 2] = channel;
        rgbaHalf[i * 4 + 3] = halfPow2(0); // alpha 1.0
    }

    auto sceneColor =
        makeSceneColorTexture(*gpu, kWidth, kHeight, rgbaHalf, "lmx.test.histogramImage");
    INFO(lmx::test::errorOf(sceneColor));
    REQUIRE(sceneColor.has_value());

    auto library = gpu->loadShaderLibrary("HistogramAccumulate");
    INFO(lmx::test::errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = lmx::engine::adoptGpu(
        gpu->device(), gpu->device().createPipeline(ComputePipelineDesc{
                           .compute = {library->get(), "computeHistogramAccumulate"},
                           .label = "lmx.test.histogramPipeline"}));
    INFO(lmx::test::errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    auto histogram =
        lmx::engine::GpuBuffer::create(gpu->device(), {.size = kBins * sizeof(uint32_t),
                                                       .usage = BufferUsage::ShaderWrite,
                                                       .label = "lmx.test.histogramBuffer"});
    INFO(lmx::test::errorOf(histogram));
    REQUIRE(histogram.has_value());
    // The kernel reads this frame's applied preExposure from a buffer, not a uniform (spec 9: the
    // same buffer ScenePass.slang/Sky.slang read in auto mode). A plain initial-data buffer stands
    // in for it here.
    auto exposure = lmx::engine::GpuBuffer::create(
        gpu->device(), {.size = sizeof(float), .label = "lmx.test.histogramExposureBuffer"},
        std::as_bytes(std::span{&kPreExposure, 1}));
    INFO(lmx::test::errorOf(exposure));
    REQUIRE(exposure.has_value());

    const HistogramParams params{.logLuminanceMin = kLogLuminanceMin,
                                 .logLuminanceMax = kLogLuminanceMax,
                                 .width = kWidth,
                                 .height = kHeight};

    auto commandsFrame = gpu->beginFrame();
    auto& commands = commandsFrame.commands;
    commands.beginScope("lmx.test.clearHistogram");
    commands.fillBuffer(histogram->rhi(), 0, kBins * sizeof(uint32_t), 0);
    commands.endScope();
    const BufferBarrier histogramAfterClear{.buffer = histogram->rhi(),
                                            .size = kBins * sizeof(uint32_t),
                                            .srcStage = Stage::Copy,
                                            .srcAccess = Access::CopyWrite,
                                            .dstStage = Stage::ComputeShader,
                                            .dstAccess = Access::ShaderWrite};
    commands.barrier({}, std::span{&histogramAfterClear, 1}, {});

    commands.beginScope("lmx.test.histogram");
    commands.setPipeline(pipeline->get());
    commands.pushRoot(
        lmx::render::HistogramRoot{.histogramAddr = histogram->address(),
                                   .exposureAddr = exposure->address(),
                                   .paramsAddr = lmx::render::frameData(commands, params),
                                   .sceneColor = static_cast<uint32_t>(sceneColor->view())});
#ifndef NDEBUG
    const std::array<ResourceAccess, 3> histogramAccesses{{{.texture = sceneColor->rhi(),
                                                            .range = {0, 1, 0, 1},
                                                            .stage = Stage::ComputeShader,
                                                            .access = Access::ShaderRead},
                                                           {.buffer = exposure->rhi(),
                                                            .size = sizeof(float),
                                                            .stage = Stage::ComputeShader,
                                                            .access = Access::ShaderRead},
                                                           {.buffer = histogram->rhi(),
                                                            .size = kBins * sizeof(uint32_t),
                                                            .stage = Stage::ComputeShader,
                                                            .access = Access::ShaderWrite}}};
    commands.declareAccess(histogramAccesses);
#endif
    commands.dispatch(1, 1, 1); // 4x4 fits one 8x8 threadgroup; the kernel bounds-checks the rest.
    commands.endScope();
    gpu->submitAndEndFrame(commandsFrame);
    gpu->waitIdle();

    std::array<uint32_t, kBins> gpuBins{};
    {
        const auto readbackBytes = lmx::render::readbackNow(
            *gpu, *histogram, {Stage::ComputeShader, Access::ShaderWrite, Layout::Undefined},
            {0, gpuBins.size() * sizeof(uint32_t)});
        INFO(lmx::test::errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(gpuBins.data(), readbackBytes->data(), gpuBins.size() * sizeof(uint32_t));
    }

    const std::array<uint32_t, kBins> expected = cpuHistogram(pixels, kPreExposure);
    for (uint32_t bin = 0; bin < kBins; ++bin) {
        if (gpuBins[bin] != 0 || expected[bin] != 0) {
            INFO("bin " << bin);
            REQUIRE(gpuBins[bin] == expected[bin]);
        }
    }
    REQUIRE(expected[192] == 13);
    REQUIRE(expected[208] == 1);
    REQUIRE(expected[224] == 1);
    REQUIRE(expected[176] == 1);
}

//======================================================================================================================
// A uniform image at a known scene-referred luminance, metered wide open (0th/100th percentile,
// so every pixel's one bin is kept whole), resolved in one dispatch and compared against the CPU
// port of the exact same binning + weighted-average formula -- the one-frame convergence spec 9
// requires: at a known manual EV (folded into kPreExposure here, since the resolve pass never
// reads EV directly) and known content, the target exposure equals the CPU prediction after
// exactly one frame's histogram + resolve.
//
// Both adaptation rates are zero, which M6.2 spec 7 defines as adaptation disabled: the resolve
// takes the instantaneous target unchanged, so this stays the same bit-for-bit assertion over the
// same metering formula it was before there was an EV step at all.
TEST_CASE("auto exposure converges to the CPU-predicted target after one frame", "[gpu]") {
    constexpr uint32_t kWidth = 8, kHeight = 8;
    constexpr float kPreExposure = 1.0f;    // exp2(manual EV 0)
    constexpr float kSceneLuminance = 2.0f; // every pixel
    constexpr float kTargetGrey = 0.18f;
    constexpr float kEvMin = -100.0f, kEvMax = 100.0f; // wide enough the clamp never engages
    constexpr float kCompensationEv = 0.0f;
    constexpr float kLowPercentile = 0.0f, kHighPercentile = 100.0f;
    constexpr float kAdaptUp = 0.0f, kAdaptDown = 0.0f; // adaptation disabled -- instantaneous
    constexpr float kDeltaSeconds = 1.0f / 60.0f;       // Renderer.cpp's kExposureFrameSeconds

    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);

    std::vector<uint16_t> rgbaHalf(size_t{kWidth} * kHeight * 4);
    const uint16_t channel = halfPow2(1); // 2.0
    for (size_t i = 0; i < size_t{kWidth} * kHeight; ++i) {
        rgbaHalf[i * 4 + 0] = channel;
        rgbaHalf[i * 4 + 1] = channel;
        rgbaHalf[i * 4 + 2] = channel;
        rgbaHalf[i * 4 + 3] = halfPow2(0);
    }
    auto sceneColor =
        makeSceneColorTexture(*gpu, kWidth, kHeight, rgbaHalf, "lmx.test.convergenceImage");
    INFO(lmx::test::errorOf(sceneColor));
    REQUIRE(sceneColor.has_value());

    auto histogramLibrary = gpu->loadShaderLibrary("HistogramAccumulate");
    INFO(lmx::test::errorOf(histogramLibrary));
    REQUIRE(histogramLibrary.has_value());
    auto histogramPipeline = lmx::engine::adoptGpu(
        gpu->device(), gpu->device().createPipeline(ComputePipelineDesc{
                           .compute = {histogramLibrary->get(), "computeHistogramAccumulate"},
                           .label = "lmx.test.convergenceHistogramPipeline"}));
    INFO(lmx::test::errorOf(histogramPipeline));
    REQUIRE(histogramPipeline.has_value());

    auto resolveLibrary = gpu->loadShaderLibrary("ExposureResolve");
    INFO(lmx::test::errorOf(resolveLibrary));
    REQUIRE(resolveLibrary.has_value());
    auto resolvePipeline = lmx::engine::adoptGpu(
        gpu->device(), gpu->device().createPipeline(ComputePipelineDesc{
                           .compute = {resolveLibrary->get(), "computeExposureResolve"},
                           .label = "lmx.test.resolvePipeline"}));
    INFO(lmx::test::errorOf(resolvePipeline));
    REQUIRE(resolvePipeline.has_value());

    auto histogram = lmx::engine::GpuBuffer::create(
        gpu->device(), {.size = kBins * sizeof(uint32_t),
                        .usage = BufferUsage::ShaderWrite,
                        .label = "lmx.test.convergenceHistogramBuffer"});
    INFO(lmx::test::errorOf(histogram));
    REQUIRE(histogram.has_value());
    // The buffer the histogram kernel reads "this frame's applied preExposure" from (spec 9) --
    // separate from `exposure` below (what the resolve pass writes for *next* frame), exactly as
    // Renderer.cpp's two-buffer-role read/write is, just without the graph's version-chain naming
    // both roles onto one physical buffer.
    auto appliedExposure = lmx::engine::GpuBuffer::create(
        gpu->device(),
        {.size = sizeof(float), .label = "lmx.test.convergenceAppliedExposureBuffer"},
        std::as_bytes(std::span{&kPreExposure, 1}));
    INFO(lmx::test::errorOf(appliedExposure));
    REQUIRE(appliedExposure.has_value());
    // The resolve reads the applied exposure it steps from out of index 0 and shifts it into
    // index 1, so the buffer it writes is the two-float pair and is seeded rather than left
    // uninitialised.
    constexpr std::array<float, 2> kInitialPair{kPreExposure, kPreExposure};
    auto exposure = lmx::engine::GpuBuffer::create(gpu->device(),
                                                   {.size = sizeof(kInitialPair),
                                                    .usage = BufferUsage::ShaderWrite,
                                                    .label = "lmx.test.convergenceExposureBuffer"},
                                                   std::as_bytes(std::span{kInitialPair}));
    INFO(lmx::test::errorOf(exposure));
    REQUIRE(exposure.has_value());

    const HistogramParams histogramParams{.logLuminanceMin = kLogLuminanceMin,
                                          .logLuminanceMax = kLogLuminanceMax,
                                          .width = kWidth,
                                          .height = kHeight};
    const ExposureResolveParams resolveParams{.lowPercentile = kLowPercentile,
                                              .highPercentile = kHighPercentile,
                                              .targetGrey = kTargetGrey,
                                              .evMin = kEvMin,
                                              .evMax = kEvMax,
                                              .compensationEv = kCompensationEv,
                                              .logLuminanceMin = kLogLuminanceMin,
                                              .logLuminanceMax = kLogLuminanceMax,
                                              .adaptUp = kAdaptUp,
                                              .adaptDown = kAdaptDown,
                                              .deltaSeconds = kDeltaSeconds,
                                              .pad = 0.0f};

    auto commandsFrame = gpu->beginFrame();
    auto& commands = commandsFrame.commands;
    commands.beginScope("lmx.test.convergenceClear");
    commands.fillBuffer(histogram->rhi(), 0, kBins * sizeof(uint32_t), 0);
    commands.endScope();
    const BufferBarrier histogramAfterClear{.buffer = histogram->rhi(),
                                            .size = kBins * sizeof(uint32_t),
                                            .srcStage = Stage::Copy,
                                            .srcAccess = Access::CopyWrite,
                                            .dstStage = Stage::ComputeShader,
                                            .dstAccess = Access::ShaderWrite};
    commands.barrier({}, std::span{&histogramAfterClear, 1}, {});

    commands.beginScope("lmx.test.convergenceHistogram");
    commands.setPipeline(histogramPipeline->get());
    commands.pushRoot(
        lmx::render::HistogramRoot{.histogramAddr = histogram->address(),
                                   .exposureAddr = appliedExposure->address(),
                                   .paramsAddr = lmx::render::frameData(commands, histogramParams),
                                   .sceneColor = static_cast<uint32_t>(sceneColor->view())});
#ifndef NDEBUG
    const std::array<ResourceAccess, 3> histogramAccesses{{{.texture = sceneColor->rhi(),
                                                            .range = {0, 1, 0, 1},
                                                            .stage = Stage::ComputeShader,
                                                            .access = Access::ShaderRead},
                                                           {.buffer = appliedExposure->rhi(),
                                                            .size = sizeof(float),
                                                            .stage = Stage::ComputeShader,
                                                            .access = Access::ShaderRead},
                                                           {.buffer = histogram->rhi(),
                                                            .size = kBins * sizeof(uint32_t),
                                                            .stage = Stage::ComputeShader,
                                                            .access = Access::ShaderWrite}}};
    commands.declareAccess(histogramAccesses);
#endif
    commands.dispatch(1, 1, 1);
    commands.endScope();

    const BufferBarrier histogramBeforeResolve{.buffer = histogram->rhi(),
                                               .size = kBins * sizeof(uint32_t),
                                               .srcStage = Stage::ComputeShader,
                                               .srcAccess = Access::ShaderWrite,
                                               .dstStage = Stage::ComputeShader,
                                               .dstAccess = Access::ShaderRead};
    commands.barrier({}, std::span{&histogramBeforeResolve, 1}, {});

    commands.beginScope("lmx.test.convergenceResolve");
    commands.setPipeline(resolvePipeline->get());
    commands.pushRoot(lmx::render::ExposureResolveRoot{
        .histogramAddr = histogram->address(),
        .exposureAddr = exposure->address(),
        .paramsAddr = lmx::render::frameData(commands, resolveParams)});
#ifndef NDEBUG
    const std::array<ResourceAccess, 2> resolveAccesses{
        {{.buffer = histogram->rhi(),
          .size = kBins * sizeof(uint32_t),
          .stage = Stage::ComputeShader,
          .access = Access::ShaderRead},
         {.buffer = exposure->rhi(),
          .size = 2 * sizeof(float),
          .stage = Stage::ComputeShader,
          .access = Access::ShaderRead | Access::ShaderWrite}}};
    commands.declareAccess(resolveAccesses);
#endif
    commands.dispatch(1, 1, 1);
    commands.endScope();
    gpu->submitAndEndFrame(commandsFrame);
    gpu->waitIdle();

    std::array<float, 2> gpuPair{};
    {
        const auto readbackBytes = lmx::render::readbackNow(
            *gpu, *exposure,
            {Stage::ComputeShader, Access::ShaderRead | Access::ShaderWrite, Layout::Undefined},
            {0, sizeof(gpuPair)});
        INFO(lmx::test::errorOf(readbackBytes));
        REQUIRE(readbackBytes.has_value());
        std::memcpy(gpuPair.data(), readbackBytes->data(), sizeof(gpuPair));
    }
    const float gpuExposure = gpuPair[0];

    const std::array<uint32_t, kBins> bins = cpuHistogram(
        std::vector<glm::vec3>(size_t{kWidth} * kHeight, glm::vec3(kSceneLuminance)), kPreExposure);
    const ExposurePair expected =
        cpuResolveExposure(bins, kLowPercentile, kHighPercentile, kTargetGrey, kEvMin, kEvMax,
                           kCompensationEv, kPreExposure, kAdaptUp, kAdaptDown, kDeltaSeconds);

    REQUIRE(gpuExposure == Catch::Approx(expected.applied).epsilon(1e-5));
    // The pair's second float is what the frame applied, which is what a history recorded at that
    // exposure has to be corrected by.
    REQUIRE(gpuPair[1] == Catch::Approx(kPreExposure));
    // The formula's own sanity: a uniform image at luminance 2 metered toward 0.18 grey wants an
    // exposure a little under 0.09 -- the binning's ~0.06-stop quantization is why "a little
    // under" rather than exact.
    REQUIRE(gpuExposure == Catch::Approx(0.09f).epsilon(0.05));
}

//======================================================================================================================
// spec 9's four reset triggers -- first frame, scene switch, auto-exposure enable, and resize --
// all resolve to the same GPU work in Renderer::declarePasses whenever EditorShell's
// shouldResetExposure() (App/Model/Rendering/Settings/ExposureReset.h,
// Tests/App/Model/Rendering/Settings/AppExposureResetTests.cpp) says a reset is pending: an
// ordinary computeExposureSeed dispatch that writes exp2(manualEV) into the persistent exposure
// buffer, no CPU readback involved, and no reason to run the same kernel four times over -- which
// trigger fired is a CPU-only decision this test does not need to remake. What belongs here is the
// one thing shouldResetExposure()'s unit tests cannot cover: that the kernel a reset actually
// dispatches does the right GPU-side thing with the seed value it is given.
TEST_CASE("the exposure seed kernel writes exp2(manual EV) when a reset is applied", "[gpu]") {
    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);

    auto library = gpu->loadShaderLibrary("ExposureSeed");
    INFO(lmx::test::errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline =
        lmx::engine::adoptGpu(gpu->device(), gpu->device().createPipeline(ComputePipelineDesc{
                                                 .compute = {library->get(), "computeExposureSeed"},
                                                 .label = "lmx.test.exposureSeedPipeline"}));
    INFO(lmx::test::errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    const auto runSeed = [&](float manualEv) {
        // A stale pair the buffer might otherwise still hold, standing in for whatever the
        // previous scene/session/size left in it -- the seed dispatch must overwrite index 0
        // regardless of what it was.
        constexpr std::array<float, 2> kStalePair{0.1f, 0.2f};
        auto exposure = lmx::engine::GpuBuffer::create(gpu->device(),
                                                       {.size = sizeof(kStalePair),
                                                        .usage = BufferUsage::ShaderWrite,
                                                        .label = "lmx.test.seedExposureBuffer"},
                                                       std::as_bytes(std::span{kStalePair}));
        INFO(lmx::test::errorOf(exposure));
        REQUIRE(exposure.has_value());
        const ExposureSeedParams params{.exposure = std::exp2(manualEv)};

        auto commandsFrame = gpu->beginFrame();
        auto& commands = commandsFrame.commands;
        commands.beginScope("lmx.test.exposureSeed");
        commands.setPipeline(pipeline->get());
        commands.pushRoot(
            lmx::render::ExposureSeedRoot{.exposureAddr = exposure->address(),
                                          .paramsAddr = lmx::render::frameData(commands, params)});
#ifndef NDEBUG
        const ResourceAccess exposureAccess{.buffer = exposure->rhi(),
                                            .size = 2 * sizeof(float),
                                            .stage = Stage::ComputeShader,
                                            .access = Access::ShaderRead | Access::ShaderWrite};
        commands.declareAccess(std::span{&exposureAccess, 1});
#endif
        commands.dispatch(1, 1, 1);
        commands.endScope();
        gpu->submitAndEndFrame(commandsFrame);
        gpu->waitIdle();

        std::array<float, 2> result{};
        {
            const auto readbackBytes = lmx::render::readbackNow(
                *gpu, *exposure,
                {Stage::ComputeShader, Access::ShaderRead | Access::ShaderWrite, Layout::Undefined},
                {0, sizeof(result)});
            INFO(lmx::test::errorOf(readbackBytes));
            REQUIRE(readbackBytes.has_value());
            std::memcpy(result.data(), readbackBytes->data(), sizeof(result));
        }
        return result[0];
    };

    constexpr float kManualEv = 2.0f;
    constexpr float kExpected = 4.0f; // exp2(2)

    REQUIRE(runSeed(kManualEv) == Catch::Approx(kExpected));
}

//======================================================================================================================
// The other half of the seed's contract (M6.2 spec 7): it shifts as well as sets. A manual-mode
// temporal frame declares it every frame precisely so the buffer records both the exposure the
// scene pass is about to apply and the one it applied last, which is the ratio a reprojected
// history has to be corrected by. Two dispatches over one buffer is the smallest thing that can
// tell a shift from an overwrite.
TEST_CASE("the exposure seed shifts the applied exposure into previous", "[gpu]") {
    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);

    auto library = gpu->loadShaderLibrary("ExposureSeed");
    INFO(lmx::test::errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline =
        lmx::engine::adoptGpu(gpu->device(), gpu->device().createPipeline(ComputePipelineDesc{
                                                 .compute = {library->get(), "computeExposureSeed"},
                                                 .label = "lmx.test.exposureShiftPipeline"}));
    INFO(lmx::test::errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    constexpr std::array<float, 2> kInitialPair{0.1f, 0.2f};
    auto exposure = lmx::engine::GpuBuffer::create(gpu->device(),
                                                   {.size = sizeof(kInitialPair),
                                                    .usage = BufferUsage::ShaderWrite,
                                                    .label = "lmx.test.shiftExposureBuffer"},
                                                   std::as_bytes(std::span{kInitialPair}));
    INFO(lmx::test::errorOf(exposure));
    REQUIRE(exposure.has_value());

    bool seeded = false;
    const auto seed = [&](float value) {
        const ExposureSeedParams params{.exposure = value};
        auto commandsFrame = gpu->beginFrame();
        auto& commands = commandsFrame.commands;
        if (seeded) {
            const BufferBarrier beforeSeed{.buffer = exposure->rhi(),
                                           .size = 2 * sizeof(float),
                                           .srcStage = Stage::ComputeShader,
                                           .srcAccess = Access::ShaderRead | Access::ShaderWrite,
                                           .dstStage = Stage::ComputeShader,
                                           .dstAccess = Access::ShaderRead | Access::ShaderWrite};
            commands.barrier({}, std::span{&beforeSeed, 1}, {});
        }
        commands.beginScope("lmx.test.exposureSeedShift");
        commands.setPipeline(pipeline->get());
        commands.pushRoot(
            lmx::render::ExposureSeedRoot{.exposureAddr = exposure->address(),
                                          .paramsAddr = lmx::render::frameData(commands, params)});
#ifndef NDEBUG
        const ResourceAccess exposureAccess{.buffer = exposure->rhi(),
                                            .size = 2 * sizeof(float),
                                            .stage = Stage::ComputeShader,
                                            .access = Access::ShaderRead | Access::ShaderWrite};
        commands.declareAccess(std::span{&exposureAccess, 1});
#endif
        commands.dispatch(1, 1, 1);
        commands.endScope();
        gpu->submitAndEndFrame(commandsFrame);
        gpu->waitIdle();

        seeded = true;
        std::array<float, 2> pair{};
        {
            const auto readbackBytes = lmx::render::readbackNow(
                *gpu, *exposure,
                {Stage::ComputeShader, Access::ShaderRead | Access::ShaderWrite, Layout::Undefined},
                {0, sizeof(pair)});
            INFO(lmx::test::errorOf(readbackBytes));
            REQUIRE(readbackBytes.has_value());
            std::memcpy(pair.data(), readbackBytes->data(), sizeof(pair));
        }
        return pair;
    };

    const std::array<float, 2> first = seed(1.0f);
    REQUIRE(first[0] == Catch::Approx(1.0f));
    REQUIRE(first[1] == Catch::Approx(kInitialPair[0]));

    const std::array<float, 2> second = seed(4.0f);
    REQUIRE(second[0] == Catch::Approx(4.0f));
    REQUIRE(second[1] == Catch::Approx(1.0f));
}

//======================================================================================================================
// M6.2 spec 7's adaptation, measured the way the spec states it: a bounded step in stops per
// second, so settle time follows from step size and rate exactly rather than from an exponential
// tail nobody can put a frozen tolerance on. The histogram is held fixed, which pins the
// instantaneous target while the applied exposure walks toward it -- 3 stops away at 3 stops per
// second is half way (1.5 stops) after 30 frames of the App's fixed 1/60 s step and settled after
// 60, in either direction. The last case is the rate *selection*: descending picks adaptDown.
TEST_CASE("auto exposure adapts toward its target at the stated stops per second", "[gpu]") {
    constexpr uint32_t kWidth = 8, kHeight = 8;
    constexpr float kSceneLuminance = 2.0f;
    constexpr float kTargetGrey = 0.18f;
    constexpr float kEvMin = -100.0f, kEvMax = 100.0f; // wide enough the clamp never engages
    constexpr float kCompensationEv = 0.0f;
    constexpr float kLowPercentile = 0.0f, kHighPercentile = 100.0f;
    constexpr float kDeltaSeconds = 1.0f / 60.0f; // Renderer.cpp's kExposureFrameSeconds

    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);

    auto library = gpu->loadShaderLibrary("ExposureResolve");
    INFO(lmx::test::errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = lmx::engine::adoptGpu(gpu->device(),
                                          gpu->device().createPipeline(ComputePipelineDesc{
                                              .compute = {library->get(), "computeExposureResolve"},
                                              .label = "lmx.test.adaptResolvePipeline"}));
    INFO(lmx::test::errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    // A histogram the resolve re-reads unchanged every frame, so the target it meters is one
    // constant the adaptation walks toward.
    const std::array<uint32_t, kBins> bins = cpuHistogram(
        std::vector<glm::vec3>(size_t{kWidth} * kHeight, glm::vec3(kSceneLuminance)), 1.0f);
    auto histogram = lmx::engine::GpuBuffer::create(gpu->device(),
                                                    {.size = sizeof(bins),
                                                     .usage = BufferUsage::ShaderWrite,
                                                     .label = "lmx.test.adaptHistogramBuffer"},
                                                    std::as_bytes(std::span{bins}));
    INFO(lmx::test::errorOf(histogram));
    REQUIRE(histogram.has_value());

    // The instantaneous target this histogram meters to, taken from the CPU reference with
    // adaptation disabled -- the same number the rate-0 convergence case above asserts the GPU
    // produces, so the walk below is measured against a target the GPU agrees on.
    const float target =
        cpuResolveExposure(bins, kLowPercentile, kHighPercentile, kTargetGrey, kEvMin, kEvMax,
                           kCompensationEv, 1.0f, 0.0f, 0.0f, kDeltaSeconds)
            .applied;

    const auto runFrames = [&](float initialApplied, float adaptUp, float adaptDown,
                               uint32_t frames) {
        const std::array<float, 2> initial{initialApplied, initialApplied};
        auto exposure = lmx::engine::GpuBuffer::create(gpu->device(),
                                                       {.size = sizeof(initial),
                                                        .usage = BufferUsage::ShaderWrite,
                                                        .label = "lmx.test.adaptExposureBuffer"},
                                                       std::as_bytes(std::span{initial}));
        INFO(lmx::test::errorOf(exposure));
        REQUIRE(exposure.has_value());

        const ExposureResolveParams params{.lowPercentile = kLowPercentile,
                                           .highPercentile = kHighPercentile,
                                           .targetGrey = kTargetGrey,
                                           .evMin = kEvMin,
                                           .evMax = kEvMax,
                                           .compensationEv = kCompensationEv,
                                           .logLuminanceMin = kLogLuminanceMin,
                                           .logLuminanceMax = kLogLuminanceMax,
                                           .adaptUp = adaptUp,
                                           .adaptDown = adaptDown,
                                           .deltaSeconds = kDeltaSeconds,
                                           .pad = 0.0f};

        for (uint32_t frame = 0; frame < frames; ++frame) {
            auto commandsFrame = gpu->beginFrame();
            auto& commands = commandsFrame.commands;
            if (frame != 0) {
                const BufferBarrier beforeResolve{
                    .buffer = exposure->rhi(),
                    .size = 2 * sizeof(float),
                    .srcStage = Stage::ComputeShader,
                    .srcAccess = Access::ShaderRead | Access::ShaderWrite,
                    .dstStage = Stage::ComputeShader,
                    .dstAccess = Access::ShaderRead | Access::ShaderWrite};
                commands.barrier({}, std::span{&beforeResolve, 1}, {});
            }
            commands.beginScope("lmx.test.adaptResolve");
            commands.setPipeline(pipeline->get());
            commands.pushRoot(lmx::render::ExposureResolveRoot{
                .histogramAddr = histogram->address(),
                .exposureAddr = exposure->address(),
                .paramsAddr = lmx::render::frameData(commands, params)});
#ifndef NDEBUG
            const std::array<ResourceAccess, 2> resolveAccesses{
                {{.buffer = histogram->rhi(),
                  .size = kBins * sizeof(uint32_t),
                  .stage = Stage::ComputeShader,
                  .access = Access::ShaderRead},
                 {.buffer = exposure->rhi(),
                  .size = 2 * sizeof(float),
                  .stage = Stage::ComputeShader,
                  .access = Access::ShaderRead | Access::ShaderWrite}}};
            commands.declareAccess(resolveAccesses);
#endif
            commands.dispatch(1, 1, 1);
            commands.endScope();
            gpu->submitAndEndFrame(commandsFrame);
            gpu->waitIdle();
        }

        std::array<float, 2> pair{};
        {
            const auto readbackBytes = lmx::render::readbackNow(
                *gpu, *exposure,
                {Stage::ComputeShader, Access::ShaderRead | Access::ShaderWrite, Layout::Undefined},
                {0, sizeof(pair)});
            INFO(lmx::test::errorOf(readbackBytes));
            REQUIRE(readbackBytes.has_value());
            std::memcpy(pair.data(), readbackBytes->data(), sizeof(pair));
        }
        return pair;
    };

    constexpr float kRate = 3.0f; // stops per second, in both directions for the walk below
    constexpr float kThreeStopsBelow = 0.125f;   // exp2(-3)
    constexpr float kThreeStopsAbove = 8.0f;     // exp2(+3)
    constexpr float kHalfWayBelow = 0.35355339f; // exp2(-1.5)
    constexpr float kHalfWayAbove = 2.82842712f; // exp2(+1.5)

    SECTION("rising three stops is half way after thirty frames and settled after sixty") {
        const std::array<float, 2> halfWay = runFrames(target * kThreeStopsBelow, kRate, kRate, 30);
        REQUIRE(halfWay[0] == Catch::Approx(target * kHalfWayBelow).epsilon(1e-3));

        const std::array<float, 2> settled = runFrames(target * kThreeStopsBelow, kRate, kRate, 60);
        REQUIRE(settled[0] == Catch::Approx(target).epsilon(1e-3));

        // The 60th frame is the one that lands on the target, so its `previous` is still the step
        // below it. One frame further and the pair holds the same exposure twice, which is what a
        // settled image means for the history correction: a ratio of one.
        const std::array<float, 2> held = runFrames(target * kThreeStopsBelow, kRate, kRate, 61);
        REQUIRE(held[0] == Catch::Approx(target).epsilon(1e-3));
        REQUIRE(held[1] == Catch::Approx(target).epsilon(1e-3));
    }

    SECTION("falling three stops is half way after thirty frames and settled after sixty") {
        const std::array<float, 2> halfWay = runFrames(target * kThreeStopsAbove, kRate, kRate, 30);
        REQUIRE(halfWay[0] == Catch::Approx(target * kHalfWayAbove).epsilon(1e-3));

        const std::array<float, 2> settled = runFrames(target * kThreeStopsAbove, kRate, kRate, 60);
        REQUIRE(settled[0] == Catch::Approx(target).epsilon(1e-3));
    }

    SECTION("the direction of the step selects which rate bounds it") {
        constexpr float kDownRate = 1.5f;
        // One frame down from three stops above the target moves by kDownRate * dt stops, not by
        // the rising rate the same call also passes.
        const std::array<float, 2> stepped =
            runFrames(target * kThreeStopsAbove, kRate, kDownRate, 1);
        const float expected = target * std::exp2(3.0f - kDownRate * kDeltaSeconds);
        REQUIRE(stepped[0] == Catch::Approx(expected).epsilon(1e-4));
        REQUIRE(stepped[1] == Catch::Approx(target * kThreeStopsAbove).epsilon(1e-4));
    }
}

//======================================================================================================================
// Isolate the final half-resolution bloom reconstruction from its compute chain. A checkerboard
// in the full-resolution scene blue channel must remain sharp; bloom's red/green ramps must
// interpolate before the display transform. The disabled case binds the actual 1x1 fallback shape.
TEST_CASE("display composition filters bloom while preserving exact scene texels", "[gpu]") {
    auto gpu = createTestGpu();
    REQUIRE(gpu != nullptr);
    auto library = gpu->loadShaderLibrary("DisplayTransform");
    INFO(lmx::test::errorOf(library));
    REQUIRE(library.has_value());
    const std::array<ColorTarget, 1> colors{{{.format = Format::BGRA8Unorm}}};
    auto pipeline = lmx::engine::adoptGpu(
        gpu->device(), gpu->device().createPipeline(
                           GraphicsPipelineDesc{.vertex = {library->get(), "vertexMain"},
                                                .fragment = {library->get(), "fragmentMain"},
                                                .raster = {.cull = CullMode::None},
                                                .depthStencil = {.depthTest = false,
                                                                 .depthWrite = false,
                                                                 .depthCompare = CompareOp::Always},
                                                .colors = colors,
                                                .label = "lmx.test.filteredDisplayPipeline"}));
    INFO(lmx::test::errorOf(pipeline));
    REQUIRE(pipeline.has_value());
    const std::array<uint16_t, 16> bloomPixels{0,
                                               0,
                                               0,
                                               halfPow2(0),
                                               halfPow2(-2),
                                               0,
                                               0,
                                               halfPow2(0),
                                               0,
                                               halfPow2(-1),
                                               0,
                                               halfPow2(0),
                                               halfPow2(-2),
                                               halfPow2(-1),
                                               0,
                                               halfPow2(0)};
    auto bloom = makeSceneColorTexture(*gpu, 2, 2, bloomPixels, "lmx.test.displayBloomRamp");
    INFO(lmx::test::errorOf(bloom));
    REQUIRE(bloom.has_value());
    const std::array<uint16_t, 4> black{};
    auto fallback = makeSceneColorTexture(*gpu, 1, 1, black, "lmx.test.displayBloomFallback");
    INFO(lmx::test::errorOf(fallback));
    REQUIRE(fallback.has_value());

    struct Probe {
        uint32_t width, height;
        std::array<float, 5> x, y;
    };
    const std::array<Probe, 2> probes{{
        {4, 4, {0.0f, 0.25f, 0.75f, 1.0f}, {0.0f, 0.25f, 0.75f, 1.0f}},
        {5, 3, {0.0f, 0.1f, 0.5f, 0.9f, 1.0f}, {0.0f, 0.5f, 1.0f}},
    }};
    for (const Probe& probe : probes) {
        std::vector<uint16_t> scenePixels(size_t{probe.width} * probe.height * 4, 0);
        for (uint32_t y = 0; y < probe.height; ++y) {
            for (uint32_t x = 0; x < probe.width; ++x) {
                scenePixels[(size_t{y} * probe.width + x) * 4 + 2] =
                    (x + y) % 2 == 0 ? halfPow2(-3) : halfPow2(-1);
            }
        }
        auto scene = makeSceneColorTexture(*gpu, probe.width, probe.height, scenePixels,
                                           "lmx.test.displayExactScene");
        INFO(lmx::test::errorOf(scene));
        REQUIRE(scene.has_value());
        auto dst = lmx::engine::GpuTexture::create(gpu->device(),
                                                   {.format = Format::BGRA8Unorm,
                                                    .extent = {probe.width, probe.height, 1},
                                                    .usage = TextureUsage::RenderTarget,
                                                    .label = "lmx.test.filteredDisplayDst"});
        INFO(lmx::test::errorOf(dst));
        REQUIRE(dst.has_value());
        bool displayed = false;
        for (const bool enabled : {false, true}) {
            const float intensity = enabled ? 1.0f : 0.0f;
            auto commandsFrame = gpu->beginFrame();
            auto& commands = commandsFrame.commands;
            const TextureBarrier beginDisplay{
                .texture = dst->rhi(),
                .range = {0, 1, 0, 1},
                .srcStage = displayed ? Stage::ColorAttachment : Stage::None,
                .srcAccess = displayed ? Access::ColorAttachmentWrite : Access::None,
                .dstStage = Stage::ColorAttachment,
                .dstAccess = Access::ColorAttachmentWrite,
                .oldLayout = displayed ? Layout::ColorAttachment : Layout::Undefined,
                .newLayout = Layout::ColorAttachment};
            commands.barrier({}, {}, std::span{&beginDisplay, 1});
            const ColorAttachment color{
                .texture = dst->rhi(), .load = LoadOp::Clear, .store = StoreOp::Store, .clear = {}};
            commands.beginRenderPass({.colors = std::span{&color, 1},
                                      .area = {0, 0, probe.width, probe.height},
                                      .label = "lmx.test.filteredDisplay"});
            commands.setViewport({.width = float(probe.width), .height = float(probe.height)});
            commands.setScissor({0, 0, probe.width, probe.height});
            commands.setPipeline(pipeline->get());
            commands.pushRoot(lmx::render::DisplayRoot{
                .sceneColor = static_cast<uint32_t>(scene->view()),
                .bloom = static_cast<uint32_t>(enabled ? bloom->view() : fallback->view()),
                .bloomIntensity = intensity});
#ifndef NDEBUG
            const std::array<ResourceAccess, 2> displayAccesses{
                {{.texture = scene->rhi(),
                  .range = {0, 1, 0, 1},
                  .stage = Stage::FragmentShader,
                  .access = Access::ShaderRead},
                 {.texture = enabled ? bloom->rhi() : fallback->rhi(),
                  .range = {0, 1, 0, 1},
                  .stage = Stage::FragmentShader,
                  .access = Access::ShaderRead}}};
            commands.declareAccess(std::span{displayAccesses}.first(enabled ? 2u : 1u));
#endif
            commands.draw(3);
            commands.endRenderPass();
            displayed = true;
            gpu->submitAndEndFrame(commandsFrame);
            gpu->waitIdle();
            std::vector<uint8_t> result(size_t{probe.width} * probe.height * 4);
            {
                const auto readbackBytes = lmx::render::readbackNow(
                    *gpu, *dst,
                    std::array{lmx::render::TextureRangeState{
                        .range = {0, 1, 0, 1},
                        .state = {Stage::ColorAttachment, Access::ColorAttachmentWrite,
                                  Layout::ColorAttachment}}});
                INFO(lmx::test::errorOf(readbackBytes));
                REQUIRE(readbackBytes.has_value());
                std::memcpy(result.data(), readbackBytes->data(), result.size());
            }
            for (uint32_t y = 0; y < probe.height; ++y) {
                for (uint32_t x = 0; x < probe.width; ++x) {
                    INFO(probe.width << "x" << probe.height << " bloom=" << enabled << " texel "
                                     << x << "," << y);
                    const auto expected = lmx::test::displayBytes(
                        {0.25f * probe.x[x] * intensity, 0.5f * probe.y[y] * intensity,
                         (x + y) % 2 == 0 ? 0.125f : 0.5f});
                    const size_t offset = (size_t{y} * probe.width + x) * 4;
                    for (size_t channel = 0; channel < 3; ++channel) {
                        REQUIRE(std::abs(int{result[offset + 2 - channel]} - expected[channel]) <=
                                1);
                    }
                    REQUIRE(result[offset + 3] == 255);
                }
            }
        }
    }
}
