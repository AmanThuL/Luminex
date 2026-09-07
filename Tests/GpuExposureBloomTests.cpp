//----------------------------------------------------------------------------------------------------------------------
/// @file GpuExposureBloomTests.cpp
/// @brief GPU conformance tests for the histogram exposure and bloom kernels vs CPU references.
//----------------------------------------------------------------------------------------------------------------------

// Run directly against CPU reference implementations that mirror the shaders line for line --
// Shaders/HistogramAccumulate.slang, Shaders/ExposureResolve.slang, and
// Shaders/BloomThreshold.slang
// -- rather than through Renderer::declarePasses(), so a failure here points at one kernel's math
// instead of the whole frame's wiring (spec 9/10).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Engine/GeometryGenerator.h"
#include "RHI/RHI.h"
#include "Render/Camera.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using namespace lmx::rhi;

//======================================================================================================================
template <typename T>
std::string errorOf(const Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// Mirrors HistogramAccumulate.slang's kHistogramBins/kLuminanceWeights and its own and
// ExposureResolve.slang's shared binning range -- Renderer.cpp's kExposureLogLuminanceMin/Max.
constexpr uint32_t kBins = 256;
constexpr glm::vec3 kLuminanceWeights{0.2126f, 0.7152f, 0.0722f};
constexpr float kLogLuminanceMin = -12.0f;
constexpr float kLogLuminanceMax = 4.0f;

//======================================================================================================================
// A half-float bit pattern for 2^exponent, exact because every power of two in the normal range
// has a zero mantissa -- so uploads need no float-to-half rounding logic to reason about.
constexpr uint16_t halfPow2(int exponent) {
    return static_cast<uint16_t>((exponent + 15) << 10);
}

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

// The two floats Shaders/ExposureResolve.slang and Shaders/ExposureSeed.slang keep in
// lmx.render.exposureBuffer: what the scene pass multiplies by next, and what it multiplied by on
// the previous declared temporal frame (M6.2 spec 7).
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

//======================================================================================================================
// Mirrors BloomThreshold.slang's per-texel formula (the 2x2 box read is the caller's job below,
// since it is identical for every probe pixel this file constructs).
glm::vec3 cpuThreshold(glm::vec3 color, float threshold) {
    const float luminance = glm::dot(color, kLuminanceWeights);
    const float contribution = std::max(luminance - threshold, 0.0f) / std::max(luminance, 1e-4f);
    return color * contribution;
}

// A tiny CPU grid with BloomDownsample.slang/BloomUpsample.slang's clamped-coordinate box filters,
// used to build the full-chain reference the bloom energy oracle compares the GPU chain against.
struct Grid {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<glm::vec3> texels;

    //==================================================================================================================
    Grid(uint32_t w, uint32_t h) : width(w), height(h), texels(size_t{w} * h, glm::vec3(0.0f)) {}

    //==================================================================================================================
    glm::vec3& at(uint32_t x, uint32_t y) { return texels[size_t{y} * width + x]; }

    //==================================================================================================================
    glm::vec3 load(uint32_t x, uint32_t y) const {
        const uint32_t cx = std::min(x, width - 1);
        const uint32_t cy = std::min(y, height - 1);
        return texels[size_t{cy} * width + cx];
    }
};

//======================================================================================================================
// Mirrors BloomDownsample.slang.
Grid cpuDownsample(const Grid& src, uint32_t dstWidth, uint32_t dstHeight) {
    Grid dst(dstWidth, dstHeight);
    for (uint32_t y = 0; y < dstHeight; ++y) {
        for (uint32_t x = 0; x < dstWidth; ++x) {
            const uint32_t bx = x * 2, by = y * 2;
            const glm::vec3 sum = src.load(bx, by) + src.load(bx + 1, by) + src.load(bx, by + 1) +
                                  src.load(bx + 1, by + 1);
            dst.at(x, y) = sum * 0.25f;
        }
    }
    return dst;
}

//======================================================================================================================
// Mirrors BloomUpsample.slang.
Grid cpuUpsampleAccumulate(const Grid& base, const Grid& small) {
    Grid dst(base.width, base.height);
    for (uint32_t y = 0; y < base.height; ++y) {
        for (uint32_t x = 0; x < base.width; ++x) {
            const uint32_t sx = std::min(x / 2, small.width - 1);
            const uint32_t sy = std::min(y / 2, small.height - 1);
            dst.at(x, y) = base.load(x, y) + small.load(sx, sy);
        }
    }
    return dst;
}

//======================================================================================================================
Result<std::unique_ptr<Texture>> makeSceneColorTexture(Device& device, uint32_t width,
                                                       uint32_t height,
                                                       std::span<const uint16_t> rgbaHalf,
                                                       const char* label) {
    const TextureMip mip{.data = rgbaHalf.data(),
                         .bytesPerRow = uint64_t{width} * 4 * sizeof(uint16_t)};
    return device.createTexture({.width = width,
                                 .height = height,
                                 .format = Format::RGBA16Float,
                                 .sampled = true,
                                 .label = label},
                                std::span{&mip, 1});
}

} // namespace

//======================================================================================================================
// A known 4x4 image (13 texels at luminance 1, one each at 2, 4, and 0.5 -- all exact powers of
// two, so both the display->scene-referred division and the log2 binning are exact) run through
// the real compute kernel and compared bin-for-bin against the CPU port above.
TEST_CASE("the GPU histogram matches a CPU reference on a known image", "[gpu]") {
    constexpr uint32_t kWidth = 4, kHeight = 4;
    constexpr float kPreExposure = 1.0f;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

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
        makeSceneColorTexture(**device, kWidth, kHeight, rgbaHalf, "lmx.test.histogramImage");
    INFO(errorOf(sceneColor));
    REQUIRE(sceneColor.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/HistogramAccumulate");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeHistogramAccumulate",
                                                      .threadsPerThreadgroup = {8, 8, 1},
                                                      .label = "lmx.test.histogramPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    auto histogram = (*device)->createBuffer({.size = kBins * sizeof(uint32_t),
                                              .storageRead = true,
                                              .storageWrite = true,
                                              .cpuReadback = true,
                                              .label = "lmx.test.histogramBuffer"},
                                             nullptr);
    INFO(errorOf(histogram));
    REQUIRE(histogram.has_value());
    // The kernel reads this frame's applied preExposure from a buffer, not a uniform (spec 9: the
    // same buffer ScenePass.slang/Sky.slang read in auto mode). A plain initial-data buffer stands
    // in for it here.
    auto exposure = (*device)->createBuffer(
        {.size = sizeof(float), .label = "lmx.test.histogramExposureBuffer"}, &kPreExposure);
    INFO(errorOf(exposure));
    REQUIRE(exposure.has_value());

    struct HistogramParams {
        float logLuminanceMin;
        float logLuminanceMax;
        uint32_t width;
        uint32_t height;
    };
    const HistogramParams params{.logLuminanceMin = kLogLuminanceMin,
                                 .logLuminanceMax = kLogLuminanceMax,
                                 .width = kWidth,
                                 .height = kHeight};

    CommandList& commands = (*device)->beginFrame();
    commands.beginCopyPass("lmx.test.clearHistogram");
    commands.fillBuffer(**histogram, 0, kBins * sizeof(uint32_t), 0);
    commands.endCopyPass();
    commands.bufferBarrier(**histogram, BufferUse::CopyDestination, BufferUse::StorageWrite);

    commands.beginComputePass("lmx.test.histogram");
    commands.bindComputePipeline(**pipeline);
    commands.bindTexture(0, **sceneColor);
    commands.bindStorageBuffer(0, **histogram, StorageAccess::ReadWrite);
    commands.bindBuffer(1, **exposure);
    commands.bindFrameData(2, params);
    commands.dispatch(1, 1, 1); // 4x4 fits one 8x8 threadgroup; the kernel bounds-checks the rest.
    commands.endComputePass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::array<uint32_t, kBins> gpuBins{};
    (*histogram)->readback(gpuBins.data(), gpuBins.size() * sizeof(uint32_t));

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

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    std::vector<uint16_t> rgbaHalf(size_t{kWidth} * kHeight * 4);
    const uint16_t channel = halfPow2(1); // 2.0
    for (size_t i = 0; i < size_t{kWidth} * kHeight; ++i) {
        rgbaHalf[i * 4 + 0] = channel;
        rgbaHalf[i * 4 + 1] = channel;
        rgbaHalf[i * 4 + 2] = channel;
        rgbaHalf[i * 4 + 3] = halfPow2(0);
    }
    auto sceneColor =
        makeSceneColorTexture(**device, kWidth, kHeight, rgbaHalf, "lmx.test.convergenceImage");
    INFO(errorOf(sceneColor));
    REQUIRE(sceneColor.has_value());

    auto histogramLibrary = (*device)->loadShaderLibrary("Shaders/HistogramAccumulate");
    INFO(errorOf(histogramLibrary));
    REQUIRE(histogramLibrary.has_value());
    auto histogramPipeline =
        (*device)->createComputePipeline({.library = histogramLibrary->get(),
                                          .computeEntry = "computeHistogramAccumulate",
                                          .threadsPerThreadgroup = {8, 8, 1},
                                          .label = "lmx.test.convergenceHistogramPipeline"});
    INFO(errorOf(histogramPipeline));
    REQUIRE(histogramPipeline.has_value());

    auto resolveLibrary = (*device)->loadShaderLibrary("Shaders/ExposureResolve");
    INFO(errorOf(resolveLibrary));
    REQUIRE(resolveLibrary.has_value());
    auto resolvePipeline =
        (*device)->createComputePipeline({.library = resolveLibrary->get(),
                                          .computeEntry = "computeExposureResolve",
                                          .threadsPerThreadgroup = {1, 1, 1},
                                          .label = "lmx.test.resolvePipeline"});
    INFO(errorOf(resolvePipeline));
    REQUIRE(resolvePipeline.has_value());

    auto histogram = (*device)->createBuffer({.size = kBins * sizeof(uint32_t),
                                              .storageRead = true,
                                              .storageWrite = true,
                                              .label = "lmx.test.convergenceHistogramBuffer"},
                                             nullptr);
    INFO(errorOf(histogram));
    REQUIRE(histogram.has_value());
    // The buffer the histogram kernel reads "this frame's applied preExposure" from (spec 9) --
    // separate from `exposure` below (what the resolve pass writes for *next* frame), exactly as
    // Renderer.cpp's two-buffer-role read/write is, just without the graph's version-chain naming
    // both roles onto one physical buffer.
    auto appliedExposure = (*device)->createBuffer(
        {.size = sizeof(float), .label = "lmx.test.convergenceAppliedExposureBuffer"},
        &kPreExposure);
    INFO(errorOf(appliedExposure));
    REQUIRE(appliedExposure.has_value());
    // The resolve reads the applied exposure it steps from out of index 0 and shifts it into
    // index 1, so the buffer it writes is the two-float pair and is seeded rather than left
    // uninitialised.
    constexpr std::array<float, 2> kInitialPair{kPreExposure, kPreExposure};
    auto exposure = (*device)->createBuffer({.size = sizeof(kInitialPair),
                                             .storageRead = true,
                                             .storageWrite = true,
                                             .cpuReadback = true,
                                             .label = "lmx.test.convergenceExposureBuffer"},
                                            kInitialPair.data());
    INFO(errorOf(exposure));
    REQUIRE(exposure.has_value());

    struct HistogramParams {
        float logLuminanceMin;
        float logLuminanceMax;
        uint32_t width;
        uint32_t height;
    };
    struct ExposureResolveParams {
        float lowPercentile, highPercentile, targetGrey, evMin, evMax, compensationEv;
        float logLuminanceMin, logLuminanceMax;
        float adaptUp, adaptDown, deltaSeconds, pad;
    };
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

    CommandList& commands = (*device)->beginFrame();
    commands.beginCopyPass("lmx.test.convergenceClear");
    commands.fillBuffer(**histogram, 0, kBins * sizeof(uint32_t), 0);
    commands.endCopyPass();
    commands.bufferBarrier(**histogram, BufferUse::CopyDestination, BufferUse::StorageWrite);

    commands.beginComputePass("lmx.test.convergenceHistogram");
    commands.bindComputePipeline(**histogramPipeline);
    commands.bindTexture(0, **sceneColor);
    commands.bindStorageBuffer(0, **histogram, StorageAccess::ReadWrite);
    commands.bindBuffer(1, **appliedExposure);
    commands.bindFrameData(2, histogramParams);
    commands.dispatch(1, 1, 1);
    commands.endComputePass();

    commands.bufferBarrier(**histogram, BufferUse::StorageWrite, BufferUse::StorageRead);

    commands.beginComputePass("lmx.test.convergenceResolve");
    commands.bindComputePipeline(**resolvePipeline);
    commands.bindStorageBuffer(0, **histogram, StorageAccess::Read);
    commands.bindStorageBuffer(1, **exposure, StorageAccess::ReadWrite);
    commands.bindFrameData(2, resolveParams);
    commands.dispatch(1, 1, 1);
    commands.endComputePass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::array<float, 2> gpuPair{};
    (*exposure)->readback(gpuPair.data(), sizeof(gpuPair));
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
// shouldResetExposure() (App/ExposureReset.h, Tests/AppExposureResetTests.cpp) says a reset is
// pending: an ordinary computeExposureSeed dispatch that writes exp2(manualEV) into the persistent
// exposure buffer, no CPU readback involved, and no reason to run the same kernel four times over
// -- which trigger fired is a CPU-only decision this test does not need to remake. What belongs
// here is the one thing shouldResetExposure()'s unit tests cannot cover: that the kernel a reset
// actually dispatches does the right GPU-side thing with the seed value it is given.
TEST_CASE("the exposure seed kernel writes exp2(manual EV) when a reset is applied", "[gpu]") {
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/ExposureSeed");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeExposureSeed",
                                                      .threadsPerThreadgroup = {1, 1, 1},
                                                      .label = "lmx.test.exposureSeedPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    struct ExposureSeedParams {
        float exposure;
    };

    const auto runSeed = [&](float manualEv) {
        // A stale pair the buffer might otherwise still hold, standing in for whatever the
        // previous scene/session/size left in it -- the seed dispatch must overwrite index 0
        // regardless of what it was.
        constexpr std::array<float, 2> kStalePair{0.1f, 0.2f};
        auto exposure = (*device)->createBuffer({.size = sizeof(kStalePair),
                                                 .storageRead = true,
                                                 .storageWrite = true,
                                                 .cpuReadback = true,
                                                 .label = "lmx.test.seedExposureBuffer"},
                                                kStalePair.data());
        const ExposureSeedParams params{.exposure = std::exp2(manualEv)};

        CommandList& commands = (*device)->beginFrame();
        commands.beginComputePass("lmx.test.exposureSeed");
        commands.bindComputePipeline(**pipeline);
        commands.bindStorageBuffer(0, **exposure, StorageAccess::ReadWrite);
        commands.bindFrameData(1, params);
        commands.dispatch(1, 1, 1);
        commands.endComputePass();
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();

        std::array<float, 2> result{};
        (*exposure)->readback(result.data(), sizeof(result));
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
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/ExposureSeed");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeExposureSeed",
                                                      .threadsPerThreadgroup = {1, 1, 1},
                                                      .label = "lmx.test.exposureShiftPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    struct ExposureSeedParams {
        float exposure;
    };

    constexpr std::array<float, 2> kInitialPair{0.1f, 0.2f};
    auto exposure = (*device)->createBuffer({.size = sizeof(kInitialPair),
                                             .storageRead = true,
                                             .storageWrite = true,
                                             .cpuReadback = true,
                                             .label = "lmx.test.shiftExposureBuffer"},
                                            kInitialPair.data());
    INFO(errorOf(exposure));
    REQUIRE(exposure.has_value());

    const auto seed = [&](float value) {
        const ExposureSeedParams params{.exposure = value};
        CommandList& commands = (*device)->beginFrame();
        commands.beginComputePass("lmx.test.exposureSeedShift");
        commands.bindComputePipeline(**pipeline);
        commands.bindStorageBuffer(0, **exposure, StorageAccess::ReadWrite);
        commands.bindFrameData(1, params);
        commands.dispatch(1, 1, 1);
        commands.endComputePass();
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();

        std::array<float, 2> pair{};
        (*exposure)->readback(pair.data(), sizeof(pair));
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

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/ExposureResolve");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline = (*device)->createComputePipeline({.library = library->get(),
                                                      .computeEntry = "computeExposureResolve",
                                                      .threadsPerThreadgroup = {1, 1, 1},
                                                      .label = "lmx.test.adaptResolvePipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    // A histogram the resolve re-reads unchanged every frame, so the target it meters is one
    // constant the adaptation walks toward.
    const std::array<uint32_t, kBins> bins = cpuHistogram(
        std::vector<glm::vec3>(size_t{kWidth} * kHeight, glm::vec3(kSceneLuminance)), 1.0f);
    auto histogram = (*device)->createBuffer(
        {.size = sizeof(bins), .storageRead = true, .label = "lmx.test.adaptHistogramBuffer"},
        bins.data());
    INFO(errorOf(histogram));
    REQUIRE(histogram.has_value());

    struct ExposureResolveParams {
        float lowPercentile, highPercentile, targetGrey, evMin, evMax, compensationEv;
        float logLuminanceMin, logLuminanceMax;
        float adaptUp, adaptDown, deltaSeconds, pad;
    };

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
        auto exposure = (*device)->createBuffer({.size = sizeof(initial),
                                                 .storageRead = true,
                                                 .storageWrite = true,
                                                 .cpuReadback = true,
                                                 .label = "lmx.test.adaptExposureBuffer"},
                                                initial.data());
        INFO(errorOf(exposure));
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
            CommandList& commands = (*device)->beginFrame();
            commands.beginComputePass("lmx.test.adaptResolve");
            commands.bindComputePipeline(**pipeline);
            commands.bindStorageBuffer(0, **histogram, StorageAccess::Read);
            commands.bindStorageBuffer(1, **exposure, StorageAccess::ReadWrite);
            commands.bindFrameData(2, params);
            commands.dispatch(1, 1, 1);
            commands.endComputePass();
            (*device)->endFrame(nullptr);
            (*device)->waitIdle();
        }

        std::array<float, 2> pair{};
        (*exposure)->readback(pair.data(), sizeof(pair));
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
// The whole frame this time, not one kernel: a manual-mode temporal frame has to leave the applied
// exposure and the one before it in the buffer, because that pair is what a temporal resolve
// corrects a reprojected history by (M6.2 spec 7). The renderer declares the seed on every such
// frame and exports what it wrote so dead-pass culling cannot drop it -- nothing in the frame
// itself reads it, its consumer is the next frame -- and the last case here is the other half of
// that rule: with temporal off, manual mode declares no seed at all, so the buffer is untouched
// and the pre-temporal frame's declaration stays exact.
TEST_CASE("a manual temporal frame records the EV edit and the value before it", "[gpu]") {
    using namespace lmx::render;

    constexpr uint32_t kExtent = 32;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto cube = createMesh(**device, makeCube(), "lmx.test.exposurePairCube");
    INFO(errorOf(cube));
    REQUIRE(cube.has_value());

    auto renderer = Renderer::create(**device, kExtent, kExtent, /*cpuReadback=*/true);
    INFO(errorOf(renderer));
    REQUIRE(renderer.has_value());

    Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};

    const std::array<DrawItem, 1> items = {DrawItem{.mesh = &*cube}};
    SceneView view;
    view.items = items;
    view.boundingSphere = {0.0f, 0.0f, 0.0f, 4.0f};
    view.temporal.enabled = true;

    const auto renderOneFrame = [&](float exposureEv, bool temporalEnabled) {
        view.exposureEv = exposureEv;
        view.temporal.enabled = temporalEnabled;
        CommandList& commands = (*device)->beginFrame();
        (*renderer)->render(commands, camera, view, /*barrierForSampling=*/false);
        (*device)->endFrame(nullptr);
        (*device)->waitIdle();

        std::array<float, lmx::render::kExposureBufferFloats> pair{};
        (*renderer)->exposureBuffer().readback(pair.data(), sizeof(pair));
        return pair;
    };

    // EV 0 first: the pair records unit exposure over whatever the buffer was created holding.
    const auto initial = renderOneFrame(0.0f, /*temporalEnabled=*/true);
    REQUIRE(initial[0] == Catch::Approx(1.0f));

    // The edit. The frame that applies exp2(2) is the frame that has to remember exp2(0).
    const auto edited = renderOneFrame(2.0f, /*temporalEnabled=*/true);
    REQUIRE(edited[0] == Catch::Approx(4.0f));
    REQUIRE(edited[1] == Catch::Approx(1.0f));

    // Temporal off declares no seed, so a further EV edit reaches shading through PassUniforms
    // alone and leaves the buffer exactly as the last temporal frame left it.
    const auto untouched = renderOneFrame(-1.0f, /*temporalEnabled=*/false);
    REQUIRE(untouched[0] == Catch::Approx(4.0f));
    REQUIRE(untouched[1] == Catch::Approx(1.0f));
}

//======================================================================================================================
// Renderer uses ceil(scene / 2) for bloom mip 0 so an odd-sized image's final source row and column
// still have a destination texel. Put the only above-threshold value in that bottom-right corner:
// floor division would allocate and dispatch only 2x1 here and silently lose the probe.
TEST_CASE("bloom threshold preserves the final texel of an odd-sized image", "[gpu]") {
    constexpr uint32_t kSrcWidth = 5, kSrcHeight = 3;
    constexpr uint32_t kDstWidth = 3, kDstHeight = 2;
    constexpr float kThreshold = 4.0f;

    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto library = (*device)->loadShaderLibrary("Shaders/BloomThreshold");
    INFO(errorOf(library));
    REQUIRE(library.has_value());
    auto pipeline =
        (*device)->createComputePipeline({.library = library->get(),
                                          .computeEntry = "computeBloomThreshold",
                                          .threadsPerThreadgroup = {8, 8, 1},
                                          .label = "lmx.test.oddBloomThresholdPipeline"});
    INFO(errorOf(pipeline));
    REQUIRE(pipeline.has_value());

    std::vector<uint16_t> source(size_t{kSrcWidth} * kSrcHeight * 4, halfPow2(0));
    const size_t probe = (size_t{kSrcHeight - 1} * kSrcWidth + (kSrcWidth - 1)) * 4;
    source[probe] = halfPow2(4);
    source[probe + 1] = halfPow2(4);
    source[probe + 2] = halfPow2(4);
    auto scene =
        makeSceneColorTexture(**device, kSrcWidth, kSrcHeight, source, "lmx.test.oddBloomScene");
    INFO(errorOf(scene));
    REQUIRE(scene.has_value());

    auto bloom = (*device)->createTexture({.width = kDstWidth,
                                           .height = kDstHeight,
                                           .format = Format::RGBA16Float,
                                           .storageWrite = true,
                                           .cpuReadback = true,
                                           .label = "lmx.test.oddBloomMip0"});
    INFO(errorOf(bloom));
    REQUIRE(bloom.has_value());

    struct ThresholdParams {
        float threshold;
        uint32_t srcWidth, srcHeight, dstWidth, dstHeight;
    };
    const ThresholdParams params{.threshold = kThreshold,
                                 .srcWidth = kSrcWidth,
                                 .srcHeight = kSrcHeight,
                                 .dstWidth = kDstWidth,
                                 .dstHeight = kDstHeight};

    CommandList& commands = (*device)->beginFrame();
    commands.beginComputePass("lmx.test.oddBloomThreshold");
    commands.bindComputePipeline(**pipeline);
    commands.bindTexture(0, **scene);
    commands.bindStorageTexture(1, **bloom, {}, StorageAccess::Write);
    commands.bindFrameData(0, params);
    commands.dispatch(1, 1, 1);
    commands.endComputePass();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint16_t> result(size_t{kDstWidth} * kDstHeight * 4);
    (*bloom)->readback(result.data(), result.size() * sizeof(uint16_t));
    const size_t last = (size_t{kDstHeight - 1} * kDstWidth + (kDstWidth - 1)) * 4;
    REQUIRE(result[last] != 0);
    REQUIRE(result[last + 1] != 0);
    REQUIRE(result[last + 2] != 0);
}

//======================================================================================================================
// Below-threshold content contributes nothing (an exact CPU match against every mip-0 texel the
// CPU chain also computes as zero, not just an ad hoc "elsewhere"), and a bright probe's energy
// after the full chain matches a CPU port of the same kernels within a stated tolerance -- spec
// 10's bloom energy oracle. Runs Renderer.cpp's own chain depth (kMaxBloomDownsampleLevels == 4,
// clamped for the extent), not a shortened stand-in, so the oracle covers what actually ships.
TEST_CASE("bloom threshold and the full four-level chain match a CPU reference", "[gpu]") {
    auto device = createDevice();
    INFO(errorOf(device));
    REQUIRE(device.has_value());

    auto thresholdLibrary = (*device)->loadShaderLibrary("Shaders/BloomThreshold");
    INFO(errorOf(thresholdLibrary));
    REQUIRE(thresholdLibrary.has_value());
    auto thresholdPipeline =
        (*device)->createComputePipeline({.library = thresholdLibrary->get(),
                                          .computeEntry = "computeBloomThreshold",
                                          .threadsPerThreadgroup = {8, 8, 1},
                                          .label = "lmx.test.bloomThresholdPipeline"});
    INFO(errorOf(thresholdPipeline));
    REQUIRE(thresholdPipeline.has_value());
    auto downsampleLibrary = (*device)->loadShaderLibrary("Shaders/BloomDownsample");
    INFO(errorOf(downsampleLibrary));
    REQUIRE(downsampleLibrary.has_value());
    auto downsamplePipeline =
        (*device)->createComputePipeline({.library = downsampleLibrary->get(),
                                          .computeEntry = "computeBloomDownsample",
                                          .threadsPerThreadgroup = {8, 8, 1},
                                          .label = "lmx.test.bloomDownsamplePipeline"});
    INFO(errorOf(downsamplePipeline));
    REQUIRE(downsamplePipeline.has_value());
    auto upsampleLibrary = (*device)->loadShaderLibrary("Shaders/BloomUpsample");
    INFO(errorOf(upsampleLibrary));
    REQUIRE(upsampleLibrary.has_value());
    auto upsamplePipeline =
        (*device)->createComputePipeline({.library = upsampleLibrary->get(),
                                          .computeEntry = "computeBloomUpsample",
                                          .threadsPerThreadgroup = {8, 8, 1},
                                          .label = "lmx.test.bloomUpsamplePipeline"});
    INFO(errorOf(upsamplePipeline));
    REQUIRE(upsamplePipeline.has_value());

    // 64x64 scene -> bloomChain mip 0..4 (32, 16, 8, 4, 2) -- Renderer.cpp's own
    // kMaxBloomDownsampleLevels -- -> bloomBlur mip 0..3 (32, 16, 8, 4). One bright probe texel,
    // well above threshold; a uniform background well below it.
    constexpr uint32_t kSceneSize = 64;
    constexpr uint32_t kLevels = 4; // downsample steps; mirrors Renderer.cpp's constant
    constexpr uint32_t kMip0Size = kSceneSize / 2;
    constexpr float kThreshold = 4.0f;
    constexpr uint32_t kProbeX = 0, kProbeY = 0; // scene texel; lands at bloom mip0 texel (0, 0)

    std::array<uint32_t, kLevels + 1> chainSize{};
    chainSize[0] = kMip0Size;
    for (uint32_t level = 1; level <= kLevels; ++level) {
        chainSize[level] = chainSize[level - 1] / 2;
    }

    std::vector<glm::vec3> scenePixels(size_t{kSceneSize} * kSceneSize, glm::vec3(1.0f));
    scenePixels[kProbeY * kSceneSize + kProbeX] = glm::vec3(16.0f);

    std::vector<uint16_t> rgbaHalf(scenePixels.size() * 4);
    for (size_t i = 0; i < scenePixels.size(); ++i) {
        const int exponent = static_cast<int>(std::round(std::log2(scenePixels[i].r)));
        const uint16_t channel = halfPow2(exponent);
        rgbaHalf[i * 4 + 0] = channel;
        rgbaHalf[i * 4 + 1] = channel;
        rgbaHalf[i * 4 + 2] = channel;
        rgbaHalf[i * 4 + 3] = halfPow2(0);
    }
    auto sceneColor =
        makeSceneColorTexture(**device, kSceneSize, kSceneSize, rgbaHalf, "lmx.test.bloomScene");
    INFO(errorOf(sceneColor));
    REQUIRE(sceneColor.has_value());

    auto bloomChain = (*device)->createTexture({.width = kMip0Size,
                                                .height = kMip0Size,
                                                .format = Format::RGBA16Float,
                                                .mipLevels = kLevels + 1,
                                                .storageRead = true,
                                                .storageWrite = true,
                                                .label = "lmx.test.bloomChain"});
    INFO(errorOf(bloomChain));
    REQUIRE(bloomChain.has_value());
    auto bloomBlur = (*device)->createTexture({.width = kMip0Size,
                                               .height = kMip0Size,
                                               .format = Format::RGBA16Float,
                                               .mipLevels = kLevels,
                                               .storageRead = true,
                                               .storageWrite = true,
                                               .cpuReadback = true,
                                               .label = "lmx.test.bloomBlur"});
    INFO(errorOf(bloomBlur));
    REQUIRE(bloomBlur.has_value());

    struct ThresholdParams {
        float threshold;
        uint32_t srcWidth, srcHeight, dstWidth, dstHeight;
    };
    struct DownsampleParams {
        uint32_t srcWidth, srcHeight, dstWidth, dstHeight;
    };
    struct UpsampleParams {
        uint32_t smallWidth, smallHeight, dstWidth, dstHeight;
    };
    const auto mipView = [](uint32_t level) {
        return TextureViewDesc{.range = {.baseMipLevel = level, .mipLevelCount = 1}};
    };

    CommandList& commands = (*device)->beginFrame();

    const ThresholdParams thresholdParams{.threshold = kThreshold,
                                          .srcWidth = kSceneSize,
                                          .srcHeight = kSceneSize,
                                          .dstWidth = kMip0Size,
                                          .dstHeight = kMip0Size};
    commands.beginComputePass("lmx.test.bloomThreshold");
    commands.bindComputePipeline(**thresholdPipeline);
    commands.bindTexture(0, **sceneColor);
    commands.bindStorageTexture(1, **bloomChain, mipView(0), StorageAccess::Write);
    commands.bindFrameData(0, thresholdParams);
    commands.dispatch(1, 1, 1);
    commands.endComputePass();

    // Downsample chain: mip (L - 1) -> mip L, one pass per level.
    for (uint32_t level = 1; level <= kLevels; ++level) {
        commands.textureBarrier(**bloomChain, TextureUse::StorageWrite, TextureUse::StorageRead);
        const DownsampleParams params{.srcWidth = chainSize[level - 1],
                                      .srcHeight = chainSize[level - 1],
                                      .dstWidth = chainSize[level],
                                      .dstHeight = chainSize[level]};
        commands.beginComputePass("lmx.test.bloomDownsample");
        commands.bindComputePipeline(**downsamplePipeline);
        commands.bindStorageTexture(0, **bloomChain, mipView(level - 1), StorageAccess::Read);
        commands.bindStorageTexture(1, **bloomChain, mipView(level), StorageAccess::Write);
        commands.bindFrameData(0, params);
        commands.dispatch(1, 1, 1);
        commands.endComputePass();
    }

    // Upsample-accumulate: walks kLevels back down to mip 0, one pass per level. The first step's
    // "small" input is bloomChain's own smallest mip; every later step's is the previous step's
    // own bloomBlur output.
    commands.textureBarrier(**bloomChain, TextureUse::StorageWrite, TextureUse::StorageRead);
    for (uint32_t stepsRemaining = kLevels; stepsRemaining > 0; --stepsRemaining) {
        const uint32_t level = stepsRemaining - 1; // walks kLevels - 1 down to 0
        const bool smallFromChain = level == kLevels - 1;
        const UpsampleParams params{.smallWidth = chainSize[level + 1],
                                    .smallHeight = chainSize[level + 1],
                                    .dstWidth = chainSize[level],
                                    .dstHeight = chainSize[level]};
        commands.beginComputePass("lmx.test.bloomUpsample");
        commands.bindComputePipeline(**upsamplePipeline);
        commands.bindStorageTexture(0, **bloomChain, mipView(level), StorageAccess::Read);
        if (smallFromChain) {
            commands.bindStorageTexture(1, **bloomChain, mipView(level + 1), StorageAccess::Read);
        } else {
            commands.bindStorageTexture(1, **bloomBlur, mipView(level + 1), StorageAccess::Read);
        }
        commands.bindStorageTexture(2, **bloomBlur, mipView(level), StorageAccess::Write);
        commands.bindFrameData(0, params);
        commands.dispatch(1, 1, 1);
        commands.endComputePass();
        if (level > 0) {
            commands.textureBarrier(**bloomBlur, TextureUse::StorageWrite, TextureUse::StorageRead);
        }
    }
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();

    std::vector<uint16_t> blurHalf(size_t{kMip0Size} * kMip0Size * 4);
    (*bloomBlur)->readback(blurHalf.data(), blurHalf.size() * sizeof(uint16_t));

    // Half -> float via the standard library's own round trip: only values this test produces (0,
    // sums of powers of two) are read back, all exactly representable.
    const auto toFloat = [](uint16_t half) {
        const uint32_t sign = (half >> 15) & 1u;
        const uint32_t exponent = (half >> 10) & 0x1Fu;
        const uint32_t mantissa = half & 0x3FFu;
        float value;
        if (exponent == 0) {
            value = std::ldexp(static_cast<float>(mantissa), -24);
        } else {
            value =
                std::ldexp(static_cast<float>(mantissa) + 1024.0f, static_cast<int>(exponent) - 25);
        }
        return sign != 0 ? -value : value;
    };
    const auto texelAt = [&](uint32_t x, uint32_t y) {
        const size_t base = (size_t{y} * kMip0Size + x) * 4;
        return glm::vec3(toFloat(blurHalf[base]), toFloat(blurHalf[base + 1]),
                         toFloat(blurHalf[base + 2]));
    };

    // CPU reference: the same kernels, over the same levels, run on the same scene pixels.
    Grid sceneGrid(kSceneSize, kSceneSize);
    for (uint32_t y = 0; y < kSceneSize; ++y) {
        for (uint32_t x = 0; x < kSceneSize; ++x) {
            sceneGrid.at(x, y) = scenePixels[y * kSceneSize + x];
        }
    }
    Grid mip0(kMip0Size, kMip0Size);
    for (uint32_t y = 0; y < kMip0Size; ++y) {
        for (uint32_t x = 0; x < kMip0Size; ++x) {
            const glm::vec3 sum = sceneGrid.load(x * 2, y * 2) + sceneGrid.load(x * 2 + 1, y * 2) +
                                  sceneGrid.load(x * 2, y * 2 + 1) +
                                  sceneGrid.load(x * 2 + 1, y * 2 + 1);
            mip0.at(x, y) = cpuThreshold(sum * 0.25f, kThreshold);
        }
    }
    std::vector<Grid> chainMips;
    chainMips.push_back(mip0);
    for (uint32_t level = 1; level <= kLevels; ++level) {
        chainMips.push_back(cpuDownsample(chainMips.back(), chainSize[level], chainSize[level]));
    }
    Grid accumulated = chainMips[kLevels]; // the smallest mip, as its own "previous step" result
    for (uint32_t stepsRemaining = kLevels; stepsRemaining > 0; --stepsRemaining) {
        const uint32_t level = stepsRemaining - 1;
        accumulated = cpuUpsampleAccumulate(chainMips[level], accumulated);
    }
    const Grid& expectedBlur = accumulated;

    // Below-threshold background: every mip-0 texel the CPU reference itself computed as exactly
    // zero has to read back as exactly zero from the GPU too -- the bloom counterpart of the
    // histogram test's exact CPU match, proving "below-threshold contributes nothing" over the
    // whole image rather than a hand-picked region.
    bool sawAZeroTexel = false;
    for (uint32_t y = 0; y < kMip0Size; ++y) {
        for (uint32_t x = 0; x < kMip0Size; ++x) {
            const glm::vec3 expected = expectedBlur.load(x, y);
            if (expected.r != 0.0f || expected.g != 0.0f || expected.b != 0.0f) {
                continue;
            }
            sawAZeroTexel = true;
            const glm::vec3 gpu = texelAt(x, y);
            INFO("below-threshold-only texel (" << x << "," << y << ")");
            REQUIRE(gpu.r == 0.0f);
            REQUIRE(gpu.g == 0.0f);
            REQUIRE(gpu.b == 0.0f);
        }
    }
    // The probe's footprint doubles each upsample level (2^kLevels texels wide out of kMip0Size),
    // so most of a 32-wide mip 0 is still background; this is the oracle's own sanity that the
    // zero check above actually exercised something.
    REQUIRE(sawAZeroTexel);

    // The probe: GPU result within 1% of the CPU chain's own prediction.
    const glm::vec3 gpuProbe = texelAt(0, 0);
    const glm::vec3 expectedProbe = expectedBlur.load(0, 0);
    INFO("expected " << expectedProbe.r << ", got " << gpuProbe.r);
    REQUIRE(expectedProbe.r > 0.0f); // the oracle itself has to be measuring something
    REQUIRE(gpuProbe.r == Catch::Approx(expectedProbe.r).epsilon(0.01));
}
